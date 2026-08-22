#include "internal/builtins.glsl"

// Declared before the archetype include so the renderer module can use grain_transform.
layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain_pool_size;  // CF does not support uint uniform
	mat4 grain_transform; // The caller's 2D transform stack, world -> clip.
};

#include "archetype/render.glsl"

layout(location = 0) flat out uint v_region;
layout(location = 1) flat out uint v_lid;

void main() {
	uint inst   = uint(gl_InstanceIndex);
	uint draw_idx = inst / uint(grain_pool_size); // position in the culled draw list
	uint lid    = inst % uint(grain_pool_size); // slot within the system's pool
	uint region = grain_load_draw_region(draw_idx);  // which texture region it occupies

	uint gid   = region * uint(grain_pool_size) + lid;

	ivec2 size  = textureSize(grain_texture_0, 0);
	ivec2 texel = ivec2(int(gid) % size.x, int(gid) / size.x);

	ParticleAttrs particle = grain_load_ParticleAttrs(texel);
	ModuleParams params = grain_load_ModuleParams(region);
	SystemClock clock = grain_load_SystemClock(region);

	Schedule sch = schedule(lid, uint(grain_pool_size), clock);
	uint gen = sch.gen + clock.gen_base;

	srand(gid, gen);

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = clock.elapsed - (sch.emit ? (clock.dt - sch.age) : 0.0);

	v_region = region;
	v_lid = lid;

	if (sch.started) {
		process(particle, params, ctx);
	} else {
		cull();
	}
}
