#include "internal/builtins.glsl"
#include "archetype/render.glsl"

layout (set = GRAIN_UNIFORM_SET, binding = 0) uniform uniform_block {
	uint grain__pool_size;
};

void main() {
	uint inst   = uint(gl_InstanceIndex);
	uint packed = inst / grain__pool_size;   // position in the culled draw list
	uint lid    = inst % grain__pool_size;   // slot within the system's pool
	uint region = grain__draw_list[packed / 4][packed % 4];  // which texture region it occupies

	uint flat   = region * grain__pool_size + lid;

	ivec2 size  = textureSize(grain__texture_0, 0);
	ivec2 texel = ivec2(int(flat) % size.x, int(flat) / size.x);

	ParticleAttrs particle = grain__load_ParticleAttrs(texel);
	ModuleParams params = grain__load_ModuleParams(region);
	SystemClock clock = grain__load_SystemClock(region);

	Schedule sch = schedule(lid, grain__pool_size, clock);
	uint gen = sch.gen + clock.gen_base;

	srand(flat, gen);

	Ctx ctx;
	ctx.frame_dt = clock.dt;
	ctx.dt = sch.emit ? sch.age : clock.dt;
	ctx.time = clock.elapsed - (sch.emit ? (clock.dt - sch.age) : 0.0);

	if (sch.started) {
		process(particle, params, ctx);
	} else {
		gl_Position = vec4(2.0, 2.0, 2.0, 0.0);
	}
}
