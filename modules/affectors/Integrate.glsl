Module(Integrate)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position += particle.velocity * ctx.dt;
}
