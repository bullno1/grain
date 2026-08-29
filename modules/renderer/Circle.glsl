Renderer(Circle)

Requires(
	vec2 position;
	float lifetime;
)

Params(
	@range(min = 0, step = 0.1)
	float radius;

	@range(min = 0, step = 0.1)
	float glow;

	@color
	uint start_color;

	@color
	uint end_color;
)

Varying(0) vec2 v_p;
Varying(1) vec2 v_half_size;

// The glow skirt never reaches zero: pad the quad by several falloffs and
// window the skirt to zero at its edge (plus a small AA margin for the rim)
const float GLOW_PAD = 3.0;

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime > 0.0) {
		v_half_size = vec2(params.radius + params.glow * GLOW_PAD) * 1.05;
		v_p = sdf_quad(v_half_size);
		gl_Position = grain_transform * vec4(particle.position + v_p, 0.0, 1.0);
	} else {
		cull();
	}
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	float d = sd_circle(v_p, params.radius);

	vec2 q = clamp(abs(v_p) / v_half_size, 0.0, 1.0);
	float window = (1.0 - q.x * q.x) * (1.0 - q.y * q.y);

	// Crisp AA disc; the glow skirt takes over past the rim
	float a = max(
		sdf_mask(d),
		sdf_glow(d, max(params.glow, 0.0001)) * window
	);

	vec4 c = unpackUnorm4x8(params.start_color);
	vec4 c2 = unpackUnorm4x8(params.end_color);
	vec4 target_color = mix(c2, c, particle.lifetime / 6.0);
	target_color.a *= a;
	grain_Color = premultiply(target_color);
	grain_Color.a = 0.0;
}

#endif
