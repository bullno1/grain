Emitter(Lifetime)

Requires(
	float lifetime;
)

Params(
	float min_lifetime;
	float max_lifetime;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.lifetime = mix(params.min_lifetime, params.max_lifetime, rand());
}
