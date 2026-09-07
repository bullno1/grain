Emitter(Burst)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@position
	vec2 position;

	// Speeds are biased toward max_speed so the shell reads as a ring with
	// a sparser core
	@range(min = 0, step = 1)
	float min_speed;
	@range(min = 0, step = 1)
	float max_speed;

	// Extra velocity every spark inherits, e.g. the rocket's leftover motion
	@vector(scale = 1)
	vec2 drift;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position = params.position;
	float speed = mix(params.min_speed, params.max_speed, sqrt(rand()));
	particle.velocity = unit_vec(rand() * TAU) * speed + params.drift;
}
