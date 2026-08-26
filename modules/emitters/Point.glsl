Emitter(Point)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@position
	vec2 position;

	@range(min=0, step=0.1)
	float min_speed;
	float max_speed;

	@range(step = 0.0174533)
	@arc(at=position, to=max_angle, inner=min_speed, outer=max_speed)
	float min_angle;
	@range(step = 0.0174533)
	float max_angle;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position = params.position;
	float speed = rand_range(params.min_speed, params.max_speed);
	float angle = rand_range(params.min_angle, params.max_angle);
	particle.velocity = unit_vec(angle) * speed;
}
