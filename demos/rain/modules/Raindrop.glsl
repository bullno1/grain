Emitter(Raindrop)

Requires(
	vec2 velocity;
	float splash;
)

Params(
	@range(min = 0, step = 1)
	float min_speed;

	@range(min = 0, step = 1)
	float max_speed;

	// Sideways drift as a fraction of the fall speed
	@range(min = -1, max = 1, step = 0.01)
	float slant;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	float speed = rand_range(params.min_speed, params.max_speed);
	particle.velocity = normalize(vec2(params.slant, -1.0)) * speed;
	// A drop starts on its way down; SurfaceBounce turns it into a splash
	particle.splash = 0.0;
}
