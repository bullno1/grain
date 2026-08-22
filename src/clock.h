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

typedef struct {
	double   rate;

	double   period;      // pool_size / rate
	double   wrap_after;   // 0 => wrapping disabled
	uint32_t wrap_cycles;  // generations discarded per wrap

	double   elapsed;     // double accumulator, never degrades
	uint32_t gen_base;     // generations before the current epoch
	bool     period_too_long;   // pool is oversized for this rate
} grain_particle_clock_t;

static inline void
grain_init_clock(grain_particle_clock_t* c, uint32_t pool_size, double rate) {
	c->rate      = rate;
	c->period    = (double)pool_size / rate;
	c->elapsed   = 0.0;
	c->gen_base  = 0;

	double t_max    = (GRAIN_DT_MIN / GRAIN_SAFETY) * (double)(1u << 23);
	double headroom = t_max - c->period;
	double cycles   = (headroom > 0.0) ? floor(headroom / c->period) : 0.0;

	c->period_too_long = (cycles < 1.0);
	c->wrap_cycles   = c->period_too_long ? 0u : (uint32_t)cycles;
	c->wrap_after    = c->period_too_long ? 0.0 : cycles * c->period;
}

static inline void
grain_advance_clock(grain_particle_clock_t* c, double dt) {
	if (dt > GRAIN_DT_CLAMP) { dt = GRAIN_DT_CLAMP; }
	if (dt < 0.0)            { dt = 0.0; }

	c->elapsed += dt;

	if (c->wrap_after > 0.0 && c->elapsed >= c->wrap_after) {
		double cycles = floor(c->elapsed / c->period);
		c->elapsed   -= cycles * c->period;
		c->gen_base  += (uint32_t)cycles;
	}
}

static inline void
grain_set_clock_rate(grain_particle_clock_t* c, uint32_t pool_size, double rate) {
	uint32_t gen_now = c->gen_base + (uint32_t)floor(c->elapsed / c->period);
	grain_init_clock(c, pool_size, rate);
	c->gen_base = gen_now;
}

#endif
