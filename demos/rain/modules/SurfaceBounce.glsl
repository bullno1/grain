Affector(SurfaceBounce)

Requires(
	vec2 position;
	vec2 velocity;
	float lifetime;
	float splash;
)

Params(
	vec2 world_min;
	vec2 world_max;

	// Largest speed the surface texture can encode in its RG channels
	@range(min = 0, step = 1)
	float max_surface_speed;

	@range(min = 0, max = 1, step = 0.01)
	float bounciness;

	// Random kick along the surface on impact
	@range(min = 0, step = 1)
	float scatter;

	// A drop that bounces upward lives at most this long, as a splash
	@range(min = 0, step = 0.01)
	float splash_lifetime;
)

Samplers(
	@filter(linear)
	@wrap(clamp)
	sampler2D surface;
)

// The drop may cross several texels in one frame: how finely to retrace its
// path back to the point of entry, and how many texels to push it out by
// when it is still embedded afterwards
const int BACKTRACK_STEPS = 8;
const int PUSH_OUT_STEPS = 4;

// The surface is a canvas covering world_min..world_max: alpha is coverage,
// RG its velocity with (0.5, 0.5) at rest. Texture row 0 is the top of the
// world, as with every CF canvas
vec4 sample_surface(vec2 p, ModuleParams params) {
	vec2 t = (p - params.world_min) / (params.world_max - params.world_min);
	vec2 uv = vec2(t.x, 1.0 - t.y);
	// Explicit LOD: this is sampled inside loops, where implicit derivatives
	// are undefined
	return textureLod(surface, atlas_uv(surface_uvrect, uv), 0.0);
}

bool inside(vec2 p, ModuleParams params) {
	return sample_surface(p, params).a >= 0.5;
}

float coverage(vec2 p, ModuleParams params) {
	return sample_surface(p, params).a;
}

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	// Splash droplets fly through everything
	if (particle.splash > 0.0) { return; }
	if (!inside(particle.position, params)) { return; }

	// Where the drop was when it hit: retrace this frame's motion
	vec2 p = particle.position;
	vec2 back = -particle.velocity * ctx.dt / float(BACKTRACK_STEPS);
	for (int i = 0; i < BACKTRACK_STEPS && inside(p, params); ++i) {
		p += back;
	}

	// Surface normal from the coverage gradient at the entry point. The
	// stencil is two texels wide so it still straddles the edge when the
	// retrace overshoots by a step
	vec2 texel = (params.world_max - params.world_min) / vec2(textureSize(surface, 0));
	vec2 dx = vec2(texel.x * 2.0, 0.0);
	vec2 dy = vec2(0.0, texel.y * 2.0);
	vec2 grad = vec2(
		coverage(p + dx, params) - coverage(p - dx, params),
		coverage(p + dy, params) - coverage(p - dy, params)
	);
	vec2 n = dot(grad, grad) > 1e-6 ? -normalize(grad) : vec2(0.0, 1.0);

	// Still embedded (the surface moved onto the drop): push out along the normal
	float step_size = max(texel.x, texel.y);
	for (int i = 0; i < PUSH_OUT_STEPS && inside(p, params); ++i) {
		p += n * step_size;
	}

	// Bounce relative to the moving surface
	vec2 surface_velocity =
		(sample_surface(particle.position, params).rg * 2.0 - 1.0) * params.max_surface_speed;
	vec2 relative = particle.velocity - surface_velocity;
	particle.position = p;
	if (dot(relative, n) >= 0.0) { return; }  // Already leaving

	vec2 tangent = vec2(n.y, -n.x);
	relative = deflect(relative, n, params.bounciness)
		+ tangent * rand_range(-params.scatter, params.scatter);
	particle.velocity = relative + surface_velocity;

	// Bounced upward: the rest of its life is a short-lived splash
	if (particle.velocity.y > 0.0) {
		particle.splash = params.splash_lifetime;
		particle.lifetime = min(particle.lifetime, params.splash_lifetime);
	}
}
