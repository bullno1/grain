Affector(VectorField)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@range(step=0.1)
	float strength;
	vec2 world_min;
	vec2 world_max;
)

Samplers(
	@filter(linear)
	@wrap(clamp)
	sampler2D field;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	vec2 uv = (particle.position - params.world_min) / (params.world_max - params.world_min);
	// RG encodes a direction: (0.5, 0.5) is zero force
	vec2 force = texture(field, atlas_uv(field_uvrect, uv)).xy * 2.0 - 1.0;
	particle.velocity += force * params.strength * ctx.dt;
}
