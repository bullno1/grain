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

	@range(min = 0, max = 1, step = 0.01)
	float flicker;

	@color
	uint hot_color;
	@color
	uint cool_color;

	@range(min = 0, max = 1, step = 0.01)
	float additivity;
)

Varying(0) vec2 v_p;
Varying(1) float v_t;
Varying(2) vec2 v_half_size;

// The glow skirt never reaches zero, so the quad is padded and the emission
// windowed to zero at its edge
const float GLOW_PAD = 1.5;

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime > 0.0) {
		// Normalized age, assuming lifetimes are around `duration`
		v_t = clamp(1.0 - particle.lifetime / params.duration, 0.0, 1.0);
		// Grow in over the first 15% of life so spawns don't pop
		float birth = smoothstep(0.0, 0.15, v_t);
		v_half_size = params.size * 0.5
			* mix(1.0, params.end_scale, v_t)
			* mix(0.3, 1.0, birth);
		v_p = sdf_quad(v_half_size * GLOW_PAD);
		gl_Position = grain_transform * vec4(particle.position + v_p, 0.0, 1.0);
	} else {
		cull();
	}
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	float w = v_half_size.x;
	float h = v_half_size.y;

	// Sideways wobble, stronger toward the tip; rand() is stable per particle
	vec2 p = v_p;
	float lift = p.y / h * 0.5 + 0.5;
	p.x += sin(ctx.time * 12.0 + rand() * TAU + p.y * 3.0 / h)
	     * params.flicker * lift * w;

	// One soft vertical blob per particle: the flame silhouette and the
	// white-hot base come from the additive sum of many, not from one shape
	float d = sd_circle(vec2(p.x, p.y * 0.7), w * 0.45);
	float core  = sdf_glow(d + w * 0.25, w * 0.15);
	float flare = sdf_glow(d, w * 0.7);

	// Window the skirt to zero at the padded quad's edge
	vec2 q = clamp(abs(v_p) / (v_half_size * GLOW_PAD), 0.0, 1.0);
	float window = (1.0 - q.x * q.x) * (1.0 - q.y * q.y);

	// Color alphas act as per-lobe intensity dials; keep them low so the
	// core only saturates to white where particles stack
	vec4 hot  = premultiply(unpackUnorm4x8(params.hot_color));
	vec4 cool = premultiply(unpackUnorm4x8(params.cool_color));
	vec4 c;
	c.rgb = (hot.rgb * core * 0.35 + cool.rgb * flare * 0.20)
	      * window * (1.0 - v_t * 0.7);
	c.a = smoothstep(0.0, 0.15, v_t)                 // fade in
	    * clamp(particle.lifetime * 4.0, 0.0, 1.0);  // fade out
	grain_Color = premultiply(c);
	// Additive blending is order-independent: at additivity 1 the stacked
	// flames at the emitter sum the same way no matter the draw order
	grain_Color.a *= 1.0 - params.additivity;
}

#endif
