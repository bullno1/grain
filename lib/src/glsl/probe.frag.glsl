// Probe pass fragment stage: spreads the float bits the vertex stage captured
// over the four 8-bit channels of the texel it covers, little-endian. See
// probe.vert.glsl.
layout(location = 14) flat in uint grain_v_probe_bits;

layout(location = 0) out vec4 result;

void main() {
	uint bits = grain_v_probe_bits;
	// Integers 0..255 over 255 land exactly on the UNORM8 steps
	result = vec4(
		float(bits & 0xFFu),
		float((bits >> 8u) & 0xFFu),
		float((bits >> 16u) & 0xFFu),
		float(bits >> 24u)
	) / 255.0;
}
