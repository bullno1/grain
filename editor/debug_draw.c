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
static const float SHADOW_DASH = 6.f;

typedef enum {
	GIZMO_POSITION,
	GIZMO_RADIUS,
	GIZMO_ANGLE,
	GIZMO_ARC,
	GIZMO_VECTOR,
	GIZMO_EXTENT,
	GIZMO_DIRECTION,
	GIZMO_CONE,
} gizmo_kind_t;

typedef struct {
	gizmo_kind_t kind;
	int param_index;
	// Anchor; a vec2 param sits on the XY plane at z = 0
	CF_V3 at;
	// Normalized axis of DIRECTION and CONE gizmos; zero when degenerate
	CF_V3 axis;
	// Kind-specific, resolved at registration:
	// RADIUS:    [0]=radius
	// ANGLE:     [0]=angle, [1]=length
	// ARC:       [0]=from angle, [1]=to angle, [2]=inner radius, [3]=outer radius
	// VECTOR:    [0]=dx, [1]=dy, [2]=dz (scale applied)
	// EXTENT:    [0]=width, [1]=height, [2]=depth (0 for a vec2)
	// DIRECTION: [0]=length
	// CONE:      [0]=half-angle, [1]=inner radius, [2]=outer radius
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
static grain_view_t current_view = GRAIN_VIEW_2D;

typedef struct {
	grain_system_t* system;
	const grain_archetype_info_t* info;
	const grain_module_info_t* module;
} resolve_ctx_t;

int
debug_draw_find_sibling_param(
	const grain_archetype_info_t* archetype_info,
	const grain_module_info_t* module,
	const char* name,
	CF_ShaderInfoDataType type
) {
	for (int i = 0; i < module->num_params; ++i) {
		int param_index = module->first_param + i;
		const grain_param_info_t* param = &archetype_info->params[param_index];
		if (param->type == type && strcmp(param->name, name) == 0) {
			return param_index;
		}
	}
	return -1;
}

static int
find_sibling_param(const resolve_ctx_t* ctx, const char* name, CF_ShaderInfoDataType type) {
	return debug_draw_find_sibling_param(ctx->info, ctx->module, name, type);
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

/**
 * Resolve an argument that must reference a sibling vec3 or vec2 param.
 *
 * A vec2 is lifted onto the XY plane at z = 0. Returns false when the
 * argument is present but cannot be resolved; an absent argument succeeds
 * with *found = false.
 */
static bool
resolve_point(
	const resolve_ctx_t* ctx,
	const grain_param_decorator_t* decorator,
	int index,
	const char* name,
	gizmo_t* gizmo,
	CF_V3* out,
	bool* found
) {
	grain_decorator_arg_t arg;
	if (!grain_find_decorator_arg(decorator, index, name, &arg)) {
		*found = false;
		return true;
	}

	*found = true;
	if (arg.type != GRAIN_DECORATOR_ARG_IDENT) { return false; }
	int num_components = 3;
	int ref = find_sibling_param(ctx, arg.value.string, CF_SHADER_INFO_TYPE_FLOAT3);
	if (ref < 0) {
		num_components = 2;
		ref = find_sibling_param(ctx, arg.value.string, CF_SHADER_INFO_TYPE_FLOAT2);
	}
	if (ref < 0) { return false; }
	const float* value = grain_get_parameter(ctx->system, ref);
	if (value == NULL) { return false; }
	*out = cf_v3(value[0], value[1], num_components == 3 ? value[2] : 0.f);
	add_ref(gizmo, ref);
	return true;
}

//! The `at` anchor; absent = origin
static bool
resolve_anchor(
	const resolve_ctx_t* ctx,
	const grain_param_decorator_t* decorator,
	int index,
	gizmo_t* gizmo
) {
	bool found;
	gizmo->at = cf_v3(0.f, 0.f, 0.f);
	return resolve_point(ctx, decorator, index, "at", gizmo, &gizmo->at, &found);
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
	bool is_vec3 = param->type == CF_SHADER_INFO_TYPE_FLOAT3;
	if (!is_vec3 && param->type != CF_SHADER_INFO_TYPE_FLOAT2) { return; }
	float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	// Interaction happens here, during the UI pass, so the write is uploaded
	// by this frame's grain_end_update. Only the 2D view drags: the mouse
	// maps to a plane there, while a 3D handle has no such plane yet.
	if (current_view == GRAIN_VIEW_2D && !is_vec3) {
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
	}

	gizmo_t* gizmo = push_gizmo(GIZMO_POSITION, param_index);
	if (gizmo == NULL) { return; }
	gizmo->at = cf_v3(value[0], value[1], is_vec3 ? value[2] : 0.f);
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
	int num_components;
	switch (param->type) {
		case CF_SHADER_INFO_TYPE_FLOAT:  num_components = 1; break;
		case CF_SHADER_INFO_TYPE_FLOAT2: num_components = 2; break;
		case CF_SHADER_INFO_TYPE_FLOAT3: num_components = 3; break;
		default: return;
	}
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
		&& (num_components > 1 || angle_found);  // a scalar magnitude needs a direction
	if (!ok) {
		drop_gizmo(gizmo);
		return;
	}
	if (num_components == 1) {
		gizmo->values[0] = cosf(angle) * value[0] * scale;
		gizmo->values[1] = sinf(angle) * value[0] * scale;
		gizmo->values[2] = 0.f;
	} else {
		gizmo->values[0] = value[0] * scale;
		gizmo->values[1] = value[1] * scale;
		gizmo->values[2] = num_components == 3 ? value[2] * scale : 0.f;
	}
}

static void
register_extent(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	bool is_vec3 = param->type == CF_SHADER_INFO_TYPE_FLOAT3;
	if (!is_vec3 && param->type != CF_SHADER_INFO_TYPE_FLOAT2) { return; }
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
	gizmo->values[2] = is_vec3 ? value[2] : 0.f;
}

static void
register_direction(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT3) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_DIRECTION, param_index);
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
	gizmo->axis = cf_safe_norm_v3(cf_v3(value[0], value[1], value[2]));
	gizmo->values[0] = length;
}

static void
register_cone(
	const resolve_ctx_t* ctx,
	const grain_param_info_t* param,
	const grain_param_decorator_t* decorator,
	int param_index
) {
	if (param->type != CF_SHADER_INFO_TYPE_FLOAT) { return; }
	const float* value = grain_get_parameter(ctx->system, param_index);
	if (value == NULL) { return; }

	gizmo_t* gizmo = push_gizmo(GIZMO_CONE, param_index);
	if (gizmo == NULL) { return; }

	bool found;
	CF_V3 axis;
	float inner = 0.f;
	float outer = DEFAULT_LENGTH;
	bool ok =
		resolve_anchor(ctx, decorator, 0, gizmo)
		&& resolve_point(ctx, decorator, 1, "axis", gizmo, &axis, &found)
		&& found  // `axis` is required: a cone needs an orientation
		&& resolve_scalar(ctx, decorator, 2, "inner", gizmo, &inner, &found)
		&& resolve_scalar(ctx, decorator, 3, "outer", gizmo, &outer, &found);
	if (!ok) {
		drop_gizmo(gizmo);
		return;
	}
	// Same fallback as a module normalizing a zero axis would sensibly pick
	gizmo->axis = cf_safe_norm_v3(axis);
	if (cf_dot_v3(gizmo->axis, gizmo->axis) == 0.f) { gizmo->axis = cf_v3(0.f, 1.f, 0.f); }
	gizmo->values[0] = *value;
	gizmo->values[1] = inner;
	gizmo->values[2] = outer;
}

void
debug_draw_begin(grain_view_t view) {
	num_gizmos = 0;
	hot_param = -1;
	current_view = view;
	if (view != GRAIN_VIEW_2D) { drag_param = -1; }
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
	if ((decorator = grain_find_decorator(param, "direction")) != NULL) {
		register_direction(&ctx, param, decorator, param_index);
	}
	if ((decorator = grain_find_decorator(param, "cone")) != NULL) {
		register_cone(&ctx, param, decorator, param_index);
	}
}

// Shared geometry {{{

static CF_V2
polar(CF_V2 origin, float angle, float radius) {
	return (CF_V2){
		origin.x + cosf(angle) * radius,
		origin.y + sinf(angle) * radius,
	};
}

/**
 * Outline of an annular sector in the XY plane: the outer arc from -> to,
 * then back along the inner arc (or through the apex when the sector starts
 * at the anchor), closed into a loop. Returns the point count.
 */
static int
sector_outline(CF_V2 at, float from, float to, float inner, float outer, CF_V2* points) {
	if (outer < inner) {
		float tmp = outer;
		outer = inner;
		inner = tmp;
	}

	int num_segments = (int)ceilf(fabsf(to - from) / ARC_SEGMENT_ANGLE);
	if (num_segments < 1) { num_segments = 1; }
	if (num_segments > MAX_ARC_SEGMENTS) { num_segments = MAX_ARC_SEGMENTS; }

	int num_points = 0;
	for (int i = 0; i <= num_segments; ++i) {
		float angle = from + (to - from) * ((float)i / (float)num_segments);
		points[num_points++] = polar(at, angle, outer);
	}
	if (inner > 0.f) {
		for (int i = num_segments; i >= 0; --i) {
			float angle = from + (to - from) * ((float)i / (float)num_segments);
			points[num_points++] = polar(at, angle, inner);
		}
	} else {
		points[num_points++] = at;
	}
	return num_points;
}

#define MAX_SECTOR_POINTS (2 * (MAX_ARC_SEGMENTS + 1) + 1)

//! Orthonormal tangent and bitangent around a unit axis
static void
axis_basis(CF_V3 axis, CF_V3* tangent, CF_V3* bitangent) {
	CF_V3 helper = fabsf(axis.y) < 0.99f ? cf_v3(0.f, 1.f, 0.f) : cf_v3(1.f, 0.f, 0.f);
	*tangent = cf_norm_v3(cf_cross_v3(helper, axis));
	*bitangent = cf_cross_v3(axis, *tangent);
}

// }}}

// 2D view {{{

static void
draw_gizmo_2d(const gizmo_t* gizmo) {
	CF_V2 at = { gizmo->at.x, gizmo->at.y };
	switch (gizmo->kind) {
		case GIZMO_POSITION: {
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
			cf_draw_circle((CF_Circle){ .p = at, .r = gizmo->values[0] }, GIZMO_THICKNESS);
			break;
		case GIZMO_ANGLE:
			cf_draw_arrow(
				at,
				polar(at, gizmo->values[0], gizmo->values[1]),
				GIZMO_THICKNESS,
				ARROW_WIDTH
			);
			break;
		case GIZMO_ARC: {
			CF_V2 points[MAX_SECTOR_POINTS];
			int num_points = sector_outline(
				at, gizmo->values[0], gizmo->values[1], gizmo->values[2], gizmo->values[3], points
			);
			cf_draw_polyline(points, num_points, GIZMO_THICKNESS, true);
		} break;
		case GIZMO_VECTOR: {
			CF_V2 delta = { gizmo->values[0], gizmo->values[1] };
			if (delta.x * delta.x + delta.y * delta.y < 1.f) { break; }
			cf_draw_arrow(
				at,
				(CF_V2){ at.x + delta.x, at.y + delta.y },
				GIZMO_THICKNESS,
				ARROW_WIDTH
			);
		} break;
		case GIZMO_EXTENT: {
			float half_w = gizmo->values[0] * 0.5f;
			float half_h = gizmo->values[1] * 0.5f;
			CF_V2 corners[] = {
				{ at.x - half_w, at.y - half_h },
				{ at.x + half_w, at.y - half_h },
				{ at.x + half_w, at.y + half_h },
				{ at.x - half_w, at.y + half_h },
			};
			cf_draw_polyline(corners, 4, GIZMO_THICKNESS, true);
		} break;
		case GIZMO_DIRECTION: {
			// The XY projection; an axis along z has nothing to show here
			CF_V2 dir = { gizmo->axis.x, gizmo->axis.y };
			if (dir.x * dir.x + dir.y * dir.y < 1e-6f) { break; }
			cf_draw_arrow(
				at,
				(CF_V2){ at.x + dir.x * gizmo->values[0], at.y + dir.y * gizmo->values[0] },
				GIZMO_THICKNESS,
				ARROW_WIDTH
			);
		} break;
		case GIZMO_CONE: {
			// The sector the cone cuts out of the XY plane
			float mid = atan2f(gizmo->axis.y, gizmo->axis.x);
			CF_V2 points[MAX_SECTOR_POINTS];
			int num_points = sector_outline(
				at,
				mid - gizmo->values[0], mid + gizmo->values[0],
				gizmo->values[1], gizmo->values[2],
				points
			);
			cf_draw_polyline(points, num_points, GIZMO_THICKNESS, true);
		} break;
	}
}

// }}}

// 3D view {{{

//! A pixel-sized handle: drawn in 2D at the projected position
static void
draw3d_handle(CF_V3 at) {
	CF_V3 p = cf_draw3d_project(at);
	if (p.z < 0.f) { return; }
	cf_draw_circle((CF_Circle){ .p = { p.x, p.y }, .r = HANDLE_RADIUS }, GIZMO_THICKNESS);
}

//! Dashed projection of a segment onto the ground plane plus a drop line
//! from its far end: the depth cue a single viewpoint lacks
static void
draw3d_shadow(CF_V3 from, CF_V3 to) {
	CF_V3 shadow_from = cf_v3(from.x, 0.f, from.z);
	CF_V3 shadow_to = cf_v3(to.x, 0.f, to.z);
	cf_draw3d_push_dash(SHADOW_DASH, SHADOW_DASH, 0.f);
	cf_draw3d_line(shadow_from, shadow_to, GIZMO_THICKNESS);
	cf_draw3d_line(to, shadow_to, GIZMO_THICKNESS);
	cf_draw3d_pop_dash();
}

static void
draw3d_arrow(CF_V3 from, CF_V3 to) {
	cf_draw3d_arrow(from, to, GIZMO_THICKNESS, ARROW_WIDTH);
	draw3d_shadow(from, to);
}

//! Points of an XY-plane sector lifted to the anchor's depth
static void
draw3d_sector(CF_V3 at, float from, float to, float inner, float outer) {
	CF_V2 points[MAX_SECTOR_POINTS];
	int num_points = sector_outline((CF_V2){ at.x, at.y }, from, to, inner, outer, points);
	CF_V3 points3d[MAX_SECTOR_POINTS];
	for (int i = 0; i < num_points; ++i) {
		points3d[i] = cf_v3(points[i].x, points[i].y, at.z);
	}
	cf_draw3d_polyline(points3d, num_points, GIZMO_THICKNESS, true);
}

static void
draw3d_cone(const gizmo_t* gizmo) {
	float half_angle = gizmo->values[0];
	float inner = gizmo->values[1];
	float outer = gizmo->values[2];
	if (outer < inner) {
		float tmp = outer;
		outer = inner;
		inner = tmp;
	}

	CF_V3 at = gizmo->at;
	CF_V3 axis = gizmo->axis;
	float s = sinf(half_angle);
	float c = cosf(half_angle);

	// Caps: circles where the sector meets the inner and outer spheres
	cf_draw3d_circle(cf_add_v3(at, cf_mul_v3_f(axis, outer * c)), axis, outer * s, GIZMO_THICKNESS);
	if (inner > 0.f) {
		cf_draw3d_circle(cf_add_v3(at, cf_mul_v3_f(axis, inner * c)), axis, inner * s, GIZMO_THICKNESS);
	}

	// Four generatrices, at quarter turns around the axis
	CF_V3 tangent, bitangent;
	axis_basis(axis, &tangent, &bitangent);
	for (int i = 0; i < 4; ++i) {
		float turn = (float)i * (CF_PI * 0.5f);
		CF_V3 rim = cf_add_v3(cf_mul_v3_f(tangent, cosf(turn)), cf_mul_v3_f(bitangent, sinf(turn)));
		CF_V3 dir = cf_add_v3(cf_mul_v3_f(rim, s), cf_mul_v3_f(axis, c));
		CF_V3 from = inner > 0.f ? cf_add_v3(at, cf_mul_v3_f(dir, inner)) : at;
		CF_V3 to = cf_add_v3(at, cf_mul_v3_f(dir, outer));
		cf_draw3d_line(from, to, GIZMO_THICKNESS);
	}
}

static void
draw_gizmo_3d(const gizmo_t* gizmo) {
	CF_V3 at = gizmo->at;
	switch (gizmo->kind) {
		case GIZMO_POSITION: {
			cf_draw3d_line(
				cf_v3(at.x - CROSSHAIR_SIZE, at.y, at.z),
				cf_v3(at.x + CROSSHAIR_SIZE, at.y, at.z),
				GIZMO_THICKNESS
			);
			cf_draw3d_line(
				cf_v3(at.x, at.y - CROSSHAIR_SIZE, at.z),
				cf_v3(at.x, at.y + CROSSHAIR_SIZE, at.z),
				GIZMO_THICKNESS
			);
			cf_draw3d_line(
				cf_v3(at.x, at.y, at.z - CROSSHAIR_SIZE),
				cf_v3(at.x, at.y, at.z + CROSSHAIR_SIZE),
				GIZMO_THICKNESS
			);
			draw3d_handle(at);
		} break;
		case GIZMO_RADIUS: {
			// Three great circles read as a sphere from any angle
			float radius = gizmo->values[0];
			cf_draw3d_circle(at, cf_v3(1.f, 0.f, 0.f), radius, GIZMO_THICKNESS);
			cf_draw3d_circle(at, cf_v3(0.f, 1.f, 0.f), radius, GIZMO_THICKNESS);
			cf_draw3d_circle(at, cf_v3(0.f, 0.f, 1.f), radius, GIZMO_THICKNESS);
		} break;
		case GIZMO_ANGLE: {
			CF_V2 tip = polar((CF_V2){ at.x, at.y }, gizmo->values[0], gizmo->values[1]);
			draw3d_arrow(at, cf_v3(tip.x, tip.y, at.z));
		} break;
		case GIZMO_ARC:
			draw3d_sector(at, gizmo->values[0], gizmo->values[1], gizmo->values[2], gizmo->values[3]);
			break;
		case GIZMO_VECTOR: {
			CF_V3 delta = cf_v3(gizmo->values[0], gizmo->values[1], gizmo->values[2]);
			if (cf_dot_v3(delta, delta) < 1.f) { break; }
			draw3d_arrow(at, cf_add_v3(at, delta));
		} break;
		case GIZMO_EXTENT:
			cf_draw3d_box_wire(
				at,
				cf_v3(gizmo->values[0] * 0.5f, gizmo->values[1] * 0.5f, gizmo->values[2] * 0.5f),
				GIZMO_THICKNESS
			);
			break;
		case GIZMO_DIRECTION: {
			if (cf_dot_v3(gizmo->axis, gizmo->axis) == 0.f) { break; }
			CF_V3 tip = cf_add_v3(at, cf_mul_v3_f(gizmo->axis, gizmo->values[0]));
			draw3d_arrow(at, tip);
			draw3d_handle(tip);
		} break;
		case GIZMO_CONE:
			draw3d_cone(gizmo);
			break;
	}
}

// }}}

void
debug_draw_end(void) {
	bool is_3d = current_view == GRAIN_VIEW_3D;
	if (is_3d) {
		// Constant on-screen stroke width, like the 2D view gets for free
		cf_draw3d_push_stroke_pixels(true);
	}

	for (int i = 0; i < num_gizmos; ++i) {
		const gizmo_t* gizmo = &gizmos[i];

		bool emphasized =
			gizmo->param_index == hot_param
			|| gizmo->param_index == drag_param;
		for (int j = 0; j < gizmo->num_refs; ++j) {
			emphasized = emphasized || gizmo->refs[j] == hot_param;
		}

		CF_Color color = emphasized
			? cf_make_color_rgba_f(1.f, 0.6f, 0.1f, 1.f)
			: cf_make_color_rgba_f(1.f, 1.f, 1.f, 0.35f);
		// Both stacks: the 3D view still draws its handles through the 2D API
		cf_draw_push_color(color);
		cf_draw3d_push_color(color);
		if (is_3d) {
			draw_gizmo_3d(gizmo);
		} else {
			draw_gizmo_2d(gizmo);
		}
		cf_draw3d_pop_color();
		cf_draw_pop_color();
	}

	if (is_3d) {
		cf_draw3d_pop_stroke_pixels();
	}
}
