Affector(CircleDeflector)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@position
	vec2 position;

	@radius(at = position)
	@range(min = 0, step = 0.1)
	float radius;

	@range(min = 0, max = 1, step = 0.01)
	float bounciness;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	vec2 d = particle.position - params.position;
	float dist = length(d);
	if (dist >= params.radius || dist <= 0.0) { return; }

	vec2 n = d / dist;
	// Project back onto the surface so particles never render inside
	particle.position = params.position + n * params.radius;
	particle.velocity = deflect(particle.velocity, n, params.bounciness);
}
