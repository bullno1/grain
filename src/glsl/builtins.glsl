#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

#define GRAIN_SAMPLER_SET 0
#define GRAIN_UNIFORM_SET 1
#define Varying(X) layout(location = X) out

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

#define GRAIN_SAMPLER_SET 2
#define GRAIN_UNIFORM_SET 3
#define Varying(X) layout(location = X) in

#endif

struct Ctx {
	float dt;
	float frame_dt;
	float time;
};

struct SystemClock {
    float rate;
    float elapsed;
    float dt;
    uint gen_base;
};

struct Schedule {
    uint  gen;
    float age;
    bool  started;
    bool  emit;
};

uint grain_rng_state;

uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void srand(uint id, uint gen) { grain_rng_state = pcg(id ^ pcg(gen)); }

float rand() {
    grain_rng_state = pcg(grain_rng_state);
    return float(grain_rng_state) * (1.0 / 4294967296.0);
}

Schedule schedule(uint local_id, uint pool_size, SystemClock clock) {
	Schedule s;

	float period = float(pool_size) / clock.rate;
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

SystemClock grain_unpack_SystemClock(uvec4 v) {
	SystemClock clock;
	clock.rate = uintBitsToFloat(v.x);
	clock.elapsed = uintBitsToFloat(v.y);
	clock.dt = uintBitsToFloat(v.z);
	clock.gen_base = v.w;
	return clock;
}

// Read from RGBA32F
void
grain_convert(out int dst, float src) {
	dst = floatBitsToInt(src);
}

void
grain_convert(out uint dst, float src) {
	dst = floatBitsToUint(src);
}

void
grain_convert(out float dst, float src) {
	dst = src;
}

// Write to RGBA32F
void
grain_convert(out float dst, int src) {
	dst = intBitsToFloat(src);
}

void
grain_convert(out float dst, uint src) {
	dst = uintBitsToFloat(src);
}

// Read from RGBA32UI
void
grain_convert(out int dst, uint src) {
	dst = int(src);
}

void
grain_convert(out uint dst, uint src) {
	dst = src;
}

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

vec2 quad() {
	vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);  // [0,1]
	return corner - 0.5; // [-0.5, 0.5]
}

void cull() {
	gl_Position = vec4(2.0, 2.0, 2.0, 0.0);
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

vec4 grain_Color;

#endif
