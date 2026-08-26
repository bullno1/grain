Affector(Flutter)

Requires(
	vec2 velocity;
)

Params(
	@range(min = 0, step = 0.1)
	float strength;

	@range(min = 0, step = 0.1)
	float frequency;

	@range(min = 0, max = 1, step = 0.01)
	float variation;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// rand() is reseeded per particle each frame: stable phase/rate per flake
	float phase = rand() * 6.2831853;
	float freq  = params.frequency * mix(1.0, 0.5 + rand(), params.variation);
	float amp   = params.strength  * mix(1.0, 0.5 + rand(), params.variation);
	particle.velocity.x += sin(ctx.time * freq + phase) * amp * ctx.dt;
}
