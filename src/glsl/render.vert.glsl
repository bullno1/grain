#include "internal/builtins.glsl"
#include "archetype/render.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain__pool_size;  // CF does not support uint uniform
};

layout(location = 0) flat out uint v_region;
layout(location = 1) flat out uint v_lid;

void main() {
	uint inst   = uint(gl_InstanceIndex);
	uint packed = inst / uint(grain__pool_size); // position in the culled draw list
	uint lid    = inst % uint(grain__pool_size); // slot within the system's pool
	uint region = grain__load_draw_region(packed);  // which texture region it occupies

	uint flat   = region * uint(grain__pool_size) + lid;

	ivec2 size  = textureSize(grain__texture_0, 0);
	ivec2 texel = ivec2(int(flat) % size.x, int(flat) / size.x);

	ParticleAttrs particle = grain__load_ParticleAttrs(texel);
	ModuleParams params = grain__load_ModuleParams(region);
	SystemClock clock = grain__load_SystemClock(region);

	Schedule sch = schedule(lid, uint(grain__pool_size), clock);
	uint gen = sch.gen + clock.gen_base;

	srand(flat, gen);

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
