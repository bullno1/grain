Affector(Fan)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@position
	vec2 position;

	@range(min = 0, step = 0.1)
	float strength;

	@range(min = 0, step = 0.1)
	float min_range;
	@range(min = 0, step = 0.1)
	float max_range;

	@range(step = 0.0174533)
	@arc(at = position, to = max_angle, inner = min_range, outer = max_range)
	float min_angle;
	@range(step = 0.0174533)
	float max_angle;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	vec2 d = particle.position - params.position;
	float dist = length(d);
	if (dist <= 0.0 || dist >= params.max_range) { return; }

	float mid = (params.min_angle + params.max_angle) * 0.5;
	float half_spread = (params.max_angle - params.min_angle) * 0.5;
	vec2 dir = d / dist;
	if (half_spread < PI && dot(dir, unit_vec(mid)) < cos(half_spread)) {
			return;
	}

	float falloff = 1.0 - smoothstep(params.min_range, params.max_range, dist);
	particle.velocity += dir * params.strength * falloff * ctx.dt;
}
