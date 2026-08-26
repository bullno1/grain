Affector(Wind)

Requires(
	vec2 velocity;
)

Params(
	@vector(scale = 1)
	vec2 velocity;

	@range(min = 0, step = 0.1)
	float drag;

	@range(min = 0, max = 1, step = 0.01)
	float gustiness;
	@range(min = 0, step = 0.1)
	float gust_frequency;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// rand() is reseeded per particle each frame, so this is a stable phase
	float phase = rand() * TAU;
	float gust = sin(ctx.time * params.gust_frequency + phase) * 0.6
	           + sin(ctx.time * params.gust_frequency * 2.33 + phase * 1.7) * 0.4;
	vec2 wind = params.velocity * (1.0 + params.gustiness * gust);
	particle.velocity += (wind - particle.velocity) * min(params.drag * ctx.dt, 1.0);
}
