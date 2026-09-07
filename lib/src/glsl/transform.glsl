// Public builtins that read the archetype's uniform block. The block is
// generated per archetype and declared after api.glsl, so anything using
// its members lives here and is included right after it (see grain.c and
// inspect.glsl), still before user code.
//
// Two transform families, mirroring CF's separate 2D and 3D draw APIs:
//
// * grain_transform     2D: CF's draw transform stack composed with the
//                        canvas projection; world -> clip in one matrix
// * grain_transform3d   3D: CF's draw3d view and transform stacks composed
//                        (view * model); world -> view space
// * grain_projection    3D: CF's draw3d projection; view -> clip
//
// The 3D pair is kept split so a renderer can offset in view space, which
// is what billboarding is.
#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

// Clip position of a camera-facing quad corner: `offset` (typically
// quad() * size) is applied in view space, so the quad faces the 3D camera
// whatever the view is
vec4 billboard(vec3 position, vec2 offset) {
	vec4 view_pos = grain_transform3d * vec4(position, 1.0);
	return grain_projection * (view_pos + vec4(offset, 0.0, 0.0));
}

#endif
