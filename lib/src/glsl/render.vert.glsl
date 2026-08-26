#include "grain/api.glsl"

// Declared before the archetype include so the renderer module can use grain_transform.
layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain_pool_size;  // CF does not support uint uniform
	mat4 grain_transform; // The caller's 2D transform stack, world -> clip.
};

#include "archetype/render.glsl"

// Locations 14-15: at the top of Vulkan's guaranteed range, clear of the
// module's Varying() declarations, which count up from 0.
layout(location = 14) flat out uint grain_v_region;
layout(location = 15) flat out uint grain_v_lid;

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
	grain_SystemClock clock = grain_load_SystemClock(region);

	grain_Schedule sch = grain_observe(particle.grain_birth, clock);

	grain_srand(gid, floatBitsToUint(particle.grain_birth));

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = sch.emit ? sch.birth : clock.elapsed;

	grain_v_region = region;
	grain_v_lid = lid;

	if (sch.started) {
		process(particle, params, ctx);
	} else {
		cull();
	}
}
