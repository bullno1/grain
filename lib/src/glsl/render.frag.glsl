#include "grain/api.glsl"
#include "archetype/render.glsl"

// Locations 14-15: reserved for grain, see render.vert.glsl.
layout(location = 14) flat in uint grain_v_region;
layout(location = 15) flat in uint grain_v_lid;

layout(location = 0) out vec4 result;

void main() {
	uint gid    = grain_v_region * uint(grain_pool_size) + grain_v_lid;
	ivec2 size  = textureSize(grain_texture_0, 0);
	ivec2 texel = ivec2(int(gid) % size.x, int(gid) / size.x);

	ParticleAttrs particle = grain_load_ParticleAttrs(texel);
	ModuleParams params = grain_load_ModuleParams(grain_v_region);
	grain_SystemClock clock = grain_load_SystemClock(grain_v_region);

	grain_Schedule sch = grain_observe(particle.grain_birth, clock);

	grain_srand(gid, floatBitsToUint(particle.grain_birth));

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = sch.emit ? sch.birth : clock.elapsed;

	process(particle, params, ctx);
	result = grain_Color;
}
