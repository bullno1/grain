Affector(Bounce)

// Bounces particles off a horizontal floor (y up)

Requires(
	vec3 position;
	vec3 velocity;
)

Params(
	@range(step = 1)
	float floor;

	@range(min = 0, max = 1, step = 0.01)
	float bounciness;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.position.y < params.floor) {
		particle.position.y = params.floor;
		particle.velocity = deflect(particle.velocity, vec3(0.0, 1.0, 0.0), params.bounciness);
	}
}
