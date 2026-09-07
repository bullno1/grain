#ifndef GRAINC_EMIT_H
#define GRAINC_EMIT_H

#include <stdio.h>
#include <stdbool.h>
#include <grain_baked.h>

//! One bit per graphics API a baked header can embed shaders for
typedef enum {
	GRAINC_TARGET_GLES3 = 1 << 0,   // GLSL 300 es source
	GRAINC_TARGET_VULKAN = 1 << 1,  // SPIR-V bytecode
	GRAINC_TARGET_D3D12 = 1 << 2,   // HLSL source
	GRAINC_TARGET_METAL = 1 << 3,   // MSL source
} grainc_target_t;

typedef uint32_t grainc_target_mask_t;

//! What `web` aliases to
#define GRAINC_TARGET_WEB GRAINC_TARGET_GLES3
//! What `desktop` aliases to: every desktop API
#define GRAINC_TARGET_DESKTOP ( \
	GRAINC_TARGET_GLES3 \
	| GRAINC_TARGET_VULKAN \
	| GRAINC_TARGET_D3D12 \
	| GRAINC_TARGET_METAL \
)
#define GRAINC_TARGET_ALL (GRAINC_TARGET_DESKTOP | GRAINC_TARGET_WEB)

/**
 * Print a baked effect as an stb-style single-header C module.
 *
 * `prefix` names every external symbol (`<prefix>_effect`, `<prefix>`,
 * `<prefix>_load`) and, uppercased, the implementation guard.
 */
void
grainc_emit_header(
	FILE* out,
	const char* source_name,
	const char* prefix,
	const grain_baked_effect_t* effect,
	grainc_target_mask_t targets
);

#endif
