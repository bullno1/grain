Emitter(Trail)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	// The rocket's speed along its local +y axis. Its position and heading
	// are the system transform, which the CPU updates every frame; the speed
	// is only needed to back-date sub-frame births along the path
	@range(min = 0, step = 1)
	float speed;

	// Spawn radius around the rocket
	@range(min = 0, step = 0.1)
	float spread;

	// Spark speed away from the rocket
	@range(min = 0, max = max_speed, step = 1)
	float min_speed;
	@range(min = min_speed, step = 1)
	float max_speed;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// A particle born partway through the frame is `ctx.dt` old already:
	// place it where the rocket was at that instant, back down its local
	// path, so the trail stays continuous instead of clumping once per frame
	vec2 origin = vec2(0.0, -params.speed * ctx.dt);
	vec2 offset = unit_vec(rand() * TAU) * sqrt(rand()) * params.spread;
	particle.position = to_world(origin + offset);
	// Sparks tumble out the back, away from the direction of travel
	vec2 back = rotate(vec2(0.0, -1.0), rand_range(-0.6, 0.6));
	particle.velocity = to_world_dir(back * rand_range(params.min_speed, params.max_speed));
}
