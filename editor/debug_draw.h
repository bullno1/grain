#ifndef GRAIN_EDITOR_DEBUG_DRAW_H
#define GRAIN_EDITOR_DEBUG_DRAW_H

#include <grain.h>

/**
 * World-space gizmos for params carrying debug decorators.
 *
 * Reference arguments name a sibling param in the same Params block; one that
 * does not resolve disables the gizmo silently. An anchor (`at`) may be a vec2
 * or vec3; a vec2 sits on the XY plane at z = 0.
 *
 * @position                     (vec2/vec3) crosshair + drag handle, writes back
 * @radius(at)                   (float) circle, or sphere in 3D
 * @angle(at, length)            (float) arrow ray, radians in the XY plane
 * @arc(at, to, inner, outer)    (float) wedge between this angle and `to`;
 *                               inner/outer radii make an annular sector
 * @vector(at, scale, angle)     (vec2/vec3) arrow of the value times `scale`,
 *                               or (float) magnitude along `angle` radians
 * @extent(at)                   (vec2/vec3) rectangle or box centered on `at`
 * @direction(at, length)        (vec3) arrow of the normalized value with a
 *                               drag handle at the tip, writes back
 * @cone(at, axis, inner, outer) (float) cone of this half-angle around the
 *                               vec3 param `axis`, the 3D form of @arc; the
 *                               silhouette ring drags the half-angle
 */

/**
 * Find the param `name` of `type` in the same Params block as `module`.
 * Returns its archetype-wide param index or -1.
 */
int
debug_draw_find_sibling_param(
	const grain_archetype_info_t* archetype_info,
	const grain_module_info_t* module,
	const char* name,
	CF_ShaderInfoDataType type
);

//! Reset the frame's gizmo list; call once per frame before any module UI
void
debug_draw_begin(grain_view_t view);

/**
 * Register the gizmos of one param and handle their interaction; call right
 * after its ImGui widget, before grain_end_update so writes upload this frame.
 * `highlight` emphasizes the gizmo (typically the widget is hovered or active).
 */
void
debug_draw_param(
	grain_system_t* system,
	const grain_archetype_info_t* archetype_info,
	const grain_module_info_t* module,
	int param_index,
	bool highlight
);

/**
 * Draw every registered gizmo; call after grain_end_render, with the draw3d
 * stacks still pushed in the 3D view.
 */
void
debug_draw_end(void);

/**
 * Outline a bounds box in the current view; empty bounds draw nothing.
 * Same call placement as debug_draw_end.
 */
void
debug_draw_bounds(grain_bounds_t bounds, CF_Color color);

#endif
