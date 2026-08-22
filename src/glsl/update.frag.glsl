#include "internal/builtins.glsl"
#include "archetype/update.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain_pool_size;  // CF does not support uint uniform
	float grain_lifetime_budget;
};

void main() {
	ivec2 texel = ivec2(gl_FragCoord.xy);
	ivec2 size  = textureSize(grain_texture_0, 0);

	uint gid    = uint(texel.y * size.x + texel.x);
	uint region = gid / uint(grain_pool_size);
	uint lid    = gid % uint(grain_pool_size);

	ParticleAttrs particle = grain_load_ParticleAttrs(texel);
	SystemParams params = grain_load_SystemParams(region);
	SystemClock clock = grain_load_SystemClock(region);

	Schedule sch = schedule(lid, grain_lifetime_budget, clock);
	uint gen = sch.gen + clock.gen_base;

	srand(gid, gen);

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = clock.elapsed - (sch.emit ? (clock.dt - sch.age) : 0.0);

	if (sch.emit) {
		grain_emit(particle, params, ctx);
	}

	if (sch.started) {
		grain_process(particle, params, ctx);
	}

	grain_store_ParticleAttrs(particle);
}
