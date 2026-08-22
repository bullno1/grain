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

// Mirrors grain_clock_entry_t. Two vec4s wide: see the comment there.
struct SystemClock {
    float elapsed;
    float dt;          // elapsed advanced since the last update pass
    float emit_base;   // emission counter at the last update pass, mod pool_size
    float emit_count;  // particles to emit this pass: window is [base, base + count)
    float wrap_shift;  // subtract from stored birth times once (update pass only)
    float grain_pad0;
    float grain_pad1;
    float grain_pad2;
};

// A particle's birth time is grain-internal state: it lives in a reserved lane of the
// attribute texture, outside ParticleAttrs, so modules can neither read nor clobber
// it. Negative means the slot has never been born.
struct Schedule {
    bool  started;  // the slot holds a particle
    bool  emit;     // ...and it was born during this frame's window
    float birth;    // absolute time of that birth (post-emit value, when emit is set)
    float age;      // elapsed - birth
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

// Update-pass scheduling. Emission is counted, not timed: the k-th particle ever
// emitted by a system lands in slot k mod pool_size, and each frame the CPU hands over
// the window [emit_base, emit_base + emit_count) of that counter. A slot emits iff some
// integer k in the window maps to it. Because the mapping never consults the rate, a
// rate change only alters how fast the window advances -- the birth times of existing
// particles are untouched, so nothing pops in or out.
//
// Round-robin over the whole pool means a slot is revisited every pool_size / rate
// seconds, and pool_size = ceil(max_rate * lifetime_budget) makes that at least the
// budget at every permitted rate: a live particle is never recycled.
Schedule schedule(uint lid, uint pool_size, float birth, SystemClock clock) {
	Schedule s;
	s.birth = birth;
	s.emit  = false;

	if (clock.emit_count > 0.0) {
		float base = clock.emit_base;
		// Smallest k >= base that maps to this slot. All-unsigned so no operand of %
		// is ever negative (undefined in GLSL ES).
		uint k_lo = uint(ceil(base));
		uint k = k_lo + (lid + pool_size - (k_lo % pool_size)) % pool_size;
		if (float(k) < base + clock.emit_count) {
			// Where in the frame the counter crossed k: spreads same-frame births
			// across the frame instead of stacking them at its end.
			float frac = (float(k) - base) / clock.emit_count;
			s.birth = clock.elapsed - clock.dt + frac * clock.dt;
			s.emit  = true;
		}
	}

	s.started = s.birth >= 0.0;
	s.age     = s.started ? clock.elapsed - s.birth : 0.0;
	return s;
}

// Render-pass view of a slot: the update pass already stored the birth, so nothing is
// decided here, only read back. `emit` recovers "born during the last update" so the
// render stages can hand modules the same Ctx the update stage did.
Schedule grain_observe(float birth, SystemClock clock) {
	Schedule s;
	s.birth   = birth;
	s.started = birth >= 0.0;
	s.age     = s.started ? clock.elapsed - birth : 0.0;
	s.emit    = s.started && s.age <= clock.dt;
	return s;
}

SystemClock grain_unpack_SystemClock(uvec4 a, uvec4 b) {
	SystemClock clock;
	clock.elapsed    = uintBitsToFloat(a.x);
	clock.dt         = uintBitsToFloat(a.y);
	clock.emit_base  = uintBitsToFloat(a.z);
	clock.emit_count = uintBitsToFloat(a.w);
	clock.wrap_shift = uintBitsToFloat(b.x);
	clock.grain_pad0 = 0.0;
	clock.grain_pad1 = 0.0;
	clock.grain_pad2 = 0.0;
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
