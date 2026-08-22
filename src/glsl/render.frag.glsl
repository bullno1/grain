#include "internal/builtins.glsl"
#include "archetype/render.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain__pool_size;
};

layout(location = 0) flat in uint v_region;
layout(location = 1) flat in uint v_lid;

layout(location = 0) out vec4 output;

void main() {
	uint flat   = v_region * uint(grain__pool_size) + v_lid;
	ivec2 size  = textureSize(grain__texture_0, 0);
	ivec2 texel = ivec2(int(flat) % size.x, int(flat) / size.x);

	ParticleAttrs particle = grain__load_ParticleAttrs(texel);
	ModuleParams params = grain__load_ModuleParams(v_region);
	SystemClock clock = grain__load_SystemClock(v_region);

	Schedule sch = schedule(v_lid, uint(grain__pool_size), clock);
	uint gen = sch.gen + clock.gen_base;

	srand(flat, gen);

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = clock.elapsed - (sch.emit ? (clock.dt - sch.age) : 0.0);

	process(particle, params, ctx);
	output = grain_Color;
}
