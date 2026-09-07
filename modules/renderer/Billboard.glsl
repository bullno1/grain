Renderer(Billboard)

Requires(
	vec3 position;
	float lifetime;
)

Params(
	@range(min = 0, step = 0.1)
	vec2 size;
	@color
	uint color;
)

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime > 0.0) {
		// Camera-facing: the corner offset is applied in view space
		gl_Position = billboard(particle.position, quad() * params.size);
	} else {
		cull();
	}
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// The color param is straight alpha while blending is premultiplied
	grain_Color = premultiply(unpackUnorm4x8(params.color))
		* clamp(particle.lifetime, 0.0, 1.0);
}

#endif
