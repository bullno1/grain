// 2D signed distance helpers. Negative = inside.
// Composites made with min/max are exact on the boundary but only lower bounds
// inside; irrelevant for sdf_mask fills, matters for interior glow gradients.

float sd_circle(vec2 p, float r) {
	return length(p) - r;
}

float sd_box(vec2 p, vec2 half_size) {
	vec2 q = abs(p) - half_size;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// Capsule from a to b with the given radius
float sd_segment(vec2 p, vec2 a, vec2 b, float r) {
	vec2 pa = p - a, ba = b - a;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	return length(pa - ba * h) - r;
}

float op_union(float a, float b)     { return min(a, b); }
float op_intersect(float a, float b) { return max(a, b); }
float op_subtract(float a, float b)  { return max(a, -b); }

// Polynomial smooth union; k is the blend radius in world units
float op_smooth_union(float a, float b, float k) {
	float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
	return mix(b, a, h) - k * h * (1.0 - h);
}
float op_smooth_intersect(float a, float b, float k) { return -op_smooth_union(-a, -b, k); }
float op_smooth_subtract(float a, float b, float k)  { return -op_smooth_union(-a, b, k); }

// Hollow outline of thickness t
float op_shell(float d, float t) { return abs(d) - t; }
// Rounded corners: grow the shape by r
float op_round(float d, float r) { return d - r; }

// 1 inside the shape, exponential glow skirt outside; suits additive blending
float sdf_glow(float d, float falloff) {
	return exp(-max(d, 0.0) / falloff);
}

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

// World-space offset of this quad corner for a shape spanning ±half_extent.
// Assign to a varying and reuse in gl_Position:
//   v_p = sdf_quad(vec2(radius));
//   gl_Position = grain_transform * vec4(particle.position + v_p, 0.0, 1.0);
// Pad half_extent by the softness when using the soft sdf_mask.
vec2 sdf_quad(vec2 half_extent) {
	return quad() * 2.0 * half_extent;
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

// Coverage of a distance: 1 inside, fwidth-wide AA edge. Distances must be
// in world units (pass sdf_quad's return value through a varying).
float sdf_mask(float d) {
	return 1.0 - smoothstep(0.0, fwidth(d), d);
}

// Soft variant: fade starts `softness` inside the edge
float sdf_mask(float d, float softness) {
	return 1.0 - smoothstep(-softness, fwidth(d), d);
}

#endif
