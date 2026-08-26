#include <btest.h>
#include <math.h>
#include "clock.h"

// The emission clock is pure CPU state so the whole burst accounting is testable
// headlessly. dt values are powers of two so `emitted` stays exact in doubles.

static btest_suite_t clock_suite = {
	.name = "clock",
};

#define EXPECT_FLOAT_EQ(ACTUAL, EXPECTED) \
	BTEST_EXPECT_EX( \
		(ACTUAL) == (float)(EXPECTED), \
		"%s == %f, expected %f", #ACTUAL, (double)(ACTUAL), (double)(EXPECTED) \
	)

BTEST(clock_suite, burst_only) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 0.0);

	grain_queue_burst(&c, 10.0, 100.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.emit_count, 0.f);
	EXPECT_FLOAT_EQ(e.burst_base, 0.f);
	EXPECT_FLOAT_EQ(e.burst_count, 10.f);

	// The burst was collected; an empty pass follows
	e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.burst_count, 0.f);
	EXPECT_FLOAT_EQ(e.emit_count, 0.f);

	// Steady emission resumes after the burst's counter positions
	grain_set_clock_rate(&c, 40.0);
	grain_advance_clock(&c, 0.25);
	e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.emit_base, 10.f);
	EXPECT_FLOAT_EQ(e.emit_count, 10.f);
	EXPECT_FLOAT_EQ(e.burst_count, 0.f);
}

BTEST(clock_suite, burst_accumulates_then_clamps) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 0.0);

	grain_queue_burst(&c, 30.0, 50.0);
	grain_queue_burst(&c, 40.0, 50.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.burst_count, 50.f);

	// The clamp happened at queue time: the counter advanced by 50, not 70
	grain_set_clock_rate(&c, 4.0);
	grain_advance_clock(&c, 0.25);
	e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.emit_base, 50.f);
}

BTEST(clock_suite, mixed_frame_partitioning) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 40.0);

	grain_advance_clock(&c, 0.25);
	grain_queue_burst(&c, 5.0, 100.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 100);
	// Steady window first, burst window appended right after: disjoint, contiguous
	EXPECT_FLOAT_EQ(e.emit_base, 0.f);
	EXPECT_FLOAT_EQ(e.emit_count, 10.f);
	EXPECT_FLOAT_EQ(e.burst_base, 10.f);
	EXPECT_FLOAT_EQ(e.burst_count, 5.f);

	grain_advance_clock(&c, 0.25);
	e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.emit_base, 15.f);
	EXPECT_FLOAT_EQ(e.emit_count, 10.f);
	EXPECT_FLOAT_EQ(e.burst_count, 0.f);
}

BTEST(clock_suite, clamp_priority_burst_wins) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 72.0);

	// Raw steady 18 + burst 8 exceeds the 20 slots; the burst keeps its 8
	grain_advance_clock(&c, 0.25);
	grain_queue_burst(&c, 8.0, 8.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 20);
	EXPECT_FLOAT_EQ(e.burst_count, 8.f);
	EXPECT_FLOAT_EQ(e.emit_count, 12.f);
	// The burst window still sits at the raw counter position
	EXPECT_FLOAT_EQ(e.burst_base, 18.f);
}

BTEST(clock_suite, burst_exceeds_pool) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 0.0);

	grain_queue_burst(&c, 30.0, 100.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 20);
	EXPECT_FLOAT_EQ(e.burst_count, 20.f);

	// Silent-drop parity with steady: the counter advanced by the full 30
	grain_set_clock_rate(&c, 4.0);
	grain_advance_clock(&c, 0.25);
	e = grain_snapshot_clock(&c, 20);
	EXPECT_FLOAT_EQ(e.emit_base, 10.f);
}

BTEST(clock_suite, burst_zero_dt) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 40.0);

	// A burst without a tick is a well-formed empty-time window
	grain_queue_burst(&c, 7.0, 100.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.dt, 0.f);
	EXPECT_FLOAT_EQ(e.elapsed, 0.f);
	EXPECT_FLOAT_EQ(e.emit_count, 0.f);
	EXPECT_FLOAT_EQ(e.burst_count, 7.f);
}

BTEST(clock_suite, burst_across_wrap) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 0.0);
	BTEST_ASSERT(c.wrap_after > 0.0);

	// One giant tick past the wrap threshold folds `elapsed` down by whole periods
	grain_advance_clock(&c, c.wrap_after + 1.0);
	BTEST_EXPECT(c.wrap_pending > 0.0);
	double folded = c.elapsed;

	grain_queue_burst(&c, 5.0, 100.0);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 100);
	// The shift is reported once and the burst window is untouched by it
	BTEST_EXPECT(e.wrap_shift > 0.f);
	EXPECT_FLOAT_EQ(e.elapsed, folded);
	EXPECT_FLOAT_EQ(e.burst_base, 0.f);
	EXPECT_FLOAT_EQ(e.burst_count, 5.f);

	e = grain_snapshot_clock(&c, 100);
	EXPECT_FLOAT_EQ(e.wrap_shift, 0.f);
}

BTEST(clock_suite, ring_wraparound_base) {
	grain_particle_clock_t c;
	grain_init_clock(&c, 16.0, 72.0);

	// Move the counter near the seam, then burst across it
	grain_advance_clock(&c, 0.25);
	grain_clock_entry_t e = grain_snapshot_clock(&c, 20);
	EXPECT_FLOAT_EQ(e.emit_count, 18.f);

	grain_queue_burst(&c, 5.0, 100.0);
	e = grain_snapshot_clock(&c, 20);
	EXPECT_FLOAT_EQ(e.burst_base, 18.f);
	EXPECT_FLOAT_EQ(e.burst_count, 5.f);
	// base + count > pool_size: the GPU maps k mod pool_size, wrapping the seam
	BTEST_EXPECT(e.burst_base + e.burst_count > 20.f);
}
