Renderer(Streak)

Requires(
	vec2 position;
	vec2 velocity;
	float lifetime;
	float splash;
)

Params(
	@range(min = 0, step = 0.1)
	float thickness;

	// Streak length per unit of speed, i.e. seconds of travel drawn
	@range(min = 0, step = 0.001)
	float stretch;

	@range(min = 0, step = 1)
	float max_length;

	@color
	uint color;

	// Splash: half-angle of the V, how fast its two arms fly apart, arm length
	@range(min = 0, max = 1.5707963, step = 0.01)
	float splash_angle;

	@range(min = 0, step = 1)
	float splash_spread;

	@range(min = 0, step = 0.1)
	float splash_length;
)

// Both arms as (start, end) relative to the particle. While falling they
// coincide: one streak trailing the drop
Varying(0) vec2 v_p;
Varying(1) vec4 v_arm0;
Varying(2) vec4 v_arm1;
Varying(3) float v_alpha;

// A falling drop fades out over its last moments instead of popping
const float FADE_TIME = 0.15;
// sd_segment divides by the segment length: never let one degenerate
const float MIN_LENGTH = 0.01;

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	if (particle.lifetime <= 0.0) {
		cull();
		return;
	}

	vec4 arm0, arm1;
	float alpha;
	if (particle.splash <= 0.0) {
		// Falling: a streak pointing back along the velocity, longer the faster
		float speed = length(particle.velocity);
		vec2 dir = speed > 0.0 ? particle.velocity / speed : vec2(0.0, -1.0);
		float len = max(min(speed * params.stretch, params.max_length), MIN_LENGTH);
		arm0 = vec4(vec2(0.0), -dir * len);
		arm1 = arm0;
		alpha = clamp(particle.lifetime / FADE_TIME, 0.0, 1.0);
	} else {
		// Splash: two droplets flying apart, a V opening upward from the
		// bounce point. The particle itself keeps moving on its bounced path
		float elapsed = particle.splash - particle.lifetime;
		float offset = params.splash_spread * elapsed;
		float len = max(params.splash_length, MIN_LENGTH);
		vec2 d0 = unit_vec(PI * 0.5 + params.splash_angle);
		vec2 d1 = unit_vec(PI * 0.5 - params.splash_angle);
		arm0 = vec4(d0 * offset, d0 * (offset + len));
		arm1 = vec4(d1 * offset, d1 * (offset + len));
		alpha = clamp(particle.lifetime / particle.splash, 0.0, 1.0);
	}

	// One quad bounding both arms, padded for the capsule radius plus AA
	float pad = params.thickness * 0.5 + 1.0;
	vec2 lo = min(min(arm0.xy, arm0.zw), min(arm1.xy, arm1.zw)) - pad;
	vec2 hi = max(max(arm0.xy, arm0.zw), max(arm1.xy, arm1.zw)) + pad;
	v_p = mix(lo, hi, quad() + 0.5);
	v_arm0 = arm0;
	v_arm1 = arm1;
	v_alpha = alpha;
	gl_Position = grain_transform * vec4(particle.position + v_p, 0.0, 1.0);
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	float r = params.thickness * 0.5;
	float d = min(
		sd_segment(v_p, v_arm0.xy, v_arm0.zw, r),
		sd_segment(v_p, v_arm1.xy, v_arm1.zw, r)
	);

	// The color param is straight alpha while blending is premultiplied
	vec4 c = unpackUnorm4x8(params.color);
	c.a *= sdf_mask(d) * v_alpha;
	grain_Color = premultiply(c);
}

#endif
