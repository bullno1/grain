Emitter(Cone)

Requires(
	vec3 position;
	vec3 velocity;
)

Params(
	@position
	vec3 position;

	@direction(at = position, length = 64)
	vec3 axis;

	@range(min = 0, max = 3.1415927, step = 0.0174533)
	@cone(at = position, axis = axis, inner = min_speed, outer = max_speed)
	float spread;

	@range(min = 0, step = 0.1)
	float min_speed;
	float max_speed;
)

void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {
	particle.position = to_world(params.position);

	vec3 axis = dot(params.axis, params.axis) > 0.0
		? normalize(to_world_dir(params.axis))
		: normalize(to_world_dir(vec3(0.0, 1.0, 0.0)));

	// Uniform over the spherical cap within `spread` of the axis: uniform in
	// the cosine of the angle off the axis, uniform in the turn around it
	float cos_angle = mix(cos(params.spread), 1.0, rand());
	float sin_angle = sqrt(max(0.0, 1.0 - cos_angle * cos_angle));
	vec2 rim = unit_vec(rand() * TAU) * sin_angle;

	// Orthonormal basis around the axis
	vec3 helper = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(helper, axis));
	vec3 bitangent = cross(axis, tangent);
	vec3 dir = tangent * rim.x + bitangent * rim.y + axis * cos_angle;

	particle.velocity = dir * rand_range(params.min_speed, params.max_speed);
}
