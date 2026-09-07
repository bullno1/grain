// The probe pass: the render stage re-run with identity view transforms into
// a small float canvas, one column of texels per slot, then read back. See
// probe.vert.glsl for the GPU side and grain.h for the contract.
#include "internal.h"
#include <float.h>
#include <math.h>

// Layout of a slot's column in the probe canvas; mirrors probe.vert.glsl.
// One RGBA32F texel per quad corner: xyz position, w age (negative: not live).
#define GRAIN_PROBE_ROWS 4
#define GRAIN_PROBE_TEXEL_SIZE (4 * (int)sizeof(float))

struct grain_probe_s {
	grain_t* grain;

	// The draw is recorded into the frame's command buffer at capture; the
	// copy out is a separate submission on the SDL_GPU backends, so it is
	// issued on the first poll, once the caller has presented that frame
	CF_Canvas canvas;
	CF_Readback readback;
	bool readback_started;
	int pool_size;

	// Filled at capture; the aggregates and slots once the copy has landed
	grain_probe_result_t result;
	grain_probe_slot_t* slots;
	grain_probe_status_t status;
};

// ---- Bounds ----

grain_bounds_t
grain_bounds_empty(void) {
	return (grain_bounds_t){
		.min = { FLT_MAX, FLT_MAX, FLT_MAX },
		.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
	};
}

bool
grain_bounds_is_empty(grain_bounds_t bounds) {
	return bounds.min[0] > bounds.max[0]
		|| bounds.min[1] > bounds.max[1]
		|| bounds.min[2] > bounds.max[2];
}

void
grain_bounds_union(grain_bounds_t* bounds, grain_bounds_t other) {
	if (grain_bounds_is_empty(other)) { return; }
	if (grain_bounds_is_empty(*bounds)) {
		*bounds = other;
		return;
	}
	for (int i = 0; i < 3; ++i) {
		if (other.min[i] < bounds->min[i]) { bounds->min[i] = other.min[i]; }
		if (other.max[i] > bounds->max[i]) { bounds->max[i] = other.max[i]; }
	}
}

static void
grain_bounds_add_point(grain_bounds_t* bounds, const float* p) {
	for (int i = 0; i < 3; ++i) {
		if (p[i] < bounds->min[i]) { bounds->min[i] = p[i]; }
		if (p[i] > bounds->max[i]) { bounds->max[i] = p[i]; }
	}
}

// ---- Pool-side resources ----

static void
grain_make_pool_probe(grain_pool_t* pool) {
	if (pool->probe_canvas.id != 0) { return; }

	CF_CanvasParams canvas_params = cf_canvas_defaults(pool->pool_size, GRAIN_PROBE_ROWS);
	canvas_params.target_count = 1;
	canvas_params.targets[0].allocate_mipmaps = false;
	canvas_params.targets[0].filter = CF_FILTER_NEAREST;
	canvas_params.targets[0].wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
	canvas_params.targets[0].wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
	canvas_params.targets[0].pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
	pool->probe_canvas = cf_make_canvas(canvas_params);

	// One region index. The GLES path reads storage buffers as uvec4 texels,
	// so allocate a whole one.
	grain_init_ssbo(&pool->probe_list, sizeof(uint32_t) * 4);
}

void
grain_cleanup_pool_probe(grain_pool_t* pool) {
	if (pool->probe_canvas.id == 0) { return; }

	cf_destroy_canvas(pool->probe_canvas);
	grain_cleanup_ssbo(&pool->probe_list);
	pool->probe_canvas = (CF_Canvas){ 0 };
}

// ---- Capture ----

grain_probe_t*
grain_probe_system(grain_system_t* system) {
	grain_pool_t* pool = system->pool;
	grain_t* grain = pool->grain;
	grain_archetype_t* archetype = pool->opts.archetype;
	int index = system - pool->systems;

	if (!archetype->has_probe_sources) {
		grain_set_last_error(grain, "Baked archetypes carry no shader source and cannot be probed");
		return NULL;
	}

	grain_reconcile_pool(pool);

	if (archetype->shaders.probe_shader.id == 0) {
		cf_arena_reset(&grain->arena);
		if (!grain_dsl_compile_probe(grain, &archetype->probe_sources, &archetype->shaders)) {
			return NULL;
		}
	}

	grain_make_pool_probe(pool);

	*(uint32_t*)grain_index_ssbo(&pool->probe_list, sizeof(uint32_t), 0) = (uint32_t)index;
	grain_sync_ssbo(&pool->probe_list, sizeof(uint32_t));

	// Parameters may have changed since the last render
	int system_hwm = grain_find_system_hwm(pool);
	grain_sync_ssbo(&pool->render_ssbo, archetype->render_size * (system_hwm + 1));
	grain_sync_ssbo(&pool->clock_ssbo, (system_hwm + 1) * sizeof(grain_clock_entry_t));

	cf_apply_canvas(pool->probe_canvas, false);
	cf_apply_mesh(grain->dummy_mesh);
	grain_bind_pool_textures(pool);

	// Both transform families at identity: whichever the renderer goes
	// through, its clip output is then the world position it fed in. The
	// system's own transform stays in effect, exactly as in a render pass.
	CF_M4x4 identity = cf_m4_identity();
	const char* view_uniforms[] = { "grain_transform", "grain_transform3d", "grain_projection" };
	for (int i = 0; i < (int)CF_ARRAY_SIZE(view_uniforms); ++i) {
		cf_material_set_uniform_vs(pool->material, view_uniforms[i], identity.elements, CF_UNIFORM_TYPE_MAT4, 1);
		cf_material_set_uniform_fs(pool->material, view_uniforms[i], identity.elements, CF_UNIFORM_TYPE_MAT4, 1);
	}

	// Texels are written verbatim, one triangle each: no blending, no depth,
	// and never the user's render state
	CF_RenderState probe_state = cf_render_state_defaults();
	probe_state.primitive_type = CF_PRIMITIVE_TYPE_TRIANGLELIST;
	cf_material_set_render_state(pool->material, probe_state);
	cf_apply_shader(archetype->shaders.probe_shader, pool->material);
	CF_StorageBuffer storage_buffers[] = {
		pool->render_ssbo.gpu,
		pool->clock_ssbo.gpu,
		pool->probe_list.gpu,
	};
	cf_apply_vs_storage_buffers(storage_buffers, CF_ARRAY_SIZE(storage_buffers));
	cf_apply_scissor(0, 0, pool->pool_size, GRAIN_PROBE_ROWS);
	cf_draw_elements_range(0, 3, pool->pool_size * GRAIN_PROBE_ROWS);

	grain_probe_t* probe = cf_alloc(sizeof(grain_probe_t));
	*probe = (grain_probe_t){
		.grain = grain,
		.canvas = pool->probe_canvas,
		.pool_size = pool->pool_size,
	};

	// What the GPU state reflects is the last update pass, not the ticks
	// accumulated since
	const grain_particle_clock_t* clock = &pool->clocks[index];
	probe->result.elapsed = (float)clock->elapsed_synced;
	probe->result.emit_cursor = (float)fmod(clock->emitted_synced, (double)pool->pool_size);
	probe->result.num_slots = pool->pool_size;
	probe->result.bounds = grain_bounds_empty();

	return probe;
}


// Issues the copy out on the first poll. The caller polls after presenting
// the frame that drew the probe, so on the SDL_GPU backends, where the copy is
// its own submission, it lands behind the draw; the GLES path copies
// synchronously either way.
static void
grain_probe_start_readback(grain_probe_t* probe) {
	if (probe->readback_started) { return; }
	probe->readback_started = true;
	probe->readback = cf_canvas_readback(probe->canvas);
	if (probe->readback.id == 0) {
		grain_set_last_error(probe->grain, "Probe canvas readback failed");
		probe->status = GRAIN_PROBE_FAILED;
	}
}

static bool
grain_probe_resolve(grain_probe_t* probe) {
	int pool_size = probe->pool_size;
	int expected_size = pool_size * GRAIN_PROBE_ROWS * GRAIN_PROBE_TEXEL_SIZE;
	if (cf_readback_size(probe->readback) != expected_size) {
		grain_set_last_error(probe->grain, "Probe readback has an unexpected size");
		return false;
	}

	float* pixels = cf_alloc(expected_size);
	if (cf_readback_data(probe->readback, pixels, expected_size) != expected_size) {
		cf_free(pixels);
		grain_set_last_error(probe->grain, "Probe readback returned short");
		return false;
	}

	grain_probe_slot_t* slots = cf_alloc(sizeof(grain_probe_slot_t) * pool_size);
	grain_probe_result_t* result = &probe->result;
	result->num_live = 0;
	result->max_age = 0.f;
	result->bounds = grain_bounds_empty();

	// Every corner of a slot carries the same age, so which row a corner
	// landed in (backends disagree on row order) never matters
	for (int lid = 0; lid < pool_size; ++lid) {
		grain_probe_slot_t* slot = &slots[lid];
		*slot = (grain_probe_slot_t){ .bounds = grain_bounds_empty() };

		for (int row = 0; row < GRAIN_PROBE_ROWS; ++row) {
			const float* texel = pixels + (row * pool_size + lid) * 4;
			if (texel[3] < 0.f) { continue; }

			slot->live = true;
			slot->age = texel[3];
			grain_bounds_add_point(&slot->bounds, texel);
		}

		if (slot->live) {
			result->num_live += 1;
			if (slot->age > result->max_age) { result->max_age = slot->age; }
			grain_bounds_union(&result->bounds, slot->bounds);
		}
	}

	cf_free(pixels);
	probe->slots = slots;
	result->slots = slots;
	return true;
}

grain_probe_status_t
grain_probe_poll(grain_probe_t* probe, const grain_probe_result_t** out) {
	if (out != NULL) { *out = NULL; }
	grain_probe_start_readback(probe);

	if (probe->status == GRAIN_PROBE_PENDING) {
		if (!cf_readback_ready(probe->readback)) { return GRAIN_PROBE_PENDING; }
		probe->status = grain_probe_resolve(probe) ? GRAIN_PROBE_READY : GRAIN_PROBE_FAILED;
	}
	if (probe->status == GRAIN_PROBE_READY && out != NULL) { *out = &probe->result; }
	return probe->status;
}

void
grain_destroy_probe(grain_probe_t* probe) {
	if (probe == NULL) { return; }

	if (probe->readback.id != 0) { cf_destroy_readback(probe->readback); }
	cf_free(probe->slots);
	cf_free(probe);
}
