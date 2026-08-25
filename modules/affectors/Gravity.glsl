Module(Gravity)

Requires(
	vec2 velocity;
)

Params(
	@range(min = 0)
	float gravity;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.velocity.y -= params.gravity * ctx.dt;
}
