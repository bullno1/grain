#include "internal/builtins.glsl"
#include "archetype/render.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain_pool_size;
	float grain_lifetime_budget;
};

layout(location = 0) flat in uint v_region;
layout(location = 1) flat in uint v_lid;

layout(location = 0) out vec4 result;

void main() {
	uint gid    = v_region * uint(grain_pool_size) + v_lid;
	ivec2 size  = textureSize(grain_texture_0, 0);
	ivec2 texel = ivec2(int(gid) % size.x, int(gid) / size.x);

	ParticleAttrs particle = grain_load_ParticleAttrs(texel);
	ModuleParams params = grain_load_ModuleParams(v_region);
	SystemClock clock = grain_load_SystemClock(v_region);

	Schedule sch = schedule(v_lid, grain_lifetime_budget, clock);
	uint gen = sch.gen + clock.gen_base;

	srand(gid, gen);

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = clock.elapsed - (sch.emit ? (clock.dt - sch.age) : 0.0);

	process(particle, params, ctx);
	result = grain_Color;
}
