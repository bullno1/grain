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
	grain_module_ref_t ref;
	char* source;

#ifndef __EMSCRIPTEN__
	bresmon_watch_t* watch;
#endif
} module_meta_t;

SCENE_VAR(grain_t*, grain)
SCENE_VAR(bool, show_emitters)
SCENE_VAR(bool, show_affectors)
SCENE_VAR(bool, show_renderer)
SCENE_VAR(char*, tmp_source_buf)

SCENE_VAR(char*, last_module_path)

SCENE_VAR(CK_MAP(module_meta_t*), emitters)
SCENE_VAR(CK_MAP(module_meta_t*), affectors)
SCENE_VAR(CK_MAP(module_meta_t*), renderers)

SCENE_VAR(char*, popup_error)

#ifndef __EMSCRIPTEN__
SCENE_VAR(bresmon_t*, bresmon)
#endif

static _Alignas(bco_align_t) char modal_action_storage[1024];
static bco_t* modal_action = (bco_t*)modal_action_storage;
static bool native_modal_guard = false;
static bool should_begin_native_modal = false;
static bool should_end_native_modal = true;

static bool should_popup_error = false;

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
		.filters = (ufa_filter_t[]){
			{ .name = "grain module", .pattern = "shd;glsl" },
			{ .name = "All files", .pattern = "*" },
		},
		.num_filters = 2,
		.directory = last_module_path,
	});

	while (ufa_check_open_file(bco_var(open_file)) == UFA_PENDING) {
		bco_yield();
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

	int slash_index;
	for (slash_index = (int)strlen(module_path); slash_index >= 0; --slash_index) {
		if (module_path[slash_index] == '/' || module_path[slash_index] == '\\') { break; }
	}
	if (slash_index > 0) {
		sclear(last_module_path);
		sappend_range(last_module_path, module_path, module_path + slash_index);
	}

	BLOG_INFO("Loaded %s %s", type_name, module_path);

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
	sfree(tmp_source_buf);
	sfree(popup_error);
	sfree(last_module_path);
}

static void
update(void) {
	cf_app_update(NULL);
	cf_clear_canvas(cf_app_get_canvas());

	ImGui_DockSpaceOverViewportEx(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

	if (ImGui_BeginMainMenuBar()) {
		if (ImGui_BeginMenu("File")) {
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

			ImGui_EndMenu();
		}
		ImGui_EndMainMenuBar();
	}

	if (show_emitters) {
		if (ImGui_Begin("Emitters", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
		}
		ImGui_End();
	}

	if (show_affectors) {
		if (ImGui_Begin("Affectors", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
		}
		ImGui_End();
	}

	if (show_renderer) {
		if (ImGui_Begin("Renderer", &show_emitters, ImGuiWindowFlags_AlwaysAutoResize)) {
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
