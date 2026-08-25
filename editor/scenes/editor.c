#include <cute.h>
#include <blog.h>
#define BGAME_SCENE_NAME editor
#include <bgame/utils.h>
#include <bgame/allocator.h>
#include <grain.h>
#include <dcimgui.h>
#include <bco.h>
#define UFA_ARENA_TYPE barena_t
#include <ufa.h>

#define start_modal_action(FN, ...) \
	do { \
		if (bco_status(modal_action) == BCO_TERMINATED) { \
			bco_spawn(modal_action, FN, __VA_ARGS__); \
		} \
	} while (0)

typedef struct {
	char* source;
	char* path;
	void* module;
} module_extra_t;

typedef enum {
	GRAIN_MODULE_EMITTER,
	GRAIN_MODULE_AFFECTOR,
	GRAIN_MODULE_RENDERER,
} module_type_t;

SCENE_VAR(grain_t*, grain)
SCENE_VAR(bool, show_emitters)
SCENE_VAR(bool, show_affectors)
SCENE_VAR(bool, show_renderer)
SCENE_VAR(char*, source_buf)
SCENE_VAR(CK_MAP(module_extra_t), emitters)
SCENE_VAR(CK_MAP(module_extra_t), affectors)
SCENE_VAR(CK_MAP(module_extra_t), renderers)

static _Alignas(bco_align_t) char modal_action_storage[1024];
static bco_t* modal_action = (bco_t*)modal_action_storage;
static bool native_modal_guard = false;
static bool should_begin_native_modal = false;
static bool should_end_native_modal = true;
static const char* popup_error = NULL;

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

bco_static(load_module, module_type_t type) {
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
	});

	while (ufa_check_open_file(bco_var(open_file)) == UFA_PENDING) {
		bco_yield();
	}

	char read_buf[1024];
	sclear(source_buf);
	while (true) {
		size_t size = sizeof(read_buf);
		if (ufa_read_open_file(bco_var(open_file), read_buf, &size) != UFA_OK) {
			bco_return();
		}
		if (size == 0) { break; }
		sappend_range(source_buf, read_buf, read_buf + size);
	}

	const char* name = NULL;
	CK_MAP(module_extra_t)* module_map = NULL;
	void* module = NULL;
	const char* type_name = NULL;
	switch (bco_arg(type)) {
		case GRAIN_MODULE_EMITTER: {
			grain_emitter_t* emitter = grain_define_emitter(grain, source_buf);

			name = grain_get_emitter_name(emitter);
			module_map = &emitters;
			module = emitter;
			type_name = "emitter";
		} break;
		case GRAIN_MODULE_AFFECTOR: {
			grain_affector_t* affector = grain_define_affector(grain, source_buf);

			name = grain_get_affector_name(affector);
			module_map = &emitters;
			module = affector;
			type_name = "affector";
		} break;
		case GRAIN_MODULE_RENDERER: {
			grain_renderer_t* renderer = grain_define_renderer(grain, source_buf);

			name = grain_get_renderer_name(renderer);
			module_map = &renderers;
			module = renderer;
			type_name = "renderer";
		} break;
	}

	if (module == NULL) {
		const char* error = grain_get_last_error(grain);
		show_error(error);
		BLOG_ERROR("Error whilel loading %s: %s", ufa_get_open_file_name(bco_var(open_file)), error);
		bco_return();
	}

	module_extra_t* module_extra = map_get_ptr(*module_map, name);
	if (module_extra == NULL) {
		map_set(*module_map, name, ((module_extra_t){ 0 }));
		module_extra = map_get_ptr(*module_map, name);
	}
	module_extra->module = module;
	sset(module_extra->path, ufa_get_open_file_name(bco_var(open_file)));
	sset(module_extra->source, source_buf);

	BLOG_INFO("Loaded %s %s", type_name, name);

	bco_end

	ufa_end_open_file(bco_var(open_file));
	barena_reset(&bco_var(arena));
	end_native_modal();
}

static void
cleanup_module_map(CK_MAP(module_extra_t)* module_map) {
	for (int i = 0; i < map_size(*module_map); ++i) {
		sfree((*module_map)[i].path);
		sfree((*module_map)[i].source);
	}
	map_free(*module_map);
}

static void
init(void) {
	cf_clear_color(0.5f, 0.5f, 0.5f, 0.0f);

	if (bgame_current_scene_state() == BGAME_SCENE_INITIALIZING) {
		show_emitters = true;
		show_affectors = true;
		show_renderer = true;

		grain = grain_create();
	}
}

static void
cleanup(void) {
	grain_destroy(grain);

	cleanup_module_map(&emitters);
	cleanup_module_map(&affectors);
	cleanup_module_map(&renderers);
	sfree(source_buf);
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

	cf_app_draw_onto_screen(true);
}

SCENE {
	.init = init,
	.update = update,
	.cleanup = cleanup,
};
