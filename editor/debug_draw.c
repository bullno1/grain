#include "debug_draw.h"
#include <cute.h>
#include <dcimgui.h>
#include <math.h>
#include <string.h>

#define MAX_GIZMOS 256
#define MAX_GIZMO_REFS 4
#define ARC_SEGMENT_ANGLE 0.0872665f  // 5 degrees
#define MAX_ARC_SEGMENTS 64

static const float HANDLE_RADIUS = 8.f;
static const float CROSSHAIR_SIZE = 12.f;
static const float ARROW_WIDTH = 6.f;
static const float DEFAULT_LENGTH = 64.f;
static const float GIZMO_THICKNESS = 1.f;

typedef enum {
	GIZMO_POSITION,
	GIZMO_RADIUS,
	GIZMO_ANGLE,
	GIZMO_ARC,
	GIZMO_VECTOR,
	GIZMO_EXTENT,
} gizmo_kind_t;

typedef struct {
	gizmo_kind_t kind;
	int param_index;
	CF_V2 at;
	// Kind-specific, resolved at registration:
	// RADIUS: [0]=radius
	// ANGLE:  [0]=angle, [1]=length
	// ARC:    [0]=from angle, [1]=to angle, [2]=inner radius, [3]=outer radius
	// VECTOR: [0]=dx, [1]=dy (scale applied)
	// EXTENT: [0]=width, [1]=height
	float values[4];
	// Params referenced through ident args, for hover emphasis
	int refs[MAX_GIZMO_REFS];
	int num_refs;
} gizmo_t;

// Per-frame lists and transient drag state; safe to reset on live reload
static gizmo_t gizmos[MAX_GIZMOS];
static int num_gizmos = 0;
static int hot_param = -1;
static int drag_param = -1;
static CF_V2 drag_offset;

typedef struct {
	grain_system_t* system;
	const grain_archetype_info_t* info;
	const grain_module_info_t* module;
} resolve_ctx_t;

static int
find_sibling_param(const resolve_ctx_t* ctx, const char* name, CF_ShaderInfoDataType type) {
	for (int i = 0; i < ctx->module->num_params; ++i) {
		int param_index = ctx->module->first_param + i;
		const grain_param_info_t* param = &ctx->info->params[param_index];
		if (param->type == type && strcmp(param->name, name) == 0) {
			return param_index;
		}
	}
	return -1;
}

static void
add_ref(gizmo_t* gizmo, int param_index) {
	if (gizmo->num_refs < MAX_GIZMO_REFS) {
		gizmo->refs[gizmo->num_refs++] = param_index;
	}
}

/**
 * Resolve an argument that is either a number literal or a reference to a
 * sibling float param.
 *
 * Returns false when the argument is present but cannot be resolved: the
 * caller should drop the whole gizmo. An absent argument succeeds with
 * *found = false so the caller can substitute a default.
 */
static bool
resolve_scalar(
	const resolve_ctx_t* ctx,
	const grain_param_decorator_t* decorator,
	int index,
	const char* name,
	gizmo_t* gizmo,
	float* out,
	bool* found
) {
	grain_decorator_arg_t arg;
	if (!grain_find_decorator_arg(decorator, index, name, &arg)) {
		*found = false;
		return true;
	}

	*found = true;
	switch (arg.type) {
		case GRAIN_DECORATOR_ARG_NUMBER:
			*out = arg.value.number;
			return true;
		case GRAIN_DECORATOR_ARG_IDENT: {
			int ref = find_sibling_param(ctx, arg.value.string, CF_SHADER_INFO_TYPE_FLOAT);
			if (ref < 0) { return false; }
			const float* value = grain_get_parameter(ctx->system, ref);
			if (value == NULL) { return false; }
			*out = *value;
			add_ref(gizmo, ref);
			return true;
		}
		default:
			return false;
	}
}

//! Like resolve_scalar but the argument must reference a vec2 param; absent = origin
static bool
resolve_anchor(
	const resolve_ctx_t* ctx,
	const grain_param_decorator_t* decorator,
	int index,
	gizmo_t* gizmo
) {
	grain_decorator_arg_t arg;
	if (!grain_find_decorator_arg(decorator, index, "at", &arg)) {
		gizmo->at = (CF_V2){ 0 };
		return true;
	}

	if (arg.type != GRAIN_DECORATOR_ARG_IDENT) { return false; }
	int ref = find_sibling_param(ctx, arg.value.string, CF_SHADER_INFO_TYPE_FLOAT2);
	if (ref < 0) { return false; }
	const float* value = grain_get_parameter(ctx->system, ref);
	if (value == NULL) { return false; }
	gizmo->at = (CF_V2){ value[0], value[1] };
	add_ref(gizmo, ref);
	return true;
}

static gizmo_t*
push_gizmo(gizmo_kind_t kind, int param_index) {
	if (num_gizmos >= MAX_GIZMOS) { return NULL; }
	gizmo_t* gizmo = &gizmos[num_gizmos++];
	*gizmo = (gizmo_t){
		.kind = kind,
		.param_index = param_index,
	};
	return gizmo;
}

static void
drop_gizmo(gizmo_t* gizmo) {
	if (gizmo == &gizmos[num_gizmos - 1]) { --num_gizmos; }
}

static void
register_position(const resolve_ctx_t* ctx, const grain_param_info_t* param, int param_index) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT2) { return; }
	float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	// Interaction happens here, during the UI pass, so the write is uploaded
	// by this frame's grain_end_update
	CF_V2 mouse = cf_screen_to_world((CF_V2){ cf_mouse_x(), cf_mouse_y() });
	if (drag_param == param_index) {
		if (cf_mouse_down(CF_MOUSE_BUTTON_LEFT)) {
			value[0] = mouse.x + drag_offset.x;
			value[1] = mouse.y + drag_offset.y;
			grain_parameter_modified(ctx->system, param_index);
		} else {
			drag_param = -1;
		}
	} else if (
		drag_param < 0
		&& cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT)
		&& !ImGui_GetIO()->WantCaptureMouse
	) {
		CF_V2 delta = { value[0] - mouse.x, value[1] - mouse.y };
		if (delta.x * delta.x + delta.y * delta.y <= HANDLE_RADIUS * HANDLE_RADIUS) {
			drag_param = param_index;
			drag_offset = delta;
		}
	}

	gizmo_t* gizmo = push_gizmo(GIZMO_POSITION, param_index);
	if (gizmo == NULL) { return; }
	gizmo->at = (CF_V2){ value[0], value[1] };
}

static void
register_radius(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_RADIUS, param_index);
	if (gizmo == NULL) { return; }
	if (!resolve_anchor(ctx, decorator, 0, gizmo)) {
		drop_gizmo(gizmo);
		return;
	}
	gizmo->values[0] = *value;
}

static void
register_angle(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_ANGLE, param_index);
	if (gizmo == NULL) { return; }

	bool found;
	float length = DEFAULT_LENGTH;
	if (
		!resolve_anchor(ctx, decorator, 0, gizmo)
		|| !resolve_scalar(ctx, decorator, 1, "length", gizmo, &length, &found)
	) {
		drop_gizmo(gizmo);
		return;
	}
	gizmo->values[0] = *value;
	gizmo->values[1] = length;
}

static void
register_arc(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_ARC, param_index);
	if (gizmo == NULL) { return; }

	bool found;
	float to;
	float inner = 0.f;
	float outer = DEFAULT_LENGTH;
	bool ok =
		resolve_anchor(ctx, decorator, 0, gizmo)
		&& resolve_scalar(ctx, decorator, 1, "to", gizmo, &to, &found)
		&& found  // `to` is required: an arc needs both ends
		&& resolve_scalar(ctx, decorator, 2, "inner", gizmo, &inner, &found)
		&& resolve_scalar(ctx, decorator, 3, "outer", gizmo, &outer, &found);
	if (!ok) {
		drop_gizmo(gizmo);
		return;
	}
	gizmo->values[0] = *value;
	gizmo->values[1] = to;
	gizmo->values[2] = inner;
	gizmo->values[3] = outer;
}

static void
register_vector(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	bool is_vec2 = param->type == CF_SHADER_INFO_TYPE_FLOAT2;
	if (!is_vec2 && param->type != CF_SHADER_INFO_TYPE_FLOAT) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_VECTOR, param_index);
	if (gizmo == NULL) { return; }

	bool found;
	float scale = 1.f;
	float angle = 0.f;
	bool angle_found = false;
	bool ok =
		resolve_anchor(ctx, decorator, 0, gizmo)
		&& resolve_scalar(ctx, decorator, 1, "scale", gizmo, &scale, &found)
		&& resolve_scalar(ctx, decorator, 2, "angle", gizmo, &angle, &angle_found)
		&& (is_vec2 || angle_found);  // a scalar magnitude needs a direction
	if (!ok) {
		drop_gizmo(gizmo);
		return;
	}
	if (is_vec2) {
		gizmo->values[0] = value[0] * scale;
		gizmo->values[1] = value[1] * scale;
	} else {
		gizmo->values[0] = cosf(angle) * value[0] * scale;
		gizmo->values[1] = sinf(angle) * value[0] * scale;
	}
}

static void
register_extent(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT2) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_EXTENT, param_index);
	if (gizmo == NULL) { return; }
	if (!resolve_anchor(ctx, decorator, 0, gizmo)) {
		drop_gizmo(gizmo);
		return;
	}
	gizmo->values[0] = value[0];
	gizmo->values[1] = value[1];
}

void
debug_draw_begin(void) {
	num_gizmos = 0;
	hot_param = -1;
}

void
debug_draw_param(
	grain_system_t* system,
	const grain_archetype_info_t* archetype_info,
	const grain_module_info_t* module,
	int param_index,
	bool highlight
) {
	if (highlight) { hot_param = param_index; }

	const grain_param_info_t* param = &archetype_info->params[param_index];
	resolve_ctx_t ctx = {
		.system = system,
		.info = archetype_info,
		.module = module,
	};

	const grain_param_decorator_t* decorator;
	if (grain_find_decorator(param, "position") != NULL) {
		register_position(&ctx, param, param_index);
	}
	if ((decorator = grain_find_decorator(param, "radius")) != NULL) {
		register_radius(&ctx, param, decorator, param_index);
	}
	if ((decorator = grain_find_decorator(param, "angle")) != NULL) {
		register_angle(&ctx, param, decorator, param_index);
	}
	if ((decorator = grain_find_decorator(param, "arc")) != NULL) {
		register_arc(&ctx, param, decorator, param_index);
	}
	if ((decorator = grain_find_decorator(param, "vector")) != NULL) {
		register_vector(&ctx, param, decorator, param_index);
	}
	if ((decorator = grain_find_decorator(param, "extent")) != NULL) {
		register_extent(&ctx, param, decorator, param_index);
	}
}

static CF_V2
polar(CF_V2 origin, float angle, float radius) {
	return (CF_V2){
		origin.x + cosf(angle) * radius,
		origin.y + sinf(angle) * radius,
	};
}

static void
draw_arc_gizmo(const gizmo_t* gizmo) {
	float from = gizmo->values[0];
	float to = gizmo->values[1];
	float inner = gizmo->values[2];
	float outer = gizmo->values[3];
	if (outer < inner) {
		float tmp = outer;
		outer = inner;
		inner = tmp;
	}

	int num_segments = (int)ceilf(fabsf(to - from) / ARC_SEGMENT_ANGLE);
	if (num_segments < 1) { num_segments = 1; }
	if (num_segments > MAX_ARC_SEGMENTS) { num_segments = MAX_ARC_SEGMENTS; }

	// Outer arc from → to, then back along the inner arc (or through the apex
	// when the sector starts at the anchor), closed into a loop
	CF_V2 points[2 * (MAX_ARC_SEGMENTS + 1) + 1];
	int num_points = 0;
	for (int i = 0; i <= num_segments; ++i) {
		float angle = from + (to - from) * ((float)i / (float)num_segments);
		points[num_points++] = polar(gizmo->at, angle, outer);
	}
	if (inner > 0.f) {
		for (int i = num_segments; i >= 0; --i) {
			float angle = from + (to - from) * ((float)i / (float)num_segments);
			points[num_points++] = polar(gizmo->at, angle, inner);
		}
	} else {
		points[num_points++] = gizmo->at;
	}

	cf_draw_polyline(points, num_points, GIZMO_THICKNESS, true);
}

static void
draw_gizmo(const gizmo_t* gizmo) {
	switch (gizmo->kind) {
		case GIZMO_POSITION: {
			CF_V2 at = gizmo->at;
			cf_draw_circle((CF_Circle){ .p = at, .r = HANDLE_RADIUS }, GIZMO_THICKNESS);
			cf_draw_line(
				(CF_V2){ at.x - CROSSHAIR_SIZE, at.y },
				(CF_V2){ at.x + CROSSHAIR_SIZE, at.y },
				GIZMO_THICKNESS
			);
			cf_draw_line(
				(CF_V2){ at.x, at.y - CROSSHAIR_SIZE },
				(CF_V2){ at.x, at.y + CROSSHAIR_SIZE },
				GIZMO_THICKNESS
			);
		} break;
		case GIZMO_RADIUS:
			cf_draw_circle(
				(CF_Circle){ .p = gizmo->at, .r = gizmo->values[0] },
				GIZMO_THICKNESS
			);
			break;
		case GIZMO_ANGLE:
			cf_draw_arrow(
				gizmo->at,
				polar(gizmo->at, gizmo->values[0], gizmo->values[1]),
				GIZMO_THICKNESS,
				ARROW_WIDTH
			);
			break;
		case GIZMO_ARC:
			draw_arc_gizmo(gizmo);
			break;
		case GIZMO_VECTOR: {
			CF_V2 delta = { gizmo->values[0], gizmo->values[1] };
			if (delta.x * delta.x + delta.y * delta.y < 1.f) { break; }
			cf_draw_arrow(
				gizmo->at,
				(CF_V2){ gizmo->at.x + delta.x, gizmo->at.y + delta.y },
				GIZMO_THICKNESS,
				ARROW_WIDTH
			);
		} break;
		case GIZMO_EXTENT: {
			float half_w = gizmo->values[0] * 0.5f;
			float half_h = gizmo->values[1] * 0.5f;
			CF_V2 corners[] = {
				{ gizmo->at.x - half_w, gizmo->at.y - half_h },
				{ gizmo->at.x + half_w, gizmo->at.y - half_h },
				{ gizmo->at.x + half_w, gizmo->at.y + half_h },
				{ gizmo->at.x - half_w, gizmo->at.y + half_h },
			};
			cf_draw_polyline(corners, 4, GIZMO_THICKNESS, true);
		} break;
	}
}

void
debug_draw_end(void) {
	for (int i = 0; i < num_gizmos; ++i) {
		const gizmo_t* gizmo = &gizmos[i];

		bool emphasized =
			gizmo->param_index == hot_param
			|| gizmo->param_index == drag_param;
		for (int j = 0; j < gizmo->num_refs; ++j) {
			emphasized = emphasized || gizmo->refs[j] == hot_param;
		}

		cf_draw_push_color(
			emphasized
				? cf_make_color_rgba_f(1.f, 0.6f, 0.1f, 1.f)
				: cf_make_color_rgba_f(1.f, 1.f, 1.f, 0.35f)
		);
		draw_gizmo(gizmo);
		cf_draw_pop_color();
	}
}
