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

// GLSL ES 3.00 lacks the 4x8 pack/unpack family. Defined under cf_ names and
// redirected, since redefining a possible builtin is an overload conflict on
// some ES drivers
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
	mat4  transform;  // the system's local-to-world matrix, see grain_set_transform
};

const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;

// The current system's local-to-world matrix, the same value as ctx.transform;
// grain assigns it before process() runs so the helpers below need no argument.
mat4 grain_system_transform;

// Local -> world for points: applies the system's full transform.
// The 2D overloads work in the z = 0 plane.
vec3 to_world(vec3 p) {
	return (grain_system_transform * vec4(p, 1.0)).xyz;
}

vec2 to_world(vec2 p) {
	return to_world(vec3(p, 0.0)).xy;
}

// Local -> world for directions and offsets: rotation and scale only, no translation
vec3 to_world_dir(vec3 d) {
	// w = 0 drops the translation; cute-spirv has no mat3(mat4) constructor
	return (grain_system_transform * vec4(d, 0.0)).xyz;
}

vec2 to_world_dir(vec2 d) {
	return to_world_dir(vec3(d, 0.0)).xy;
}

// Maps a unit UV into a binding's atlas rect
vec2 atlas_uv(vec4 uvrect, vec2 uv) {
	return mix(uvrect.xy, uvrect.zw, uv);
}

vec2 unit_vec(float angle) {
	return vec2(cos(angle), sin(angle));
}

// Azimuth in the XY plane, elevation toward +z:
// unit_vec(a, 0.0) == vec3(unit_vec(a), 0.0)
vec3 unit_vec(float azimuth, float elevation) {
	return vec3(unit_vec(azimuth) * cos(elevation), sin(elevation));
}

vec2 rotate(vec2 v, float angle) {
	float c = cos(angle);
	float s = sin(angle);
	return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// 3D overload: rotates v around a unit axis (Rodrigues). Around +z it matches
// the 2D form: rotate(vec3(v, 0.0), vec3(0.0, 0.0, 1.0), a) == vec3(rotate(v, a), 0.0)
vec3 rotate(vec3 v, vec3 axis, float angle) {
	float c = cos(angle);
	float s = sin(angle);
	return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// Converts a straight-alpha color for premultiplied-alpha blending
vec4 premultiply(vec4 color) {
	return vec4(color.rgb * color.a, color.a);
}

// Bounces velocity off a surface with normal n, only when moving into it
vec2 deflect(vec2 velocity, vec2 n, float bounciness) {
	float vn = dot(velocity, n);
	return vn < 0.0 ? velocity - (1.0 + bounciness) * vn * n : velocity;
}

vec3 deflect(vec3 velocity, vec3 n, float bounciness) {
	float vn = dot(velocity, n);
	return vn < 0.0 ? velocity - (1.0 + bounciness) * vn * n : velocity;
}

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

float rand_range(float lo, float hi) {
	return mix(lo, hi, rand());
}

#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX

// Which quad corner this invocation computes: the vertex index in a render
// pass, set explicitly per instance by the probe pass
#ifdef GRAIN_PROBE
int grain_corner_index;
#define GRAIN_CORNER_INDEX grain_corner_index
#else
#define GRAIN_CORNER_INDEX gl_VertexIndex
#endif

vec2 quad() {
	vec2 corner = vec2(GRAIN_CORNER_INDEX & 1, (GRAIN_CORNER_INDEX >> 1) & 1);  // [0,1]
	return corner - 0.5; // [-0.5, 0.5]
}

// Unit UV for the current quad corner, y-down to match texture space
vec2 uv_quad() {
	vec2 corner = vec2(GRAIN_CORNER_INDEX & 1, (GRAIN_CORNER_INDEX >> 1) & 1);  // [0,1]
	return vec2(corner.x, 1.0 - corner.y);
}

void cull() {
	gl_Position = vec4(2.0, 2.0, 2.0, 0.0);
}

#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT

vec4 grain_Color;

// Pixel-art sampling under a linear filter, same as CF's smooth_uv: crisp
// texels with antialiased seams, degrading to plain linear once texels shrink
// below a screen pixel
vec2 smooth_uv(vec2 uv, vec2 texture_size) {
	vec2 pixel = uv * texture_size;
	vec2 seam = floor(pixel + 0.5);
	pixel = seam + clamp((pixel - seam) / fwidth(pixel), -0.5, 0.5);
	return pixel / texture_size;
}

// Also keeps the result inside uvrect by half a texel, so a sprite's edge
// clamps to its own texels instead of bleeding into its atlas neighbors
vec2 smooth_uv(vec2 uv, vec4 uvrect, vec2 texture_size) {
	uv = smooth_uv(uv, texture_size);
	vec2 half_texel = 0.5 / texture_size;
	return clamp(uv, uvrect.xy + half_texel, uvrect.zw - half_texel);
}

// texture(image, atlas_uv(image_uvrect, uv)) for pixel art: the unit uv is
// mapped into the atlas rect, smoothed, and clamped to the rect
vec4 texture_smooth(sampler2D image, vec4 uvrect, vec2 uv) {
	vec2 texture_size = vec2(textureSize(image, 0));
	return texture(image, smooth_uv(atlas_uv(uvrect, uv), uvrect, texture_size));
}

#endif

#include "grain/sdf.glsl"
