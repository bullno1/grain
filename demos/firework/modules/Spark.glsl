Renderer(Spark)

Requires(
	vec2 position;
	float lifetime;
)

Params(
	@range(min = 0, step = 0.1)
	float radius;

	// Glow skirt falloff distance; 0 disables the glow
	@range(min = 0, step = 0.1)
	float glow;

	// Each spark picks a tone between the two at birth
	@color
	uint color;
	@color
	uint color2;

	// Fade out over the last `fade_time` seconds of life
	@range(min = 0, step = 0.05)
	float fade_time;

	// 0 = steady, 1 = fully flickering
	@range(min = 0, max = 1, step = 0.01)
	float twinkle;

	// 0 = alpha blend, 1 = additive; per pool render state stays premultiplied
	@range(min = 0, max = 1, step = 0.01)
	float additivity;
)

Varying(0) vec2 v_p;
Varying(1) vec2 v_half_size;
Varying(2) vec4 v_color;

// The glow skirt never reaches zero: pad the quad by several falloffs and
// window it to zero at the edge
const float GLOW_PAD = 3.0;

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime <= 0.0) {
		cull();
		return;
	}

	// rand() is reseeded per particle each frame: the tone and the flicker
	// phase are stable across a spark's life
	float tone = rand();
	float phase = rand() * TAU;
	float flicker = mix(1.0, 0.5 + 0.5 * sin(ctx.time * 40.0 + phase), params.twinkle);
	float fade = clamp(particle.lifetime / max(params.fade_time, 1e-3), 0.0, 1.0);

	v_color = mix(unpackUnorm4x8(params.color), unpackUnorm4x8(params.color2), tone);
	v_color.a *= fade * flicker;

	v_half_size = vec2(params.radius + params.glow * GLOW_PAD) * 1.05 + 1.0;
	v_p = sdf_quad(v_half_size);
	gl_Position = grain_transform * vec4(particle.position + v_p, 0.0, 1.0);
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	float d = sd_circle(v_p, params.radius);

	vec2 q = clamp(abs(v_p) / v_half_size, 0.0, 1.0);
	float window = (1.0 - q.x * q.x) * (1.0 - q.y * q.y);

	// Crisp AA disc; the glow skirt takes over past the rim
	float a = max(
		sdf_mask(d),
		sdf_glow(d, max(params.glow, 1e-4)) * window * 0.6
	);

	vec4 c = v_color;
	c.a *= a;
	grain_Color = premultiply(c);
	grain_Color.a *= 1.0 - params.additivity;
}

#endif
