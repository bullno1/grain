Emitter(Lifetime)

Requires(
	float lifetime;
)

Params(
	@range(min=0, step=0.1)
	float min_lifetime;
	float max_lifetime;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.lifetime = rand_range(params.min_lifetime, params.max_lifetime);
}
