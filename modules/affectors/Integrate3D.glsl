Affector(Integrate3D)

Requires(
	vec3 position;
	vec3 velocity;
)

Params(
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position += particle.velocity * ctx.dt;
}
