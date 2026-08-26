// vim: set foldmethod=marker foldlevel=0:
#define _GNU_SOURCE
#include <cute.h>
#include <blog.h>
#define BGAME_SCENE_NAME editor
#include <bgame/utils.h>
#include <bgame/allocator.h>
#include <bgame/allocator/frame.h>
#include <grain.h>
#include <dcimgui.h>
#include <bco.h>
#include <barray.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#ifndef __EMSCRIPTEN__
#	define BRESMON_API static
#	include <bresmon.h>
#endif

#define UFA_ARENA_TYPE barena_t
#include <ufa.h>

#include "../debug_draw.h"

#define start_modal_action(FN, ...) \
	do { \
		if (bco_status(modal_action) == BCO_TERMINATED) { \
			bco_spawn(modal_action, FN, __VA_ARGS__); \
		} \
	} while (0)

// States

// UI states {{{

SCENE_VAR(bool, show_emitters)
SCENE_VAR(bool, show_affectors)
SCENE_VAR(bool, show_renderer)
SCENE_VAR(bool, show_log)
SCENE_VAR(bool, log_auto_scroll)

SCENE_VAR(int, gui_emitter_index)
SCENE_VAR(int, gui_affector_index)
SCENE_VAR(int, gui_renderer_index)

SCENE_VAR(bool, show_system)
SCENE_VAR(float, emission_rate)
SCENE_VAR(int, burst_count)

// The pool options in effect; grain_pool_t is opaque so the editor tracks them
SCENE_VAR(float, current_lifetime_budget)
SCENE_VAR(float, current_max_emission_rate)
SCENE_VAR(int, current_max_burst_size)
// Edited in the System window, applied only when the pool is recreated
SCENE_VAR(float, pending_lifetime_budget)
SCENE_VAR(float, pending_max_emission_rate)
SCENE_VAR(int, pending_max_burst_size)

// }}}

// Grain {{{

typedef struct {
	grain_module_ref_t ref;
	char* source;
	char* path;

#ifndef __EMSCRIPTEN__
	bresmon_watch_t* watch;
#endif
} module_meta_t;

typedef struct {
	char data[128];
} system_name_t;

SCENE_VAR(grain_t*, grain)
SCENE_VAR(char*, tmp_source_buf)
SCENE_VAR(char*, last_module_path)
SCENE_VAR(char*, last_system_path)
SCENE_VAR(system_name_t, system_name)
SCENE_VAR(grain_renderer_t*, noop_renderer)

SCENE_VAR(barray(grain_emitter_t*), archetype_emitters)
SCENE_VAR(barray(grain_affector_t*), archetype_affectors)
SCENE_VAR(grain_renderer_t*, archetype_renderer)
SCENE_VAR(grain_archetype_t*, archetype)
SCENE_VAR(grain_pool_t*, pool)
SCENE_VAR(grain_system_t*, particle_system)

#ifndef __EMSCRIPTEN__
SCENE_VAR(bresmon_t*, bresmon)
#endif

// }}}

// Document lifecycle {{{

SCENE_VAR(bool, unsaved_changes)
SCENE_VAR(ufa_file_ref_t*, current_file_ref)
SCENE_VAR(CK_MAP(module_meta_t*), emitters)
SCENE_VAR(CK_MAP(module_meta_t*), affectors)
SCENE_VAR(CK_MAP(module_meta_t*), renderers)

// A sampler slot's binding as the editor tracks it: the archetype only holds
// GPU handles, the path is editor state. The binding owns its decoded pixels
// and derives its texture from them on demand.
typedef struct {
	char* path;  // sstring; NULL when the slot is unbound
	CF_Image image;  // pix == NULL when the file could not be resolved
	CF_Texture texture;
} texture_binding_meta_t;

// Keyed by "<kind>:<module_index>:<sampler_name>" so the same module in two
// slots keeps distinct bindings
SCENE_VAR(CK_MAP(texture_binding_meta_t*), texture_bindings)
SCENE_VAR(char*, last_texture_path)

// }}}

// Modal {{{

SCENE_VAR(char*, popup_error)
SCENE_VAR(char*, attributions_text)

static _Alignas(bco_align_t) char modal_action_storage[2048];
static bco_t* modal_action = (bco_t*)modal_action_storage;

static bool should_begin_native_modal = false;
static bool should_end_native_modal = true;

static bool should_popup_error = false;
static bool should_popup_about = false;

typedef enum {
	SAVE_PROMPT_PENDING,
	SAVE_PROMPT_SAVE,
	SAVE_PROMPT_DONT_SAVE,
	SAVE_PROMPT_CANCEL,
} save_prompt_result_t;

static bool should_prompt_unsaved = false;
static bool unsaved_prompt_active = false;
static save_prompt_result_t save_prompt_result = SAVE_PROMPT_CANCEL;

// Result of the last do_save_system run, for callers that chain on it
static bool save_flow_succeeded = false;

// Menu items and shortcut keys funnel into one of these each frame
typedef enum {
	CMD_NONE,
	CMD_NEW,
	CMD_OPEN,
	CMD_SAVE,
	CMD_SAVE_AS,
} editor_command_t;

// }}}

// Logging {{{

typedef struct {
	int offset;
	int len;
	blog_level_t level;
} log_entry_t;

SCENE_VAR(char*, log_text)
SCENE_VAR(barray(log_entry_t), log_lines)

// Both reset on live reload, in sync with blog's own logger registry which
// also lives in (bgame's) statics.
static bool log_sink_hooked = false;
static bool log_sink_active = false;

// }}}

// Functions

// Logging {{{

static void
editor_log_sink(const blog_ctx_t* ctx, blog_str_t msg, void* userdata) {
	(void)userdata;
	if (!log_sink_active) { return; }

	const char* line_begin = msg.data;
	const char* msg_end = msg.data + msg.len;
	while (line_begin < msg_end) {
		const char* line_end = memchr(line_begin, '\n', (size_t)(msg_end - line_begin));
		if (line_end == NULL) { line_end = msg_end; }

		log_entry_t entry = {
			.offset = slen(log_text),
			.len = (int)(line_end - line_begin),
			.level = ctx->level,
		};
		sappend_range(log_text, line_begin, line_end);
		barray_push(log_lines, entry, scene_allocator);

		line_begin = line_end + 1;
	}
}

static void
hook_log_sink(void) {
	if (!log_sink_hooked) {
		blog_add_logger(BLOG_LEVEL_TRACE, editor_log_sink, NULL);
		log_sink_hooked = true;
	}
	log_sink_active = true;
}

static ImVec4
log_level_color(blog_level_t level) {
	switch (level) {
		case BLOG_LEVEL_TRACE: return (ImVec4){ 0.5f, 0.5f, 0.5f, 1.0f };
		case BLOG_LEVEL_DEBUG: return (ImVec4){ 0.4f, 0.7f, 1.0f, 1.0f };
		case BLOG_LEVEL_INFO:  return (ImVec4){ 0.9f, 0.9f, 0.9f, 1.0f };
		case BLOG_LEVEL_WARN:  return (ImVec4){ 1.0f, 0.8f, 0.2f, 1.0f };
		case BLOG_LEVEL_ERROR: return (ImVec4){ 1.0f, 0.4f, 0.4f, 1.0f };
		case BLOG_LEVEL_FATAL: return (ImVec4){ 1.0f, 0.2f, 0.6f, 1.0f };
		default:               return (ImVec4){ 1.0f, 1.0f, 1.0f, 1.0f };
	}
}

// }}}

// Modal {{{

static void
begin_native_modal(void) {
	should_begin_native_modal = true;
	should_end_native_modal = false;

	bgame_block_reload();
}

static void
end_native_modal(void) {
	bgame_unblock_reload();

	should_end_native_modal = true;
}

static void
show_error(const char* error) {
	sset(popup_error, error);
	should_popup_error = true;
}

// }}}

// Grain {{{

static const char*
read_open_file(ufa_open_file_t* open_file) {
	char read_buf[1024];
	sclear(tmp_source_buf);
	while (true) {
		size_t size = sizeof(read_buf);
		if (ufa_read_open_file(open_file, read_buf, &size) != UFA_OK) {
			return NULL;
		}

		if (size == 0) { break; }
		sappend_range(tmp_source_buf, read_buf, read_buf + size);
	}

	return tmp_source_buf;
}

#ifndef __EMSCRIPTEN__

static void
reload_module(const char* path, void* userdata) {
	module_meta_t* module_meta = userdata;

	barena_t arena;
	barena_init(&arena, bgame_arena_pool);

	ufa_open_file_t* file = ufa_begin_open_file(
		(ufa_config_t){
			.arena = &arena,
			.memalign = barena_memalign,
			.filename = path,
		}
	);

	if (ufa_check_open_file(file) != UFA_OK) {
		BLOG_ERROR("Could not reopen %s", path);
		goto end;
	}

	const char* source = read_open_file(file);
	if (source == NULL) {
		BLOG_ERROR("Error while reading %s: %s", path, ufa_get_open_file_error(file));
		goto end;
	}

	void* new_module = NULL;
	switch (module_meta->ref.kind) {
		case GRAIN_MODULE_EMITTER:
			new_module = grain_define_emitter(grain, source);
			break;
		case GRAIN_MODULE_AFFECTOR:
			new_module = grain_define_affector(grain, source);
			break;
		case GRAIN_MODULE_RENDERER:
			new_module = grain_define_renderer(grain, source);
			break;
		default:
			break;
	}
	if (new_module == NULL) {
		BLOG_ERROR("Error while reading %s: %s", path, grain_get_last_error(grain));
		goto end;
	}

	sset(module_meta->source, source);
	BLOG_INFO("Reloaded %s", path);

end:
	ufa_end_open_file(file);
	barena_reset(&arena);
}

static void
watch_module(const char* path, module_meta_t* module_meta) {
	bresmon_init_watch(bresmon, &module_meta->watch, path, reload_module, module_meta);
}

static void
reinit_watch(CK_MAP(module_meta_t*) module_map) {
	for (int i = 0; i < map_size(module_map); ++i) {
		bresmon_set_watch_callback(module_map[i]->watch, reload_module, module_map[i]);
	}
}

#endif

static int
param_type_size(CF_ShaderInfoDataType type) {
	switch (type) {
		case CF_SHADER_INFO_TYPE_SINT:
		case CF_SHADER_INFO_TYPE_UINT:
		case CF_SHADER_INFO_TYPE_FLOAT:
			return 4;
		case CF_SHADER_INFO_TYPE_SINT2:
		case CF_SHADER_INFO_TYPE_UINT2:
		case CF_SHADER_INFO_TYPE_FLOAT2:
			return 8;
		case CF_SHADER_INFO_TYPE_SINT3:
		case CF_SHADER_INFO_TYPE_UINT3:
		case CF_SHADER_INFO_TYPE_FLOAT3:
			return 12;
		case CF_SHADER_INFO_TYPE_SINT4:
		case CF_SHADER_INFO_TYPE_UINT4:
		case CF_SHADER_INFO_TYPE_FLOAT4:
			return 16;
		default:
			return 0;
	}
}

static void
remember_directory(char** dir_var, const char* file_path) {
	int slash_index;
	for (slash_index = (int)strlen(file_path); slash_index >= 0; --slash_index) {
		if (file_path[slash_index] == '/' || file_path[slash_index] == '\\') { break; }
	}
	if (slash_index > 0) {
		sclear(*dir_var);
		sappend_range(*dir_var, file_path, file_path + slash_index);
	}
}

// SDL reads the filter list when the dialog's callback fires, long after the
// begin call has yielded out of the coroutine, so the filters cannot be
// compound literals in the coroutine's (transient) stack frame
static const ufa_filter_t module_file_filters[] = {
	{ .name = "grain module", .pattern = "shd;glsl" },
	{ .name = "All files", .pattern = "*" },
};
static const ufa_filter_t system_file_filters[] = {
	{ .name = "grain system", .pattern = "json" },
	{ .name = "All files", .pattern = "*" },
};
static const ufa_filter_t image_file_filters[] = {
	{ .name = "PNG image", .pattern = "png" },
	{ .name = "All files", .pattern = "*" },
};

bco_static(import_module) {
	bco_vars(
		barena_t arena;
		ufa_open_file_t* open_file;
	)

	bco_begin
	begin_native_modal();

	barena_init(&bco_var(arena), bgame_arena_pool);
	bco_var(open_file) = ufa_begin_open_file((ufa_config_t){
		.arena = &bco_var(arena),
		.memalign = barena_memalign,
		.parent_window = cf_app_get_window(),
		.filters = module_file_filters,
		.num_filters = CF_ARRAY_SIZE(module_file_filters),
		.directory = last_module_path,
	});

	while (ufa_check_open_file(bco_var(open_file)) == UFA_PENDING) {
		bco_yield();
	}

	ufa_status_t open_status = ufa_check_open_file(bco_var(open_file));
	if (open_status == UFA_CANCELLED) { bco_return(); }
	if (open_status == UFA_ERROR) {
		show_error(ufa_get_open_file_error(bco_var(open_file)));
		bco_return();
	}

	const char* source = read_open_file(bco_var(open_file));
	if (source == NULL) {
		show_error(ufa_get_open_file_error(bco_var(open_file)));
		bco_return();
	}

	const char* module_path = ufa_get_open_file_name(bco_var(open_file));

	grain_module_ref_t module_ref = grain_define_module(grain, source);
	if (module_ref.kind == GRAIN_MODULE_INVALID) {
		const char* error = grain_get_last_error(grain);
		show_error(error);
		BLOG_ERROR("Error while loading %s: %s", module_path, error);
		bco_return();
	}

	const char* module_name = NULL;
	CK_MAP(module_meta_t*)* module_map = NULL;
	const char* type_name = NULL;
	switch (module_ref.kind) {
		case GRAIN_MODULE_EMITTER: {
			module_name = grain_get_emitter_name(module_ref.module);
			module_map = &emitters;
			type_name = "emitter";
		} break;
		case GRAIN_MODULE_AFFECTOR: {
			module_name = grain_get_affector_name(module_ref.module);
			module_map = &affectors;
			type_name = "affector";
		} break;
		case GRAIN_MODULE_RENDERER: {
			module_name = grain_get_renderer_name(module_ref.module);
			module_map = &renderers;
			type_name = "renderer";
		} break;
		default: bco_return();
	}

	module_meta_t* module_meta = map_get(*module_map, module_name);
	if (module_meta == NULL) {
		module_meta = bgame_malloc(sizeof(module_meta_t), scene_allocator);
		map_set(*module_map, module_name, module_meta);
		*module_meta = (module_meta_t){
			.ref = module_ref,
		};

#ifndef __EMSCRIPTEN__
		watch_module(ufa_get_open_file_name(bco_var(open_file)), module_meta);
#endif
	}
	sset(module_meta->source, source);
	sset(module_meta->path, module_path);

	remember_directory(&last_module_path, module_path);

	BLOG_INFO("Loaded %s %s", type_name, module_path);

	bco_end

	ufa_end_open_file(bco_var(open_file));
	barena_reset(&bco_var(arena));
	end_native_modal();
}

static CK_MAP(module_meta_t*)*
module_map_for_kind(grain_module_kind_t kind) {
	switch (kind) {
		case GRAIN_MODULE_EMITTER: return &emitters;
		case GRAIN_MODULE_AFFECTOR: return &affectors;
		case GRAIN_MODULE_RENDERER: return &renderers;
		default: return NULL;
	}
}

// Texture bindings {{{

static const char*
texture_binding_key(grain_module_kind_t kind, int module_index, const char* sampler_name) {
	char buf[256];
	snprintf(buf, sizeof(buf), "%d:%d:%s", (int)kind, module_index, sampler_name);
	return sintern(buf);
}

static texture_binding_meta_t*
get_or_create_texture_binding(const char* key) {
	texture_binding_meta_t* meta = map_get(texture_bindings, key);
	if (meta == NULL) {
		meta = bgame_malloc(sizeof(texture_binding_meta_t), scene_allocator);
		*meta = (texture_binding_meta_t){ 0 };
		map_set(texture_bindings, key, meta);
	}
	return meta;
}

static void
unbind_texture_meta(texture_binding_meta_t* meta) {
	if (meta->texture.id != 0) {
		cf_destroy_texture(meta->texture);
		meta->texture = (CF_Texture){ 0 };
	}
	cf_image_free(&meta->image);
	meta->image = (CF_Image){ 0 };
	sfree(meta->path);
	meta->path = NULL;
}

static void
clear_texture_bindings(void) {
	for (int i = 0; i < map_size(texture_bindings); ++i) {
		unbind_texture_meta(texture_bindings[i]);
		bgame_free(texture_bindings[i], scene_allocator);
	}
	map_free(texture_bindings);
	texture_bindings = NULL;
}

static bool
set_texture_binding_image(
	texture_binding_meta_t* meta,
	const char* path,
	const char* data,
	size_t size
) {
	CF_Image image = { 0 };
	if (cf_is_error(cf_image_load_png_from_memory(data, (int)size, &image))) {
		return false;
	}

	unbind_texture_meta(meta);
	meta->image = image;
	sset(meta->path, path);
	return true;
}

#ifndef __EMSCRIPTEN__

static bool
load_texture_binding(texture_binding_meta_t* meta, const char* path) {
	barena_t arena;
	barena_init(&arena, bgame_arena_pool);
	ufa_open_file_t* file = ufa_begin_open_file((ufa_config_t){
		.arena = &arena,
		.memalign = barena_memalign,
		.filename = path,
	});

	bool ok = false;
	if (ufa_check_open_file(file) == UFA_OK) {
		const char* data = read_open_file(file);
		if (data != NULL) {
			ok = set_texture_binding_image(meta, path, data, (size_t)slen(data));
		}
	}

	ufa_end_open_file(file);
	barena_reset(&arena);
	return ok;
}

#endif

// Pushes every bound slot at the pool. Runs each frame so pool recreation and
// archetype reload need no special-casing
static void
apply_module_texture_bindings(
	grain_module_kind_t kind,
	int module_index,
	const grain_module_info_t* module,
	grain_archetype_info_t* archetype_info
) {
	for (int i = 0; i < module->num_samplers; ++i) {
		int sampler_index = module->first_sampler + i;
		const grain_sampler_info_t* sampler_info = &archetype_info->samplers[sampler_index];
		texture_binding_meta_t* meta = map_get(
			texture_bindings,
			texture_binding_key(kind, module_index, sampler_info->name)
		);
		if (meta == NULL || meta->image.pix == NULL) { continue; }

		if (meta->texture.id == 0) {
			CF_TextureParams params = cf_texture_defaults(
				meta->image.w, meta->image.h
			);
			meta->texture = cf_make_texture(params);
			cf_texture_update(
				meta->texture,
				meta->image.pix,
				meta->image.w * meta->image.h * (int)sizeof(CF_Pixel)
			);
		}
		grain_set_texture(pool, sampler_index, (grain_texture_binding_t){
			.texture = meta->texture,
		});
	}
}

static void
apply_texture_bindings(grain_archetype_info_t* archetype_info) {
	for (int i = 0; i < archetype_info->num_emitters; ++i) {
		apply_module_texture_bindings(
			GRAIN_MODULE_EMITTER, i, &archetype_info->emitters[i], archetype_info
		);
	}
	for (int i = 0; i < archetype_info->num_affectors; ++i) {
		apply_module_texture_bindings(
			GRAIN_MODULE_AFFECTOR, i, &archetype_info->affectors[i], archetype_info
		);
	}
	apply_module_texture_bindings(
		GRAIN_MODULE_RENDERER, 0, &archetype_info->renderer, archetype_info
	);
}

bco_static(pick_texture, const char* binding_key) {
	bco_vars(
		barena_t arena;
		ufa_open_file_t* open_file;
	)

	bco_begin
	begin_native_modal();

	barena_init(&bco_var(arena), bgame_arena_pool);
	bco_var(open_file) = ufa_begin_open_file((ufa_config_t){
		.arena = &bco_var(arena),
		.memalign = barena_memalign,
		.parent_window = cf_app_get_window(),
		.filters = image_file_filters,
		.num_filters = CF_ARRAY_SIZE(image_file_filters),
		.directory = last_texture_path,
	});

	while (ufa_check_open_file(bco_var(open_file)) == UFA_PENDING) {
		bco_yield();
	}

	ufa_status_t open_status = ufa_check_open_file(bco_var(open_file));
	if (open_status == UFA_CANCELLED) { bco_return(); }
	if (open_status == UFA_ERROR) {
		show_error(ufa_get_open_file_error(bco_var(open_file)));
		bco_return();
	}

	const char* data = read_open_file(bco_var(open_file));
	if (data == NULL) {
		show_error(ufa_get_open_file_error(bco_var(open_file)));
		bco_return();
	}

	const char* path = ufa_get_open_file_name(bco_var(open_file));
	texture_binding_meta_t* meta = get_or_create_texture_binding(bco_arg(binding_key));
	// Decode first: a bad file leaves the previous binding untouched
	if (!set_texture_binding_image(meta, path, data, (size_t)slen(data))) {
		show_error("Not a valid PNG file");
		bco_return();
	}
	remember_directory(&last_texture_path, path);
	unsaved_changes = true;

	BLOG_INFO("Bound texture %s", path);

	bco_end

	ufa_end_open_file(bco_var(open_file));
	barena_reset(&bco_var(arena));
	end_native_modal();
}

// }}}

// The new pool is created before the old one is destroyed so that a rejected
// configuration (grain_create_pool validates it) leaves the current pool running.
static void
recreate_pool(grain_archetype_info_t* archetype_info) {
	grain_pool_t* new_pool = grain_create_pool(grain, (grain_pool_opts_t){
		.archetype = archetype,
		.lifetime_budget = pending_lifetime_budget,
		.max_emission_rate = pending_max_emission_rate,
		.max_burst_size = pending_max_burst_size,
		.max_systems = 1,
	});
	if (new_pool == NULL) {
		BLOG_ERROR("Could not recreate pool: %s", grain_get_last_error(grain));
		show_error(grain_get_last_error(grain));
		return;
	}

	grain_system_t* new_system = grain_create_system(new_pool);

	// Module params live in the pool's buffers; carry them over so Apply does
	// not reset everything the user has tuned.
	int num_params = archetype_info->renderer.first_param + archetype_info->renderer.num_params;
	for (int i = 0; i < num_params; ++i) {
		int size = param_type_size(archetype_info->params[i].type);
		void* src = grain_get_parameter(particle_system, i);
		void* dst = grain_get_parameter(new_system, i);
		if (src != NULL && dst != NULL && size > 0) {
			memcpy(dst, src, size);
			grain_parameter_modified(new_system, i);
		}
	}

	grain_destroy_pool(pool);
	pool = new_pool;
	particle_system = new_system;

	current_lifetime_budget = pending_lifetime_budget;
	current_max_emission_rate = pending_max_emission_rate;
	current_max_burst_size = pending_max_burst_size;
	unsaved_changes = true;

	BLOG_INFO(
		"Recreated pool: %.1f particles/s max, %.1fs lifetime budget, %d max burst",
		current_max_emission_rate, current_lifetime_budget, current_max_burst_size
	);
}

// }}}

// Editor widgets {{{

static const char*
module_list_name_getter(void* user_data, int idx) {
	// Map keys are uint64_t-widened interned pointers so the key array cannot
	// be reinterpreted as const char*[] on 32-bit targets (wasm).
	CK_MAP(module_meta_t*) module_map = user_data;
	return (const char*)(uintptr_t)map_keys(module_map)[idx];
}

static bool
show_module_list(CK_MAP(module_meta_t*) module_map, const char* label, int* current_item) {
	return ImGui_ComboCallback(label, current_item, module_list_name_getter, module_map, map_size(module_map));
}

static void
show_module_params(
	const grain_module_info_t* module,
	grain_archetype_info_t* archetype_info
) {
	for (int i = 0; i < module->num_params; ++i) {
		int param_index = module->first_param + i;
		const grain_param_info_t* param_info = &archetype_info->params[param_index];

		const grain_param_decorator_t* range_decorator = grain_find_decorator(param_info, "range");

		bool has_min = false;
		float min_val;
		bool has_max = false;
		float max_val;
		float step = 1.f;
		if (range_decorator) {
			grain_decorator_arg_t arg;
			int arg_index = 0;
			if (
				grain_find_decorator_arg(range_decorator, arg_index++, "min", &arg)
				&&
				arg.type == GRAIN_DECORATOR_ARG_NUMBER
			) {
				has_min = true;
				min_val = arg.value.number;
			}

			if (
				grain_find_decorator_arg(range_decorator, arg_index++, "max", &arg)
				&&
				arg.type == GRAIN_DECORATOR_ARG_NUMBER
			) {
				has_max = true;
				max_val = arg.value.number;
			}

			if (
				grain_find_decorator_arg(range_decorator, arg_index++, "step", &arg)
				&&
				arg.type == GRAIN_DECORATOR_ARG_NUMBER
			) {
				step = arg.value.number;
			}
		}

		const grain_param_decorator_t* color_decorator = grain_find_decorator(param_info, "color");

		int num_components = 0;
		ImGuiDataType type = ImGuiDataType_COUNT;

		switch (param_info->type) {
			case CF_SHADER_INFO_TYPE_SINT:
			case CF_SHADER_INFO_TYPE_UINT:
			case CF_SHADER_INFO_TYPE_FLOAT:
				num_components = 1;
				break;
			case CF_SHADER_INFO_TYPE_SINT2:
			case CF_SHADER_INFO_TYPE_UINT2:
			case CF_SHADER_INFO_TYPE_FLOAT2:
				num_components = 2;
				break;
			case CF_SHADER_INFO_TYPE_SINT3:
			case CF_SHADER_INFO_TYPE_UINT3:
			case CF_SHADER_INFO_TYPE_FLOAT3:
				num_components = 3;
				break;
			case CF_SHADER_INFO_TYPE_SINT4:
			case CF_SHADER_INFO_TYPE_UINT4:
			case CF_SHADER_INFO_TYPE_FLOAT4:
				num_components = 4;
				break;
			default:
				break;
		}

		switch (param_info->type) {
			case CF_SHADER_INFO_TYPE_SINT:
			case CF_SHADER_INFO_TYPE_SINT2:
			case CF_SHADER_INFO_TYPE_SINT3:
			case CF_SHADER_INFO_TYPE_SINT4:
				type = ImGuiDataType_S32;
				break;
			case CF_SHADER_INFO_TYPE_UINT:
			case CF_SHADER_INFO_TYPE_UINT2:
			case CF_SHADER_INFO_TYPE_UINT3:
			case CF_SHADER_INFO_TYPE_UINT4:
				type = ImGuiDataType_U32;
				break;
			case CF_SHADER_INFO_TYPE_FLOAT:
			case CF_SHADER_INFO_TYPE_FLOAT2:
			case CF_SHADER_INFO_TYPE_FLOAT3:
			case CF_SHADER_INFO_TYPE_FLOAT4:
				type = ImGuiDataType_Float;
				break;
			default:
				break;
		}

		void* param_addr = grain_get_parameter(particle_system, param_index);
		bool updated = false;

		if (color_decorator != NULL) {
			switch (param_info->type) {
				case CF_SHADER_INFO_TYPE_UINT:
					uint32_t packed = *(uint32_t*)param_addr;
					CF_Color color = cf_pixel_to_color((CF_Pixel){ .val = packed });
					updated = ImGui_ColorEdit4(
						param_info->name,
						&color.r,
						ImGuiColorEditFlags_AlphaBar
					);
					if (updated) {
						*(uint32_t*)param_addr = cf_color_to_pixel(color).val;
					}
					break;
				case CF_SHADER_INFO_TYPE_FLOAT3:
					updated = ImGui_ColorEdit3(
						param_info->name,
						param_addr,
						0
					);
					break;
				case CF_SHADER_INFO_TYPE_FLOAT4:
					updated = ImGui_ColorEdit4(
						param_info->name,
						param_addr,
						ImGuiColorEditFlags_AlphaBar
					);
					break;
				default:
					color_decorator = NULL;  // Invalid decorator, treat as number
					break;
			}
		}

		if (color_decorator == NULL) {
			void* min_ptr = NULL;
			void* max_ptr = NULL;
			if (has_min) min_ptr = type == ImGuiDataType_Float ? (void*)&min_val : (void*)&(int){ (int)min_val };
			if (has_max) max_ptr = type == ImGuiDataType_Float ? (void*)&max_val : (void*)&(int){ (int)max_val };

			if (has_min && has_max) {
				updated = ImGui_SliderScalarNEx(
					param_info->name,
					type,
					param_addr,
					num_components,
					min_ptr, max_ptr,
					NULL,
					ImGuiSliderFlags_AlwaysClamp
				);
			} else {
				updated = ImGui_DragScalarNEx(
					param_info->name,
					type,
					param_addr, num_components,
					step,
					min_ptr, max_ptr,
					NULL,
					ImGuiSliderFlags_AlwaysClamp
				);
			}
		}

		debug_draw_param(
			particle_system,
			archetype_info,
			module,
			param_index,
			ImGui_IsItemHovered(0) || ImGui_IsItemActive()
		);

		if (updated) {
			grain_parameter_modified(particle_system, param_index);
			unsaved_changes = true;
		}
	}
}

static void
show_module_samplers(
	grain_module_kind_t kind,
	int module_index,
	const grain_module_info_t* module,
	grain_archetype_info_t* archetype_info
) {
	for (int i = 0; i < module->num_samplers; ++i) {
		int sampler_index = module->first_sampler + i;
		const grain_sampler_info_t* sampler_info =
			&archetype_info->samplers[sampler_index];
		const char* key = texture_binding_key(kind, module_index, sampler_info->name);
		texture_binding_meta_t* meta = map_get(texture_bindings, key);
		bool has_path = meta != NULL && meta->path != NULL;

		ImGui_PushID(sampler_info->name);

		ImGui_Text(
			"%s: %s%s",
			sampler_info->name,
			has_path ? meta->path : "(none)",
			has_path && meta->image.pix == NULL ? " (not loaded)" : ""
		);
		if (ImGui_Button("Browse...")) {
			start_modal_action(pick_texture, key);
		}
		if (has_path) {
			ImGui_SameLine();
			if (ImGui_Button("Clear")) {
				unbind_texture_meta(meta);
				grain_set_texture(pool, sampler_index, (grain_texture_binding_t){ 0 });
				unsaved_changes = true;
			}
		}

		ImGui_PopID();
	}
}

// }}}

// Document lifecycle {{{

static const char*
save_module_path(void* userdata, grain_module_kind_t kind, const char* module_name) {
	(void)userdata;
	CK_MAP(module_meta_t*)* module_map = module_map_for_kind(kind);
	if (module_map == NULL) { return NULL; }

	module_meta_t* module_meta = map_get(*module_map, sintern(module_name));
	return module_meta != NULL ? module_meta->path : NULL;
}

static const char*
save_texture_path(
	void* userdata,
	grain_module_kind_t kind,
	int module_index,
	const char* module_name,
	const char* sampler_name
) {
	(void)userdata;
	(void)module_name;
	texture_binding_meta_t* meta = map_get(
		texture_bindings, texture_binding_key(kind, module_index, sampler_name)
	);
	return meta != NULL ? meta->path : NULL;
}

// Waits until the user picks a choice in the "Unsaved changes" modal.
// The choice is left in save_prompt_result.
bco_static(prompt_unsaved_changes) {
	bco_begin

	save_prompt_result = SAVE_PROMPT_PENDING;
	should_prompt_unsaved = true;
	while (save_prompt_result == SAVE_PROMPT_PENDING) {
		bco_yield();
	}

	bco_end
}

// Save to current_file_ref without a dialog when one is held ("Save"), or
// always through a dialog when force_dialog is set ("Save as").
// Success is left in save_flow_succeeded for chained flows.
bco_static(do_save_system, bool force_dialog) {
	bco_vars(
		barena_t arena;
		ufa_save_file_t* save_file;
	)

	bco_begin
	save_flow_succeeded = false;
	begin_native_modal();

	barena_init(&bco_var(arena), bgame_arena_pool);
	bco_var(save_file) = ufa_begin_save_file((ufa_config_t){
		.arena = &bco_var(arena),
		.memalign = barena_memalign,
		.parent_window = cf_app_get_window(),
		.filters = system_file_filters,
		.num_filters = CF_ARRAY_SIZE(system_file_filters),
		.directory = last_system_path,
		.suggested_name = system_name.data[0] != '\0' ? system_name.data : NULL,
		.file_ref = bco_arg(force_dialog) ? NULL : current_file_ref,
	});

	while (ufa_check_save_file(bco_var(save_file)) == UFA_PENDING) {
		bco_yield();
	}

	ufa_status_t save_status = ufa_check_save_file(bco_var(save_file));
	if (save_status == UFA_CANCELLED) { bco_return(); }
	if (save_status == UFA_ERROR) {
		show_error(ufa_get_save_file_error(bco_var(save_file)));
		bco_return();
	}

	CF_JDoc doc = cf_make_json(NULL, 0);
	CF_JVal jsystem = grain_save_system(
		grain,
		particle_system,
		(grain_save_opts_t){
			.name = system_name.data[0] != '\0' ? system_name.data : NULL,
			.emission_rate = emission_rate,
			.module_path = save_module_path,
			.texture_path = save_texture_path,
		},
		doc
	);
	if (jsystem.id == 0) {
		show_error(grain_get_last_error(grain));
		cf_destroy_json(doc);
		bco_return();
	}
	cf_json_set_root(doc, jsystem);

	char* json_text = cf_json_to_string(doc);
	const char* file_path = ufa_get_save_file_name(bco_var(save_file));

	size_t total = (size_t)slen(json_text);
	size_t written = 0;
	bool write_ok = true;
	while (written < total) {
		size_t size = total - written;
		if (ufa_write_save_file(bco_var(save_file), json_text + written, &size) != UFA_OK) {
			show_error(ufa_get_save_file_error(bco_var(save_file)));
			write_ok = false;
			break;
		}
		written += size;
	}

	sfree(json_text);
	cf_destroy_json(doc);

	if (write_ok) {
		ufa_file_ref_t* new_ref = ufa_ref_save_file(bco_var(save_file));
		if (new_ref != NULL) {
			if (current_file_ref != NULL) { ufa_release_file_ref(current_file_ref); }
			current_file_ref = new_ref;
		}

		unsaved_changes = false;
		save_flow_succeeded = true;
		remember_directory(&last_system_path, file_path);
		BLOG_INFO("Saved system to %s", file_path);
	}

	bco_end

	ufa_end_save_file(bco_var(save_file));
	barena_reset(&bco_var(arena));
	end_native_modal();
}

static void
reset_editor_system(void) {
	barray_clear(archetype_emitters);
	barray_clear(archetype_affectors);
	archetype_renderer = noop_renderer;
	gui_renderer_index = -1;
	clear_texture_bindings();

	grain_archetype_t* new_archetype = grain_define_archetype(
		grain, "Editor",
		(grain_archetype_spec_t){
			.emitters = archetype_emitters,
			.num_emitters = barray_len(archetype_emitters),

			.affectors = archetype_affectors,
			.num_affectors = barray_len(archetype_affectors),

			.renderer = archetype_renderer,
		}
	);
	if (new_archetype == NULL) {
		BLOG_ERROR("Could not reset archetype: %s", grain_get_last_error(grain));
		show_error(grain_get_last_error(grain));
		return;
	}
	archetype = new_archetype;

	grain_pool_t* new_pool = grain_create_pool(grain, (grain_pool_opts_t){
		.archetype = archetype,
		.lifetime_budget = 16.f,
		.max_emission_rate = 512.f,
		.max_burst_size = 256,
		.max_systems = 1,
	});
	if (new_pool == NULL) {
		BLOG_ERROR("Could not recreate pool: %s", grain_get_last_error(grain));
		show_error(grain_get_last_error(grain));
		return;
	}
	grain_destroy_pool(pool);
	pool = new_pool;
	particle_system = grain_create_system(new_pool);

	emission_rate = 10.f;
	burst_count = 50;
	current_lifetime_budget = pending_lifetime_budget = 16.f;
	current_max_emission_rate = pending_max_emission_rate = 512.f;
	current_max_burst_size = pending_max_burst_size = 256;
	snprintf(system_name.data, sizeof(system_name.data), "%s", "Effect");

	if (current_file_ref != NULL) {
		ufa_release_file_ref(current_file_ref);
		current_file_ref = NULL;
	}
	unsaved_changes = false;

	BLOG_INFO("Created a new system");
}

bco_static(new_system) {
	bco_begin

	if (unsaved_changes) {
		bco_call(prompt_unsaved_changes);
		if (save_prompt_result == SAVE_PROMPT_CANCEL) { bco_return(); }
		if (save_prompt_result == SAVE_PROMPT_SAVE) {
			bco_call(do_save_system, false);
			if (!save_flow_succeeded) { bco_return(); }
		}
	}

	reset_editor_system();

	bco_end
}

static bool
apply_blueprint_to_editor(grain_blueprint_t* blueprint) {
	// Modules first, so the archetype rebuild below sees fresh definitions
	int num_modules = grain_blueprint_num_modules(blueprint);
	for (int i = 0; i < num_modules; ++i) {
		grain_blueprint_module_t bp_module = grain_blueprint_get_module(blueprint, i);
		CK_MAP(module_meta_t*)* module_map = module_map_for_kind(bp_module.ref.kind);
		if (module_map == NULL) { continue; }

		module_meta_t* module_meta = map_get(*module_map, bp_module.name);
		if (module_meta == NULL) {
			module_meta = bgame_malloc(sizeof(module_meta_t), scene_allocator);
			*module_meta = (module_meta_t){ 0 };
			map_set(*module_map, bp_module.name, module_meta);
		}
		module_meta->ref = bp_module.ref;
		sset(module_meta->source, bp_module.source);

		if (bp_module.path != NULL) {
			sset(module_meta->path, bp_module.path);
#ifndef __EMSCRIPTEN__
			// Safe even when the file is missing: bresmon degrades to a NULL
			// watch on Linux and to a directory-level watch on Windows, which
			// even picks the file up if it appears later.
			watch_module(module_meta->path, module_meta);

			FILE* file = fopen(bp_module.path, "rb");
			if (file != NULL) {
				fclose(file);
				// Prefer the on-disk version over the embedded snapshot
				reload_module(module_meta->path, module_meta);
			} else {
				BLOG_WARN("%s is missing; using the embedded source", bp_module.path);
			}
#endif
		}
	}

	// Archetype composition
	grain_archetype_info_t info = grain_inspect_archetype(
		grain_blueprint_archetype(blueprint)
	);
	barray_clear(archetype_emitters);
	for (int i = 0; i < info.num_emitters; ++i) {
		module_meta_t* module_meta = map_get(emitters, info.emitters[i].name);
		barray_push(archetype_emitters, module_meta->ref.module, scene_allocator);
	}
	barray_clear(archetype_affectors);
	for (int i = 0; i < info.num_affectors; ++i) {
		module_meta_t* module_meta = map_get(affectors, info.affectors[i].name);
		barray_push(archetype_affectors, module_meta->ref.module, scene_allocator);
	}
	module_meta_t* renderer_meta = map_get(renderers, info.renderer.name);
	archetype_renderer = renderer_meta->ref.module;
	gui_renderer_index = -1;
	for (int i = 0; i < map_size(renderers); ++i) {
		if (renderers[i] == renderer_meta) {
			gui_renderer_index = i;
			break;
		}
	}

	// The editor's own archetype, rebuilt with the loaded composition
	grain_archetype_t* new_archetype = grain_define_archetype(
		grain, "Editor",
		(grain_archetype_spec_t){
			.emitters = archetype_emitters,
			.num_emitters = barray_len(archetype_emitters),

			.affectors = archetype_affectors,
			.num_affectors = barray_len(archetype_affectors),

			.renderer = archetype_renderer,
		}
	);
	if (new_archetype == NULL) {
		BLOG_ERROR("Could not rebuild archetype: %s", grain_get_last_error(grain));
		show_error(grain_get_last_error(grain));
		return false;
	}
	archetype = new_archetype;

	// Pool with the loaded config
	grain_pool_opts_t loaded_opts = grain_blueprint_pool_opts(blueprint);
	grain_pool_t* new_pool = grain_create_pool(grain, (grain_pool_opts_t){
		.archetype = archetype,
		.lifetime_budget = loaded_opts.lifetime_budget,
		.max_emission_rate = loaded_opts.max_emission_rate,
		.max_burst_size = loaded_opts.max_burst_size,
		.max_systems = 1,
	});
	if (new_pool == NULL) {
		BLOG_ERROR("Could not recreate pool: %s", grain_get_last_error(grain));
		show_error(grain_get_last_error(grain));
		return false;
	}
	grain_destroy_pool(pool);
	pool = new_pool;
	particle_system = grain_create_system(new_pool);

	current_lifetime_budget = pending_lifetime_budget = loaded_opts.lifetime_budget;
	current_max_emission_rate = pending_max_emission_rate = loaded_opts.max_emission_rate;
	current_max_burst_size = pending_max_burst_size = loaded_opts.max_burst_size;

	// Texture bindings: the blueprint carries paths only
	clear_texture_bindings();
	int num_textures = grain_blueprint_num_textures(blueprint);
	for (int i = 0; i < num_textures; ++i) {
		grain_blueprint_texture_info_t texture_info =
			grain_blueprint_get_texture(blueprint, i);
		texture_binding_meta_t* meta = get_or_create_texture_binding(
			texture_binding_key(
				texture_info.kind, texture_info.module_index, texture_info.sampler_name
			)
		);
#ifndef __EMSCRIPTEN__
		if (!load_texture_binding(meta, texture_info.path)) {
			BLOG_WARN("%s is missing; leaving `%s` unbound",
				texture_info.path, texture_info.sampler_name);
			sset(meta->path, texture_info.path);
		}
#else
		// The web build can only reach files through a dialog: keep the path
		// for saving, sample the fallback until the user rebinds
		BLOG_WARN("Cannot resolve %s; rebind `%s` manually",
			texture_info.path, texture_info.sampler_name);
		sset(meta->path, texture_info.path);
#endif
	}

	// Params and system state
	grain_blueprint_apply(blueprint, particle_system);
	emission_rate = grain_blueprint_emission_rate(blueprint);
	snprintf(
		system_name.data, sizeof(system_name.data),
		"%s", grain_blueprint_name(blueprint)
	);

	BLOG_INFO(
		"Loaded system `%s`: %d emitter(s), %d affector(s), renderer %s",
		grain_blueprint_name(blueprint),
		info.num_emitters, info.num_affectors, info.renderer.name
	);
	return true;
}

bco_static(open_system) {
	bco_vars(
		barena_t arena;
		ufa_open_file_t* open_file;
		bool dialog_started;
	)

	bco_begin

	if (unsaved_changes) {
		bco_call(prompt_unsaved_changes);
		if (save_prompt_result == SAVE_PROMPT_CANCEL) { bco_return(); }
		if (save_prompt_result == SAVE_PROMPT_SAVE) {
			bco_call(do_save_system, false);
			if (!save_flow_succeeded) { bco_return(); }
		}
	}

	bco_var(dialog_started) = true;
	begin_native_modal();

	barena_init(&bco_var(arena), bgame_arena_pool);
	bco_var(open_file) = ufa_begin_open_file((ufa_config_t){
		.arena = &bco_var(arena),
		.memalign = barena_memalign,
		.parent_window = cf_app_get_window(),
		.filters = system_file_filters,
		.num_filters = CF_ARRAY_SIZE(system_file_filters),
		.directory = last_system_path,
	});

	while (ufa_check_open_file(bco_var(open_file)) == UFA_PENDING) {
		bco_yield();
	}

	ufa_status_t open_status = ufa_check_open_file(bco_var(open_file));
	if (open_status == UFA_CANCELLED) { bco_return(); }
	if (open_status == UFA_ERROR) {
		show_error(ufa_get_open_file_error(bco_var(open_file)));
		bco_return();
	}

	const char* content = read_open_file(bco_var(open_file));
	if (content == NULL) {
		show_error(ufa_get_open_file_error(bco_var(open_file)));
		bco_return();
	}

	const char* file_path = ufa_get_open_file_name(bco_var(open_file));

	CF_JDoc doc = cf_make_json(content, (size_t)slen(content));
	CF_JVal jroot = cf_json_get_root(doc);
	grain_blueprint_t* blueprint = NULL;
	if (jroot.id == 0) {
		show_error("Not a valid JSON file");
	} else {
		blueprint = grain_load_blueprint(grain, jroot);
		if (blueprint == NULL) {
			const char* error = grain_get_last_error(grain);
			show_error(error);
			BLOG_ERROR("Error while loading %s: %s", file_path, error);
		}
	}
	cf_destroy_json(doc);
	if (blueprint == NULL) { bco_return(); }

	remember_directory(&last_system_path, file_path);
	bool applied = apply_blueprint_to_editor(blueprint);
	grain_destroy_blueprint(blueprint);

	if (applied) {
		if (current_file_ref != NULL) { ufa_release_file_ref(current_file_ref); }
		current_file_ref = ufa_ref_open_file(bco_var(open_file));
		unsaved_changes = false;
	}

	bco_end

	// The unsaved-changes guard can return before any resource is acquired
	if (bco_var(dialog_started)) {
		ufa_end_open_file(bco_var(open_file));
		barena_reset(&bco_var(arena));
		end_native_modal();
	}
}

// }}}

// Scene lifecycle {{{

static void
cleanup_module_map(CK_MAP(module_meta_t*)* module_map) {
	for (int i = 0; i < map_size(*module_map); ++i) {
		sfree((*module_map)[i]->source);
		sfree((*module_map)[i]->path);
		bgame_free((*module_map)[i], scene_allocator);
	}
	map_free(*module_map);
}

static void
after_reload(void) {
	hook_log_sink();

#ifndef __EMSCRIPTEN__
	reinit_watch(emitters);
	reinit_watch(affectors);
	reinit_watch(renderers);
#endif
}

static void
init(void) {
	cf_clear_color(0.5f, 0.5f, 0.5f, 0.0f);

	hook_log_sink();

	if (bgame_current_scene_state() == BGAME_SCENE_INITIALIZING) {
		show_emitters = true;
		show_affectors = true;
		show_renderer = true;
		show_log = false;
		show_system = true;
		log_auto_scroll = true;
		gui_renderer_index = -1;

		emission_rate = 10.f;
		burst_count = 50;
		current_lifetime_budget = pending_lifetime_budget = 16.f;
		current_max_emission_rate = pending_max_emission_rate = 512.f;
		current_max_burst_size = pending_max_burst_size = 256;
		snprintf(system_name.data, sizeof(system_name.data), "%s", "Effect");

		grain = grain_create();

		noop_renderer = grain_define_renderer(
			grain,
			"Renderer(Noop)\n"
			"Requires(\n"
			")\n"
			"Params(\n"
			")\n"
			"#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX\n"
			"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
			"	cull();\n"
			"}\n"
			"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
			"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
			"	discard;\n"
			"}\n"
			"#endif"
		);
		if (noop_renderer == NULL) {
			BLOG_FATAL("Could not define Noop renderer: %s", grain_get_last_error(grain));
		}
		archetype_renderer = noop_renderer;

		archetype = grain_define_archetype(grain, "Editor", (grain_archetype_spec_t){
			.emitters = archetype_emitters,
			.num_emitters = barray_len(archetype_emitters),

			.affectors = archetype_affectors,
			.num_affectors = barray_len(archetype_affectors),

			.renderer = archetype_renderer,
		});
		if (archetype == NULL) {
			BLOG_FATAL("Could not define archetype: %s", grain_get_last_error(grain));
		}

		pool = grain_create_pool(grain, (grain_pool_opts_t){
			.archetype = archetype,
			.lifetime_budget = current_lifetime_budget,
			.max_emission_rate = current_max_emission_rate,
			.max_burst_size = current_max_burst_size,
			.max_systems = 1,
		});

		particle_system = grain_create_system(pool);

#ifndef __EMSCRIPTEN__
		bresmon = bresmon_create(scene_allocator);
#endif
	}
}

static void
cleanup(void) {
	log_sink_active = false;

	grain_destroy_pool(pool);

	sfree(log_text);
	barray_free(log_lines, scene_allocator);

	barray_free(archetype_emitters, scene_allocator);
	barray_free(archetype_affectors, scene_allocator);

#ifndef __EMSCRIPTEN__
	bresmon_destroy(bresmon);
#endif
	grain_destroy(grain);

	cleanup_module_map(&emitters);
	cleanup_module_map(&affectors);
	cleanup_module_map(&renderers);
	clear_texture_bindings();
	sfree(tmp_source_buf);
	sfree(popup_error);
	sfree(attributions_text);
	sfree(last_module_path);
	sfree(last_system_path);
	sfree(last_texture_path);

	ufa_release_file_ref(current_file_ref);
}

static void
update(void) {
	cf_app_update(NULL);
	cf_clear_canvas(cf_app_get_canvas());

	bool should_rebuild_archetype = false;
	editor_command_t pending_command = CMD_NONE;

	ImGui_DockSpaceOverViewportEx(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

	debug_draw_begin();

// Menu bar {{{
	if (ImGui_BeginMainMenuBar()) {
		if (ImGui_BeginMenu("File")) {
			if (ImGui_MenuItemEx("New", "Ctrl+N", false, true)) {
				pending_command = CMD_NEW;
			}

			if (ImGui_MenuItemEx("Open", "Ctrl+O", false, true)) {
				pending_command = CMD_OPEN;
			}

			if (ImGui_MenuItemEx("Save", "Ctrl+S", false, true)) {
				pending_command = CMD_SAVE;
			}

			if (ImGui_MenuItemEx("Save as", "Ctrl+Shift+S", false, true)) {
				pending_command = CMD_SAVE_AS;
			}

			ImGui_Separator();

			if (ImGui_MenuItem("Import module")) {
				start_modal_action(import_module);
			}

			ImGui_EndMenu();
		}

		if (ImGui_BeginMenu("View")) {
			if (ImGui_MenuItemEx("Emitters", NULL, show_emitters, true)) {
				show_emitters = !show_emitters;
			}

			if (ImGui_MenuItemEx("Affectors", NULL, show_affectors, true)) {
				show_affectors = !show_affectors;
			}

			if (ImGui_MenuItemEx("Renderer", NULL, show_renderer, true)) {
				show_renderer = !show_renderer;
			}

			if (ImGui_MenuItemEx("System", NULL, show_system, true)) {
				show_system = !show_system;
			}

			if (ImGui_MenuItemEx("Log", NULL, show_log, true)) {
				show_log = !show_log;
			}

			ImGui_EndMenu();
		}

		if (ImGui_BeginMenu("Help")) {
			if (ImGui_MenuItem("About")) {
				should_popup_about = true;
			}

			ImGui_EndMenu();
		}
		ImGui_EndMainMenuBar();
	}
// }}}

// Commands {{{
	if (ImGui_Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) {
		pending_command = CMD_NEW;
	}
	if (ImGui_Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
		pending_command = CMD_OPEN;
	}
	if (ImGui_Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
		pending_command = CMD_SAVE;
	}
	if (ImGui_Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
		pending_command = CMD_SAVE_AS;
	}

	// Menu items and shortcuts meet here so both trigger the same handler.
	// start_modal_action already ignores commands while one is in progress.
	switch (pending_command) {
		case CMD_NEW:
			start_modal_action(new_system);
			break;
		case CMD_OPEN:
			start_modal_action(open_system);
			break;
		case CMD_SAVE:
			start_modal_action(do_save_system, false);
			break;
		case CMD_SAVE_AS:
			start_modal_action(do_save_system, true);
			break;
		case CMD_NONE:
			break;
	}
// }}}

// Param windows {{{
	grain_archetype_info_t archetype_info = grain_inspect_archetype(archetype);
	grain_begin_update(grain);
	grain_set_emission_rate(particle_system, emission_rate);

	if (show_system) {
		if (ImGui_Begin("System", &show_system, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui_InputText("Name", system_name.data, sizeof(system_name.data), 0)) {
				unsaved_changes = true;
			}

			if (ImGui_DragFloatEx(
				"Emission rate", &emission_rate,
				1.f, 0.f, current_max_emission_rate, "%.1f/s",
				ImGuiSliderFlags_AlwaysClamp
			)) {
				unsaved_changes = true;
			}

			ImGui_BeginDisabled(current_max_burst_size == 0);
			ImGui_DragIntEx(
				"Burst count", &burst_count,
				1.f, 1, current_max_burst_size > 0 ? current_max_burst_size : 1, "%d",
				ImGuiSliderFlags_AlwaysClamp
			);
			if (ImGui_Button("Burst")) {
				grain_burst(particle_system, burst_count);
			}
			ImGui_EndDisabled();
			if (current_max_burst_size == 0) {
				ImGui_SameLine();
				ImGui_TextDisabledUnformatted("Set a max burst size to enable");
			}

			ImGui_SeparatorText("Pool");
			ImGui_DragFloatEx(
				"Lifetime budget", &pending_lifetime_budget,
				0.1f, 0.1f, FLT_MAX, "%.1fs",
				ImGuiSliderFlags_AlwaysClamp
			);
			ImGui_DragFloatEx(
				"Max emission rate", &pending_max_emission_rate,
				1.f, 1.f, FLT_MAX, "%.1f/s",
				ImGuiSliderFlags_AlwaysClamp
			);
			ImGui_DragIntEx(
				"Max burst size", &pending_max_burst_size,
				1.f, 0, INT_MAX, "%d",
				ImGuiSliderFlags_AlwaysClamp
			);
			ImGui_TextDisabled(
				"Pool capacity: %d particles",
				(int)ceilf(pending_max_emission_rate * pending_lifetime_budget)
					+ pending_max_burst_size
			);

			bool pool_dirty =
				pending_lifetime_budget != current_lifetime_budget
				|| pending_max_emission_rate != current_max_emission_rate
				|| pending_max_burst_size != current_max_burst_size;
			ImGui_BeginDisabled(!pool_dirty);
			if (ImGui_Button("Apply")) {
				recreate_pool(&archetype_info);
			}
			ImGui_EndDisabled();
			if (pool_dirty) {
				ImGui_SameLine();
				ImGui_TextDisabledUnformatted("Recreates the pool; live particles reset");
			}
		}
		ImGui_End();
	}

	if (show_emitters) {
		if (ImGui_Begin("Emitters", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
			int remove_index = -1;
			for (int i = 0; i < archetype_info.num_emitters; ++i) {
				ImGui_PushIDInt(i);

				const grain_module_info_t* module = &archetype_info.emitters[i];
				if (ImGui_CollapsingHeader(module->name, 0)) {
					show_module_params(module, &archetype_info);
					show_module_samplers(GRAIN_MODULE_EMITTER, i, module, &archetype_info);

					if (ImGui_Button("Remove")) {
						remove_index = i;
					}
				}

				ImGui_PopID();
			}

			if (remove_index >= 0) {
				barray_shift_remove(archetype_emitters, remove_index);
				should_rebuild_archetype = true;
			}

			if (map_size(emitters) > 0) {
				ImGui_SeparatorText("New emitter");
				show_module_list(emitters, "Type", &gui_emitter_index);

				if (ImGui_Button("Add")) {
					barray_push(archetype_emitters, emitters[gui_emitter_index]->ref.module, scene_allocator);

					should_rebuild_archetype = true;
				}
			}
		}
		ImGui_End();
	}

	if (show_affectors) {
		if (ImGui_Begin("Affectors", &show_affectors, ImGuiWindowFlags_AlwaysAutoResize)) {
			int remove_index = -1;
			int swap_index = -1;
			for (int i = 0; i < archetype_info.num_affectors; ++i) {
				ImGui_PushIDInt(i);

				const grain_module_info_t* module = &archetype_info.affectors[i];

				if (ImGui_CollapsingHeader(module->name, 0)) {
					show_module_params(module, &archetype_info);
					show_module_samplers(GRAIN_MODULE_AFFECTOR, i, module, &archetype_info);

					if (ImGui_Button("Remove")) {
						remove_index = i;
					}

					ImGui_SameLine();
					ImGui_BeginDisabled(i == 0);
					if (ImGui_ArrowButton("move_up", ImGuiDir_Up)) {
						swap_index = i - 1;
					}
					ImGui_EndDisabled();

					ImGui_SameLine();
					ImGui_BeginDisabled(i == archetype_info.num_affectors - 1);
					if (ImGui_ArrowButton("move_down", ImGuiDir_Down)) {
						swap_index = i;
					}
					ImGui_EndDisabled();
				}

				ImGui_PopID();
			}

			if (remove_index >= 0) {
				barray_shift_remove(archetype_affectors, remove_index);
				should_rebuild_archetype = true;
			}

			if (swap_index >= 0) {
				grain_affector_t* tmp = archetype_affectors[swap_index];
				archetype_affectors[swap_index] = archetype_affectors[swap_index + 1];
				archetype_affectors[swap_index + 1] = tmp;
				should_rebuild_archetype = true;
			}

			if (map_size(affectors) > 0) {
				ImGui_SeparatorText("New affector");
				show_module_list(affectors, "Type", &gui_affector_index);

				if (ImGui_Button("Add")) {
					barray_push(archetype_affectors, affectors[gui_affector_index]->ref.module, scene_allocator);

					should_rebuild_archetype = true;
				}
			}
		}
		ImGui_End();
	}

	if (show_renderer) {
		if (ImGui_Begin("Renderer", &show_renderer, ImGuiWindowFlags_AlwaysAutoResize)) {
			show_module_params(&archetype_info.renderer, &archetype_info);
			show_module_samplers(
				GRAIN_MODULE_RENDERER, 0, &archetype_info.renderer, &archetype_info
			);

			if (map_size(renderers) > 0) {
				if (show_module_list(renderers, "Type", &gui_renderer_index)) {
					archetype_renderer = renderers[gui_renderer_index]->ref.module;
					should_rebuild_archetype = true;
				}
			}
		}
		ImGui_End();
	}

	apply_texture_bindings(&archetype_info);

	grain_tick(particle_system, CF_DELTA_TIME);
	grain_end_update(grain);
// }}}

// Logging {{{
	if (show_log) {
		if (ImGui_Begin("Log", &show_log, 0)) {
			if (ImGui_Button("Clear")) {
				sclear(log_text);
				barray_clear(log_lines);
			}
			ImGui_SameLine();
			ImGui_Checkbox("Auto-scroll", &log_auto_scroll);
			ImGui_Separator();

			if (ImGui_BeginChild(
					"##scroll",
					(ImVec2){ 0.0f, 0.0f },
					0,
					ImGuiWindowFlags_HorizontalScrollbar
			)) {
				ImGuiListClipper clipper = { 0 };
				ImGuiListClipper_Begin(&clipper, (int)barray_len(log_lines), -1.0f);
				while (ImGuiListClipper_Step(&clipper)) {
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
						log_entry_t entry = log_lines[i];
						const char* line = log_text + entry.offset;
						ImGui_PushStyleColorImVec4(ImGuiCol_Text, log_level_color(entry.level));
						ImGui_TextUnformattedEx(line, line + entry.len);
						ImGui_PopStyleColor();
					}
				}

				if (log_auto_scroll && ImGui_GetScrollY() >= ImGui_GetScrollMaxY()) {
					ImGui_SetScrollHereY(1.0f);
				}
			}
			ImGui_EndChild();
		}
		ImGui_End();
	}
// }}}

// Modal {{{
	if (bco_status(modal_action) != BCO_TERMINATED) {
		bco_resume(modal_action);
	}

	if (should_begin_native_modal) {
		ImGui_OpenPopup("##NativeBlocker", 0);
		should_begin_native_modal = false;
	}

	if (ImGui_BeginPopupModal(
			"##NativeBlocker",
			NULL,
			ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoMove
	)) {
		ImGui_Text("Waiting");
		if (should_end_native_modal) {
			ImGui_CloseCurrentPopup();
		}

		ImGui_EndPopup();
	}

	if (should_popup_error) {
		ImGui_OpenPopup("Error", 0);
		should_popup_error = false;
	}

	if (ImGui_BeginPopupModal(
			"Error",
			NULL,
			ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoMove
	)) {
		ImGui_Text("%s", popup_error);
		if (ImGui_Button("OK")) {
			ImGui_CloseCurrentPopup();
		}

		ImGui_EndPopup();
	}

	if (should_popup_about) {
		if (attributions_text == NULL) {
			char* content = cf_fs_read_entire_file_to_memory_and_nul_terminate(
				"/assets/ATTRIBUTIONS.md", NULL
			);
			if (content != NULL) {
				sset(attributions_text, content);
				cf_free(content);
			} else {
				sset(attributions_text, "Could not load /assets/ATTRIBUTIONS.md");
			}
		}

		ImGui_OpenPopup("About", 0);
		should_popup_about = false;
	}

	if (ImGui_BeginPopupModal(
			"About",
			NULL,
			ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui_TextUnformatted("grain-editor");
		ImGui_SeparatorText("Attributions");
		ImGui_TextUnformatted(attributions_text);

		if (ImGui_Button("OK")) {
			ImGui_CloseCurrentPopup();
		}

		ImGui_EndPopup();
	}

	if (should_prompt_unsaved) {
		ImGui_OpenPopup("Unsaved changes", 0);
		should_prompt_unsaved = false;
		unsaved_prompt_active = true;
	}

	if (ImGui_BeginPopupModal(
			"Unsaved changes",
			NULL,
			ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui_Text("Save changes to %s?", system_name.data);

		if (ImGui_Button("Save")) {
			save_prompt_result = SAVE_PROMPT_SAVE;
			unsaved_prompt_active = false;
			ImGui_CloseCurrentPopup();
		}
		ImGui_SameLine();
		if (ImGui_Button("Don't save")) {
			save_prompt_result = SAVE_PROMPT_DONT_SAVE;
			unsaved_prompt_active = false;
			ImGui_CloseCurrentPopup();
		}
		ImGui_SameLine();
		if (ImGui_Button("Cancel")) {
			save_prompt_result = SAVE_PROMPT_CANCEL;
			unsaved_prompt_active = false;
			ImGui_CloseCurrentPopup();
		}

		ImGui_EndPopup();
	} else if (unsaved_prompt_active) {
		// Dismissed without a choice (e.g. Escape)
		save_prompt_result = SAVE_PROMPT_CANCEL;
		unsaved_prompt_active = false;
	}
// }}}

#ifndef __EMSCRIPTEN__
	if (bresmon_should_reload(bresmon, false) > 0) {
		bresmon_reload(bresmon);
		should_rebuild_archetype = true;
	}
#endif

	if (should_rebuild_archetype) {
		// Composition or module sources changed; the saved file is stale either way
		unsaved_changes = true;

		grain_archetype_t* new_archetype = grain_define_archetype(grain, "Editor", (grain_archetype_spec_t){
			.emitters = archetype_emitters,
			.num_emitters = barray_len(archetype_emitters),

			.affectors = archetype_affectors,
			.num_affectors = barray_len(archetype_affectors),

			.renderer = archetype_renderer,
		});

		if (new_archetype == NULL) {
			BLOG_ERROR("Could not rebuild archetype: %s", grain_get_last_error(grain));
			show_error(grain_get_last_error(grain));
		} else {
			archetype = new_archetype;
			BLOG_INFO(
				"Rebuilt archetype: %d emitter(s), %d affector(s), renderer %s",
				(int)barray_len(archetype_emitters),
				(int)barray_len(archetype_affectors),
				grain_get_renderer_name(archetype_renderer)
			);
		}
	}

	cf_apply_canvas(cf_app_get_canvas(), false);
	grain_begin_render(grain);
	grain_render(particle_system);
	grain_end_render(grain);

	debug_draw_end();

	cf_app_draw_onto_screen(false);
}

// }}}

SCENE {
	.init = init,
	.update = update,
	.cleanup = cleanup,

	.after_reload = after_reload,
};

#ifndef __EMSCRIPTEN__
#	define BRESMON_IMPLEMENTATION
#	define BRESMON_REALLOC bgame_realloc
#	include <bresmon.h>
#endif
