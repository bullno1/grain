Renderer(Flame)

Requires(
	vec2 position;
	float lifetime;
)

Params(
	@range(step = 0.1)
	vec2 size;

	@range(min = 0, step = 0.1)
	float duration;

	@range(min = 0, max = 2, step = 0.01)
	float end_scale;

	@color
	uint hot_color;
	@color
	uint cool_color;
)

Samplers(
	sampler2D image;
)

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

Varying(2) vec2 v_uv;
Varying(3) float v_t;

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime > 0.0) {
		// Normalized age, assuming lifetimes are around `duration`
		v_t = clamp(1.0 - particle.lifetime / params.duration, 0.0, 1.0);
		vec2 corner = quad();
		// The atlas is y-down while the quad is y-up
		v_uv = vec2(corner.x + 0.5, 0.5 - corner.y);
		vec2 size = params.size * mix(1.0, params.end_scale, v_t);
		gl_Position = grain_transform * vec4(particle.position + corner * size, 0.0, 1.0);
	} else {
		cull();
	}
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

Varying(2) vec2 v_uv;
Varying(3) float v_t;

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	vec4 tint = mix(unpackUnorm4x8(params.hot_color), unpackUnorm4x8(params.cool_color), v_t);
	grain_Color = texture(image, atlas_uv(image_uvrect, v_uv)) * tint;
	grain_Color.a *= clamp(particle.lifetime * 4.0, 0.0, 1.0);
}

#endif
