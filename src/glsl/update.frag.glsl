#include "internal/builtins.glsl"
#include "archetype/update.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain__pool_size;  // CF does not support uint uniform
};

void main() {
	ivec2 texel = ivec2(gl_FragCoord.xy);
	ivec2 size  = textureSize(grain__texture_0, 0);

	uint flat   = uint(texel.y * size.x + texel.x);
	uint region = flat / uint(grain__pool_size;
	uint lid    = flat % uint(grain__pool_size);

	ParticleAttrs particle = grain__load_ParticleAttrs(texel);
	SystemParams params = grain__load_SystemParams(region);
	SystemClock clock = grain__load_SystemClock(region);
*)
	Schedule sch = schedule(lid, uint(grain__pool_size), clock);
	uint gen = sch.gen + clock.gen_base;

	srand(flat, gen);

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = clock.elapsed - (sch.emit ? (clock.dt - sch.age) : 0.0);

	if (sch.emit) {
		grain__emit(particle, params, ctx);
	}

	if (sch.started) {
		grain__process(particle, params, ctx);
	}

	grain__store_ParticleAttrs(particle);
}
