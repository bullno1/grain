Renderer(Sprite)

Requires(
	vec2 position;
	float lifetime;
)

Params(
	@range(step=0.1)
	vec2 size;
)

Samplers(
	sampler2D image;
)

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

Varying(2) vec2 v_uv;

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime > 0.0) {
		vec2 corner = quad();
		// The atlas is y-down while the quad is y-up
		v_uv = vec2(corner.x + 0.5, 0.5 - corner.y);
		gl_Position = grain_transform * vec4(particle.position + corner * params.size, 0.0, 1.0);
	} else {
		cull();
	}
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

Varying(2) vec2 v_uv;

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	grain_Color = texture(image, atlas_uv(image_uvrect, v_uv));
	grain_Color.a *= clamp(particle.lifetime, 0.0, 1.0);
}

#endif
