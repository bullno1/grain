#ifndef GRAIN_EDITOR_DEBUG_DRAW_H
#define GRAIN_EDITOR_DEBUG_DRAW_H

#include <grain.h>

/**
 * World-space gizmos for params carrying debug decorators.
 *
 * The vocabulary is editor-assigned; the library treats decorators as opaque.
 * Reference arguments are bare identifiers naming a sibling param in the same
 * Params block; a reference that does not resolve disables the gizmo silently.
 * An anchor (`at`) may name a vec2 or a vec3 param; a vec2 sits on the XY
 * plane at z = 0, which is how the 3D view shows 2D gizmos.
 *
 * @position            (vec2/vec3) crosshair + handle, writes back. In the
 *                                  3D view a vec3 also gets translate stems:
 *                                  the center handle drags on the
 *                                  camera-facing plane, a stem along its
 *                                  axis; a vec2 drags on the XY plane
 * @radius(at)          (float) circle around the anchor; a sphere in 3D
 * @angle(at, length)   (float) arrow ray from the anchor, angle in radians,
 *                              in the XY plane
 * @arc(at, to, inner, outer)
 *                      (float) wedge between this angle and `to`'s value in
 *                              the XY plane; inner/outer radii make it an
 *                              annular sector
 * @vector(at, scale, angle)
 *                      (vec2/vec3) arrow of the value scaled by `scale`, or
 *                      (float) magnitude along `angle` radians
 * @extent(at)          (vec2/vec3) rectangle or box of that size centered
 *                                  on the anchor
 * @direction(at, length)
 *                      (vec3)  arrow of the normalized value, `length` long;
 *                              its tip handle drags on the sphere of that
 *                              radius and writes back, keeping the magnitude
 * @cone(at, axis, inner, outer)
 *                      (float) cone of this half-angle around the vec3 param
 *                              `axis` names; inner/outer radii make it a
 *                              spherical sector, the 3D form of @arc. The
 *                              ring handle on the silhouette drags the
 *                              half-angle and writes back
 *
 * In the 3D view, arrows cast a dashed shadow on the y = 0 ground plane with
 * a drop line from the tip, the depth cue a single viewpoint lacks. Every 3D
 * drag is one or two degrees of freedom on a plane or sphere fixed at grab,
 * so a drag never has to guess depth. Picking needs the draw3d camera stacks
 * pushed before debug_draw_param runs.
 */

/**
 * Find the param named `name` with type `type` in the same Params block as
 * `module`. Returns its archetype-wide param index or -1.
 *
 * This is the resolution rule for every identifier decorator argument, and is
 * shared with the param widgets (e.g. `@range(max = other_param)`).
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
 * Register the gizmos of one param; call right after its ImGui widget.
 *
 * Also handles gizmo interaction (e.g. dragging a @position handle): it must
 * run before grain_end_update so writes are uploaded in the same frame.
 * highlight marks the param as the one being inspected (typically the widget
 * is hovered or active) and drives gizmo emphasis.
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
 * Draw every registered gizmo; call after grain_end_render.
 *
 * In the 3D view the draw3d projection and view stacks must still be pushed.
 */
void
debug_draw_end(void);

/**
 * Outline a bounds box in the current view: a dashed rectangle of its XY
 * extent in 2D, a wire box in 3D. Empty bounds draw nothing. Same call
 * placement as debug_draw_end.
 */
void
debug_draw_bounds(grain_bounds_t bounds, CF_Color color);

#endif
