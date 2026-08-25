Affector(Age)

Requires(
	float lifetime;
)

Params(
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.lifetime -= ctx.dt;
}
