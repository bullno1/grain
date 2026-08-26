Renderer(Quad)

Requires(
	vec2 position;
	float lifetime;
)

Params(
	@range(step=0.1)
	@extent
	vec2 size;
	@color
	uint color;
)

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime > 0.0) {
		gl_Position = grain_transform * vec4(particle.position + quad() * params.size, 0.0, 1.0);
	} else {
		cull();
	}
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// The color param is straight alpha while blending is premultiplied
	vec4 color = unpackUnorm4x8(params.color);
	grain_Color = vec4(color.rgb * color.a, color.a)
		* clamp(particle.lifetime, 0.0, 1.0);
}

#endif
