Emitter(Point)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	vec2 position;

	@range(min=0, step=0.1)
	float min_speed;
	float max_speed;
	float min_angle;
	float max_angle;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position = params.position;
	float speed = mix(params.min_speed, params.max_speed, rand());
	float angle = mix(params.min_angle, params.max_angle, rand());
	particle.velocity = vec2(cos(angle), sin(angle)) * speed;
}
