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
#define GRAIN_MAX_LIFETIME_BUDGET (((GRAIN_DT_MIN / GRAIN_SAFETY) * (double)(1u << 23)) * 0.5)

typedef struct {
	double   rate;

	double   period;      // == the pool's lifetime_budget
	double   wrap_after;   // 0 => wrapping disabled
	uint32_t wrap_cycles;  // generations discarded per wrap

	double   elapsed;     // double accumulator, never degrades
	uint32_t gen_base;     // generations before the current epoch
} grain_particle_clock_t;

static inline void
grain_init_clock(grain_particle_clock_t* c, double lifetime_budget, double rate) {
	c->rate      = rate;
	c->period    = lifetime_budget;
	c->elapsed   = 0.0;
	c->gen_base  = 0;

	double t_max    = (GRAIN_DT_MIN / GRAIN_SAFETY) * (double)(1u << 23);
	double headroom = t_max - c->period;
	double cycles   = (headroom > 0.0) ? floor(headroom / c->period) : 0.0;

	c->wrap_cycles   = (uint32_t)cycles;
	c->wrap_after    = cycles * c->period;
}

// Returns the dt actually applied to `elapsed`, which is what the caller must upload to the GPU
static inline double
grain_advance_clock(grain_particle_clock_t* c, double dt) {
	if (dt < 0.0)            { dt = 0.0; }

	c->elapsed += dt;

	if (c->wrap_after > 0.0 && c->elapsed >= c->wrap_after) {
		double cycles = floor(c->elapsed / c->period) - 1.0;
		if (cycles > 0.0) {
			c->elapsed   -= cycles * c->period;
			c->gen_base  += (uint32_t)cycles;
		}
	}

	return dt;
}

static inline void
grain_set_clock_rate(grain_particle_clock_t* c, double lifetime_budget, double rate) {
	uint32_t gen_now = c->gen_base + (uint32_t)floor(c->elapsed / c->period);
	grain_init_clock(c, lifetime_budget, rate);
	c->gen_base = gen_now;
}

#endif
