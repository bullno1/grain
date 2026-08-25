#include "internal/builtins.glsl"

#define Emitter(X) layout(set = 0, binding = 0) uniform Grain__Inspect_Emitter { float X; } Emitter;
#define Affector(X) layout(set = 0, binding = 3) uniform Grain__Inspect_Affector { float X; } Affector;
#define Renderer(X) layout(set = 0, binding = 4) uniform Grain__Inspect_Renderer { float X; } Renderer;
#define Requires(X) struct ParticleAttrs { X float grain__ignore; }; layout(set = 0, binding = 1) uniform Grain__Inspect_Requires {X float grain__ignore;} Requires;
#define Params(X) struct ModuleParams { X float grain__ignore; }; layout(set = 0, binding = 2) uniform Grain__Inspect_Params {X float grain__ignore;} Params;

mat4 grain_transform;

#include "module.glsl"

void main() {
	ParticleAttrs particle;
	ModuleParams params;
	Ctx ctx;
	process(particle, params, ctx);
}
