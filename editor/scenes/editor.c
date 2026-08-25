#define _GNU_SOURCE
#include <cute.h>
#include <blog.h>
#define BGAME_SCENE_NAME editor
#include <bgame/utils.h>
#include <bgame/allocator.h>
#include <grain.h>
#include <dcimgui.h>
#include <bco.h>

#ifndef __EMSCRIPTEN__
#	define BRESMON_API static
#	include <bresmon.h>
#endif

#define UFA_ARENA_TYPE barena_t
#include <ufa.h>

#define start_modal_action(FN, ...) \
	do { \
		if (bco_status(modal_action) == BCO_TERMINATED) { \
			bco_spawn(modal_action, FN, __VA_ARGS__); \
		} \
	} while (0)

typedef struct {
	grain_module_kind_t type;
	void* module;
	char* source;

#ifndef __EMSCRIPTEN__
	bresmon_watch_t* watch;
#endif
} module_meta_t;

SCENE_VAR(grain_t*, grain)
SCENE_VAR(bool, show_emitters)
SCENE_VAR(bool, show_affectors)
SCENE_VAR(bool, show_renderer)
SCENE_VAR(char*, source_buf)

SCENE_VAR(CK_MAP(module_meta_t*), emitters)
SCENE_VAR(char*, last_emitter_path)

SCENE_VAR(CK_MAP(module_meta_t*), affectors)
SCENE_VAR(char*, last_affector_path)

SCENE_VAR(CK_MAP(module_meta_t*), renderers)
SCENE_VAR(char*, last_renderer_path)

#ifndef __EMSCRIPTEN__
SCENE_VAR(bresmon_t*, bresmon)
#endif

static _Alignas(bco_align_t) char modal_action_storage[1024];
static bco_t* modal_action = (bco_t*)modal_action_storage;
static bool native_modal_guard = false;
static bool should_begin_native_modal = false;
static bool should_end_native_modal = true;
static const char* popup_error = NULL;

static bool
load_module_from_file(
	ufa_open_file_t* open_file,
	grain_module_kind_t module_type,
	void** module_out,
	char** source_out
) {
	char read_buf[1024];
	sclear(source_buf);
	while (true) {
		size_t size = sizeof(read_buf);
		if (ufa_read_open_file(open_file, read_buf, &size) != UFA_OK) {
			return false;
		}
		if (size == 0) { break; }
		sappend_range(source_buf, read_buf, read_buf + size);
	}

	void* module = NULL;
	const char* path = ufa_get_open_file_name(open_file);
	char** last_path = NULL;

	switch (module_type) {
		case GRAIN_MODULE_EMITTER: {
			module = grain_define_emitter(grain, source_buf);
			last_path = &last_emitter_path;
		} break;
		case GRAIN_MODULE_AFFECTOR: {
			module = grain_define_affector(grain, source_buf);
			last_path = &last_affector_path;
		} break;
		case GRAIN_MODULE_RENDERER: {
			module = grain_define_renderer(grain, source_buf);
			last_path = &last_renderer_path;
		} break;
		default: return false;
	}

	if (module != NULL) {
		*module_out = module;
		sset(*source_out, source_buf);

		int slash_index;
		for (slash_index = (int)strlen(path); slash_index >= 0; --slash_index) {
			if (path[slash_index] == '/' || path[slash_index] == '\\') { break; }
		}
		if (slash_index > 0) {
			sclear(*last_path);
			sappend_range(*last_path, path, path + slash_index);
		}

		return true;
	} else {
		return false;
	}
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

	if (load_module_from_file(file, module_meta->type, &module_meta->module, &module_meta->source)) {
		BLOG_INFO("Reloaded %s", path);
	} else {
		BLOG_ERROR("Error while reloading %s: %s", path, grain_get_last_error(grain));
	}

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
	popup_error = error;
}

bco_static(load_module, grain_module_kind_t type) {
	bco_vars(
		barena_t arena;
		ufa_open_file_t* open_file;
	)

	bco_begin
	begin_native_modal();

	char* last_path = NULL;
	switch (bco_arg(type)) {
		case GRAIN_MODULE_EMITTER: {
			last_path = last_emitter_path;
		} break;
		case GRAIN_MODULE_AFFECTOR: {
			last_path = last_affector_path;
		} break;
		case GRAIN_MODULE_RENDERER: {
			last_path = last_renderer_path;
		} break;
		default: break;
	}

	barena_init(&bco_var(arena), bgame_arena_pool);
	bco_var(open_file) = ufa_begin_open_file((ufa_config_t){
		.arena = &bco_var(arena),
		.memalign = barena_memalign,
		.parent_window = cf_app_get_window(),
		.filters = (ufa_filter_t[]){
			{ .name = "grain module", .pattern = "shd;glsl" },
			{ .name = "All files", .pattern = "*" },
		},
		.num_filters = 2,
		.directory = last_path,
	});

	while (ufa_check_open_file(bco_var(open_file)) == UFA_PENDING) {
		bco_yield();
	}

	void* module = NULL;
	char* source = NULL;
	if (!load_module_from_file(bco_var(open_file), bco_arg(type), &module, &source)) {
		const char* error = grain_get_last_error(grain);
		show_error(error);
		BLOG_ERROR("Error while loading %s: %s", ufa_get_open_file_name(bco_var(open_file)), error);
		bco_return();
	}

	const char* name = NULL;
	CK_MAP(module_meta_t*)* module_map = NULL;
	const char* type_name = NULL;
	switch (bco_arg(type)) {
		case GRAIN_MODULE_EMITTER: {
			name = grain_get_emitter_name(module);
			module_map = &emitters;
			type_name = "emitter";
		} break;
		case GRAIN_MODULE_AFFECTOR: {
			name = grain_get_affector_name(module);
			module_map = &affectors;
			type_name = "affector";
		} break;
		case GRAIN_MODULE_RENDERER: {
			name = grain_get_renderer_name(module);
			module_map = &renderers;
			type_name = "renderer";
		} break;
		default: bco_return();
	}

	module_meta_t* module_meta = map_get(*module_map, name);
	if (module_meta == NULL) {
		module_meta = bgame_malloc(sizeof(module_meta_t), scene_allocator);
		map_set(*module_map, name, module_meta);
		*module_meta = (module_meta_t){
			.type = bco_arg(type),
			.module = module,
		};

#ifndef __EMSCRIPTEN__
		watch_module(ufa_get_open_file_name(bco_var(open_file)), module_meta);
#endif
	}
	if (module_meta->source != NULL) {
		sfree(module_meta->source);
	}
	module_meta->source = source;

	BLOG_INFO("Loaded %s %s", type_name, name);

	bco_end

	ufa_end_open_file(bco_var(open_file));
	barena_reset(&bco_var(arena));
	end_native_modal();
}

static void
cleanup_module_map(CK_MAP(module_meta_t*)* module_map) {
	for (int i = 0; i < map_size(*module_map); ++i) {
		sfree((*module_map)[i]->source);
		bgame_free((*module_map)[i], scene_allocator);
	}
	map_free(*module_map);
}

static void
after_reload(void) {
#ifndef __EMSCRIPTEN__
	reinit_watch(emitters);
	reinit_watch(affectors);
	reinit_watch(renderers);
#endif
}

static void
init(void) {
	cf_clear_color(0.5f, 0.5f, 0.5f, 0.0f);

	if (bgame_current_scene_state() == BGAME_SCENE_INITIALIZING) {
		show_emitters = true;
		show_affectors = true;
		show_renderer = true;

		grain = grain_create();

#ifndef __EMSCRIPTEN__
		bresmon = bresmon_create(scene_allocator);
#endif
	}
}

static void
cleanup(void) {
#ifndef __EMSCRIPTEN__
	bresmon_destroy(bresmon);
#endif
	grain_destroy(grain);

	cleanup_module_map(&emitters);
	cleanup_module_map(&affectors);
	cleanup_module_map(&renderers);
	sfree(source_buf);
	sfree(last_emitter_path);
	sfree(last_affector_path);
	sfree(last_renderer_path);
}

static void
update(void) {
	cf_app_update(NULL);
	cf_clear_canvas(cf_app_get_canvas());

	ImGui_DockSpaceOverViewportEx(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

	if (ImGui_BeginMainMenuBar()) {
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

			ImGui_EndMenu();
		}
		ImGui_EndMainMenuBar();
	}

	if (show_emitters) {
		if (ImGui_Begin("Emitters", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui_Button("Load")) {
				start_modal_action(load_module, GRAIN_MODULE_EMITTER);
			}
		}
		ImGui_End();
	}

	if (show_affectors) {
		if (ImGui_Begin("Affectors", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui_Button("Load")) {
				start_modal_action(load_module, GRAIN_MODULE_AFFECTOR);
			}
		}
		ImGui_End();
	}

	if (show_renderer) {
		if (ImGui_Begin("Renderer", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui_Button("Load")) {
				start_modal_action(load_module, GRAIN_MODULE_RENDERER);
			}
		}
		ImGui_End();
	}

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

#ifndef __EMSCRIPTEN__
	bresmon_check(bresmon, false);
#endif

	cf_app_draw_onto_screen(true);
}

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
