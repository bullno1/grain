float init_seed()
{
	return 0.0;
}

float rand_(inout float _seed)
{
	_seed += 1.0;
	return fract(sin(dot(vec2(gl_FragCoord.x * _seed, gl_FragCoord.y * _time), vec2(12.9898, 78.233))) * 43758.5453);
}

float random_range_(inout float _seed, float lower, float upper)
{
	return lower + rand_(_seed) * (upper - lower);
}

#define rand() rand_(_seed)
#define random_range(lower, upper) random_range_(_seed, lower, upper)
#define select(condition, ifTrue, ifFalse) mix(ifFalse, ifTrue, float(condition))
