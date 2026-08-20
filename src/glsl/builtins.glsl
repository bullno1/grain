struct Ctx {
	float dt;
	float frameDt;
	float time;
};

float rand() {
	return 0.0; // TODO: implement
}

void destroy() {
}

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

vec2 quad() {
	vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);  // [0,1]
	return corner - 0.5; // [-0.5, 0.5]
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

vec4 grain_Color;

#endif
