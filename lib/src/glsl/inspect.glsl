#include "grain/api.glsl"

#define Emitter(X) layout(set = 0, binding = 0) uniform grain_Inspect_Emitter { float X; } Emitter;
#define Affector(X) layout(set = 0, binding = 3) uniform grain_Inspect_Affector { float X; } Affector;
#define Renderer(X) layout(set = 0, binding = 4) uniform grain_Inspect_Renderer { float X; } Renderer;
#define Requires(X) struct ParticleAttrs { X float grain_ignore; }; layout(set = 0, binding = 1) uniform grain_Inspect_Requires {X float grain_ignore;} Requires;
#define Params(X) struct ModuleParams { X float grain_ignore; }; layout(set = 0, binding = 2) uniform grain_Inspect_Params {X float grain_ignore;} Params;
#define Samplers(X)

// Stand-ins for the archetype uniform block, see transform.glsl
mat4 grain_transform;
mat4 grain_transform3d;
mat4 grain_projection;
#include "grain/transform.glsl"

// Generated declarations for the module's Samplers block
#include "grain/samplers.glsl"

#include "module.glsl"

// Included after the module so it stays invisible to user code
#include "grain/internal.glsl"

void main() {
	ParticleAttrs particle;
	ModuleParams params;
	Ctx ctx;
	process(particle, params, ctx);
}
