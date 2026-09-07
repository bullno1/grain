// Probe pass entry point: the render stage with every view transform set to
// identity, writing each particle's corners into a readback canvas instead of
// the screen. See grain_probe_system.
//
// The canvas is 8-bit RGBA, the one format every backend reads back, so each
// texel carries the four bytes of one float. A slot is a column of
// GRAIN_PROBE_ROWS texels: for each corner its x, y, z and age, then a marker
// row that lets the CPU tell which way up the rows came back.
#define GRAIN_PROBE 1
#include "grain/api.glsl"
#include "archetype/render.glsl"

// Location 14: reserved for grain, see render.vert.glsl. The module's own
// Varying() outputs are declared but never consumed; the probe fragment stage
// reads only this.
layout(location = 14) flat out uint grain_v_probe_bits;

#define GRAIN_PROBE_ROWS 17
#define GRAIN_PROBE_MARKER_ROW 16
#define GRAIN_PROBE_MARKER 0x47524149u

void main() {
	// One triangle per (slot, row)
	uint inst = uint(gl_InstanceIndex);
	uint lid = inst / uint(GRAIN_PROBE_ROWS);
	uint row = inst % uint(GRAIN_PROBE_ROWS);

	uint bits = GRAIN_PROBE_MARKER;
	if (row != uint(GRAIN_PROBE_MARKER_ROW)) {
		uint corner = row / 4u;
		uint component = row % 4u;
		grain_corner_index = int(corner);

		uint region = grain_load_draw_region(0u);
		uint gid = region * uint(grain_pool_size) + lid;

		ivec2 size = textureSize(grain_texture_0, 0);
		ivec2 texel = ivec2(int(gid) % size.x, int(gid) / size.x);

		ParticleAttrs particle = grain_load_ParticleAttrs(texel);
		ModuleParams params = grain_load_ModuleParams(region);
		grain_SystemClock clock = grain_load_SystemClock(region);

		grain_Schedule sch = grain_observe(particle.grain_birth, clock);

		grain_srand(gid, floatBitsToUint(particle.grain_birth));

		Ctx ctx;
		ctx.frame_dt = clock.dt;
		ctx.dt = sch.emit ? sch.age : clock.dt;
		ctx.time = sch.emit ? sch.birth : clock.elapsed;
		grain_system_transform = grain_clock_transform(clock);
		ctx.transform = grain_system_transform;

		// xyz: corner position in world space; w: age, negative when the slot
		// holds nothing the renderer would draw
		vec4 probe = vec4(0.0, 0.0, 0.0, -1.0);
		if (sch.started) {
			cull();
			process(particle, params, ctx);
			// cull() leaves w at zero; anything else is what the renderer would
			// have rasterized, already in world space since the view transforms
			// are identity
			if (gl_Position.w != 0.0) {
				probe = vec4(gl_Position.xyz / gl_Position.w, sch.age);
			}
		}
		bits = floatBitsToUint(probe[component]);
	}
	grain_v_probe_bits = bits;

	// Cover texel (lid, row) of the pool_size x GRAIN_PROBE_ROWS canvas with
	// one triangle. Legs of 1.5 texels from the texel's corner take in its
	// center and no neighbour's.
	vec2 texel_size = vec2(2.0 / float(grain_pool_size), 2.0 / float(GRAIN_PROBE_ROWS));
	vec2 origin = vec2(-1.0, -1.0) + vec2(float(lid), float(row)) * texel_size;
	vec2 leg = vec2(
		gl_VertexIndex == 1 ? 1.5 : 0.0,
		gl_VertexIndex == 2 ? 1.5 : 0.0
	);
	gl_Position = vec4(origin + leg * texel_size, 0.0, 1.0);
}
