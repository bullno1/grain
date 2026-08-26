// The public builtin API: everything a user module may reference. This file is
// included before user modules
#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

#define GRAIN_SAMPLER_SET 0
#define GRAIN_UNIFORM_SET 1
// Locations 14-15 are reserved for grain's own varyings (see render.vert.glsl).
#define Varying(X) layout(location = X) out

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

#define GRAIN_SAMPLER_SET 2
#define GRAIN_UNIFORM_SET 3
// Locations 14-15 are reserved for grain's own varyings (see render.frag.glsl).
#define Varying(X) layout(location = X) in

#endif

// GLSL ES 3.00 has only the 2x16 pack/unpack family, the 4x8 variants are ES
// 3.10, so the GLES backend needs them spelled out. They are defined under
// cf_ names and the real names redirected onto them: redefining a name the
// driver may itself expose as a builtin is an overload conflict on some ES
// drivers, while a cf_ name can never collide.
#ifdef CF_GLES

uint cf_packUnorm4x8(vec4 v) {
	uvec4 p = uvec4(round(clamp(v, 0.0, 1.0) * 255.0));
	return p.x | (p.y << 8) | (p.z << 16) | (p.w << 24);
}

vec4 cf_unpackUnorm4x8(uint u) {
	return vec4(u & 0xFFu, (u >> 8) & 0xFFu, (u >> 16) & 0xFFu, u >> 24) / 255.0;
}

uint cf_packSnorm4x8(vec4 v) {
	ivec4 p = ivec4(round(clamp(v, -1.0, 1.0) * 127.0));
	return (uint(p.x) & 0xFFu) | ((uint(p.y) & 0xFFu) << 8) | ((uint(p.z) & 0xFFu) << 16) | ((uint(p.w) & 0xFFu) << 24);
}

vec4 cf_unpackSnorm4x8(uint u) {
	ivec4 p = ivec4(u << 24, u << 16, u << 8, u) >> 24;
	return clamp(vec4(p) / 127.0, -1.0, 1.0);
}

#define packUnorm4x8   cf_packUnorm4x8
#define unpackUnorm4x8 cf_unpackUnorm4x8
#define packSnorm4x8   cf_packSnorm4x8
#define unpackSnorm4x8 cf_unpackSnorm4x8

#endif

struct Ctx {
	float dt;
	float frame_dt;
	float time;
};

uint grain_rng_state;

uint grain_pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand() {
	grain_rng_state = grain_pcg(grain_rng_state);
	return float(grain_rng_state) * (1.0 / 4294967296.0);
}

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

vec2 quad() {
	vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);  // [0,1]
	return corner - 0.5; // [-0.5, 0.5]
}

void cull() {
	gl_Position = vec4(2.0, 2.0, 2.0, 0.0);
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

vec4 grain_Color;

#endif
