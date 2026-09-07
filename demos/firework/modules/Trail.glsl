Emitter(Trail)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	// Where the rocket is now and how it moves. The CPU updates these every
	// frame while the rocket rises
	@position
	vec2 position;

	@vector(scale = 1)
	vec2 velocity;

	// Spawn radius around the rocket
	@range(min = 0, step = 0.1)
	float spread;

	// Spark speed away from the rocket
	@range(min = 0, step = 1)
	float min_speed;
	@range(min = 0, step = 1)
	float max_speed;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// A particle born partway through the frame is `ctx.dt` old already: place
	// it where the rocket was at that instant so the trail stays continuous
	// instead of clumping once per frame
	vec2 origin = params.position - params.velocity * ctx.dt;
	float angle = rand() * TAU;
	particle.position = origin + unit_vec(angle) * sqrt(rand()) * params.spread;
	// Sparks tumble out the back, away from the direction of travel
	vec2 back = -normalize(params.velocity + vec2(0.0, 1e-3));
	particle.velocity = rotate(back, rand_range(-0.6, 0.6)) * rand_range(params.min_speed, params.max_speed);
}
