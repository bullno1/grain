#ifndef GRAIN_EDITOR_DEBUG_DRAW_H
#define GRAIN_EDITOR_DEBUG_DRAW_H

#include <grain.h>

/**
 * World-space gizmos for params carrying debug decorators.
 *
 * The vocabulary is editor-assigned; the library treats decorators as opaque.
 * Reference arguments are bare identifiers naming a sibling param in the same
 * Params block; a reference that does not resolve disables the gizmo silently.
 *
 * @position            (vec2)  crosshair + draggable handle, writes back
 * @radius(at)          (float) circle around the anchor
 * @angle(at, length)   (float) arrow ray from the anchor, angle in radians
 * @arc(at, to, inner, outer)
 *                      (float) wedge between this angle and `to`'s value;
 *                              inner/outer radii make it an annular sector
 * @vector(at, scale, angle)
 *                      (vec2)  arrow of the value scaled by `scale`, or
 *                      (float) magnitude along `angle` radians
 * @extent(at)          (vec2)  rectangle of that size centered on the anchor
 */

//! Reset the frame's gizmo list; call once per frame before any module UI
void
debug_draw_begin(void);

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

//! Draw every registered gizmo; call after grain_end_render
void
debug_draw_end(void);

#endif
