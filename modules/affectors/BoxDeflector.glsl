Affector(BoxDeflector)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@position
	vec2 position;

	@extent(at = position)
	vec2 size;

	@range(min = 0, max = 1, step = 0.01)
	float bounciness;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	vec2 d = particle.position - params.position;
	vec2 half_size = params.size * 0.5;
	vec2 overlap = half_size - abs(d);
	if (overlap.x <= 0.0 || overlap.y <= 0.0) { return; }

	// Resolve along the axis of least penetration
	if (overlap.x < overlap.y) {
		float s = d.x < 0.0 ? -1.0 : 1.0;
		particle.position.x = params.position.x + s * half_size.x;
		particle.velocity = deflect(particle.velocity, vec2(s, 0.0), params.bounciness);
	} else {
		float s = d.y < 0.0 ? -1.0 : 1.0;
		particle.position.y = params.position.y + s * half_size.y;
		particle.velocity = deflect(particle.velocity, vec2(0.0, s), params.bounciness);
	}
}
