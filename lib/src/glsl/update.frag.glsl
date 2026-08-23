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
	SystemParams params = grain_load_SystemParams(region);
	SystemClock clock = grain_load_SystemClock(region);

	// The CPU folded `elapsed` down to keep it precise; bring this particle's birth
	// into the same epoch. Only the update pass does this, and it stores the result,
	// so the shift is applied exactly once.
	if (particle.grain_birth >= 0.0) { particle.grain_birth -= clock.wrap_shift; }

	Schedule sch = schedule(lid, uint(grain_pool_size), particle.grain_birth, clock);

	// The seed is the birth time: unique per particle, fixed for its whole life, and
	// the render stages recover the identical value from the texture.
	srand(gid, floatBitsToUint(sch.birth));

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

	// Reassign to prevent wholesale assign from modules
	particle.grain_birth = sch.birth;
	grain_store(particle);
}
