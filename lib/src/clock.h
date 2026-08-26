#ifndef GRAIN_CLOCK_H
#define GRAIN_CLOCK_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Precision budget for the highp float uploaded to the GPU.
// DT_MIN: shortest frame we expect to resolve (240 Hz).
// SAFETY: distinct float values required per frame step.
#define GRAIN_DT_MIN   (1.0 / 240.0)
#define GRAIN_SAFETY   64.0
#define GRAIN_DT_CLAMP 0.05        // ignore tab-switch / breakpoint spikes

// Longest lifetime budget whose generations still resolve as a 32-bit float. `elapsed`
// must keep one frame step representable, and wrapping needs room for a whole period
// beyond the period itself -- hence the halving. Checked by grain_create_pool.
#define GRAIN_MAX_LIFETIME_BUDGET \
	(((GRAIN_DT_MIN / GRAIN_SAFETY) * (double)(1u << 23)) * 0.5)

typedef struct {
	double   rate;

	double   period;        // == the pool's lifetime_budget; only the wrap uses it
	double   wrap_after;    // fold `elapsed` once it passes this (0 => disabled)
	double   elapsed;       // double accumulator, never degrades
	double   emitted;       // particles emitted so far, fractional
	double   pending_burst; // burst particles queued since the last upload

	// Snapshot taken at the last upload. The GPU's window is [synced, current), so
	// several ticks between update passes fold into one window instead of the last
	// tick overwriting the others.
	double   elapsed_synced;
	double   emitted_synced;
	double   wrap_pending;  // total `elapsed` shift since the last upload
} grain_particle_clock_t;

// What the GPU receives per system. Two vec4s wide so the GLES path can carry it as
// a pair of RGBA32UI texels; padded explicitly so std430 and the texel path agree.
typedef struct {
	float elapsed;
	float dt;           // elapsed advanced since the last update pass
	float emit_base;    // emission counter at the last update pass, mod pool_size
	float emit_count;   // particles to emit this pass: window is [base, base + count)
	float wrap_shift;   // subtract from stored birth times once, then forget
	float burst_base;   // counter position where the burst window starts, mod pool_size
	float burst_count;  // burst particles this pass: window is [base, base + count)
	float pad0;
} grain_clock_entry_t;

static inline void
grain_init_clock(grain_particle_clock_t* c, double lifetime_budget, double rate) {
	c->rate           = rate;
	c->period         = lifetime_budget;
	c->elapsed        = 0.0;
	c->emitted        = 0.0;
	c->pending_burst  = 0.0;
	c->elapsed_synced = 0.0;
	c->emitted_synced = 0.0;
	c->wrap_pending   = 0.0;

	double t_max    = (GRAIN_DT_MIN / GRAIN_SAFETY) * (double)(1u << 23);
	double headroom = t_max - c->period;
	double cycles   = (headroom > 0.0) ? floor(headroom / c->period) : 0.0;
	c->wrap_after   = cycles * c->period;
}

static inline void
grain_advance_clock(grain_particle_clock_t* c, double dt) {
	if (dt < 0.0) { dt = 0.0; }

	c->elapsed += dt;
	c->emitted += c->rate * dt;

	if (c->wrap_after > 0.0 && c->elapsed >= c->wrap_after) {
		double cycles = floor(c->elapsed / c->period) - 1.0;
		if (cycles > 0.0) {
			double shift = cycles * c->period;
			c->elapsed        -= shift;
			c->elapsed_synced -= shift;
			c->wrap_pending   += shift;
		}
	}
}

static inline void
grain_set_clock_rate(grain_particle_clock_t* c, double rate) {
	c->rate = rate;
}

// Queue a burst: emitted all at one instant at the next snapshot, in counter
// positions appended after the steady window. Accumulates across calls until the
// snapshot collects it; the accumulated total saturates at `max_pending`.
static inline void
grain_queue_burst(grain_particle_clock_t* c, double count, double max_pending) {
	c->pending_burst += count;
	if (c->pending_burst > max_pending) { c->pending_burst = max_pending; }
}

// Produce the GPU entry for everything that happened since the last snapshot, then
// mark it all as uploaded.
static inline grain_clock_entry_t
grain_snapshot_clock(grain_particle_clock_t* c, int pool_size) {
	double count_s = c->emitted - c->emitted_synced;
	double count_b = c->pending_burst;
	// More emissions than slots in one pass would need a slot to be born twice; the
	// ring can only express one. Drop the excess rather than corrupt the ring.
	// The burst keeps its particles first: a thinned stream for one pathological
	// frame is invisible, a shrunken explosion is not.
	if (count_b > (double)pool_size)           { count_b = (double)pool_size; }
	if (count_s > (double)pool_size - count_b) { count_s = (double)pool_size - count_b; }

	grain_clock_entry_t e = {
		.elapsed     = (float)c->elapsed,
		.dt          = (float)(c->elapsed - c->elapsed_synced),
		.emit_base   = (float)fmod(c->emitted_synced, (double)pool_size),
		.emit_count  = (float)count_s,
		.wrap_shift  = (float)c->wrap_pending,
		// The burst window sits right after the steady one: bursts consume counter
		// positions like any emission, so the ring discipline is undisturbed.
		.burst_base  = (float)fmod(c->emitted, (double)pool_size),
		.burst_count = (float)count_b,
	};

	// Advance by the full raw amount even when clamped: silent-drop parity with the
	// steady path above.
	c->emitted       += c->pending_burst;
	c->pending_burst  = 0.0;
	c->elapsed_synced = c->elapsed;
	c->emitted_synced = c->emitted;
	c->wrap_pending   = 0.0;
	return e;
}

#endif
