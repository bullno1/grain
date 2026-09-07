// Probe pass fragment stage: stores what the vertex stage captured into the
// readback canvas texel it covers. See probe.vert.glsl.
layout(location = 14) flat in vec4 grain_v_probe;

layout(location = 0) out vec4 result;

void main() {
	result = grain_v_probe;
}
