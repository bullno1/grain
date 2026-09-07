Affector(Gravity3D)

Requires(
	vec3 velocity;
)

Params(
	@range(step = 1)
	@vector(scale = 0.25)
	vec3 gravity;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.velocity += params.gravity * ctx.dt;
}
