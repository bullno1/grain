// Builtins reading the archetype's uniform block, hence included right after it.
// grain_transform is 2D world -> clip; grain_transform3d (world -> view) and
// grain_projection (view -> clip) are kept split so renderers can billboard
#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

// Clip position of a camera-facing quad corner; `offset` (typically
// quad() * size) is applied in view space
vec4 billboard(vec3 position, vec2 offset) {
	vec4 view_pos = grain_transform3d * vec4(position, 1.0);
	return grain_projection * (view_pos + vec4(offset, 0.0, 0.0));
}

#endif
