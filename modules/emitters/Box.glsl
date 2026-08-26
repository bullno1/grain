Emitter(Box)

Requires(
	vec2 position;
	vec2 velocity;
)

Params(
	@position
	vec2 position;

	@extent(at = position)
	vec2 size;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position = params.position + (vec2(rand(), rand()) - 0.5) * params.size;
	particle.velocity = vec2(0.0);
}
