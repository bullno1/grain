#include "internal/builtins.glsl"
#include "archetype/update.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	int grain_pool_size;  // CF does not support uint uniform
};

void main() {
	ivec2 texel = ivec2(gl_FragCoord.xy);
	ivec2 size  = textureSize(grain_texture_0, 0);

	uint gid    = uint(texel.y * size.x + texel.x);
	uint region = gid / uint(grain_pool_size);
	uint lid    = gid % uint(grain_pool_size);

	ParticleAttrs particle = grain_load_ParticleAttrs(texel);
	float birth = grain_load_birth(texel);
	SystemParams params = grain_load_SystemParams(region);
	SystemClock clock = grain_load_SystemClock(region);

	// The CPU folded `elapsed` down to keep it precise; bring this particle's birth
	// into the same epoch. Only the update pass does this, and it stores the result,
	// so the shift is applied exactly once.
	if (birth >= 0.0) { birth -= clock.wrap_shift; }

	Schedule sch = schedule(lid, uint(grain_pool_size), birth, clock);
	birth = sch.birth;

	// The seed is the birth time: unique per particle, fixed for its whole life, and
	// the render stages recover the identical value from the texture.
	srand(gid, floatBitsToUint(birth));

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = sch.emit ? sch.birth : clock.elapsed;

	if (sch.emit) {
		grain_emit(particle, params, ctx);
	}

	if (sch.started) {
		grain_process(particle, params, ctx);
	}

	grain_store(particle, birth);
}
