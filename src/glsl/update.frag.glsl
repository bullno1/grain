#include "grain/internal/builtins.glsl"
#include "archetype/update.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	uint grain__pool_size;
};

Schedule schedule(uint local_id, SystemClock clock) {
	Schedule s;

	float period = float(grain__pool_size) / clock.rate;
	float first  = float(local_id) / clock.rate;

	float t_now  = clock.elapsed - first;
	float t_prev = t_now - clock.dt;

	if (t_now < 0.0) {
		s.gen = 0u;
		s.age = 0.0;
		s.started = false;
		s.emit = false;
		return s;
	}

	float gen_now = floor(t_now / period);
	s.gen     = uint(gen_now);
	s.age     = t_now - gen_now * period;
	s.started = true;
	s.emit    = (t_prev < 0.0) || (gen_now > floor(t_prev / period));
	return s;
}

void main() {
	ivec2 texel = ivec2(gl_FragCoord.xy);
	ivec2 size  = textureSize(uPos, 0);

	uint flat   = uint(texel.y * size.x + texel.x);
	uint region = flat / grain__pool_size;
	uint lid    = flat % grain__pool_size;

	ParticleAttrs particle = grain__load_ParticleAttrs(texel);
	SystemParams params = grain__load_SystemParams(region);
	SystemClock clock = grain__load_SystemClock(region);

	Schedule sch = schedule(lid, clock);
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
