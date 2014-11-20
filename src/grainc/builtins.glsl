float rand_(inout int _count)
{
	++_count;
	return fract(sin(dot(vec2(gl_FragCoord.x * _count, gl_FragCoord.y * _time), vec2(12.9898, 78.233))) * 43758.5453);
}

float random_range_(inout int _count, float lower, float upper)
{
	return lower + rand_(_count) * (upper - lower);
}

#define rand() rand_(_count)
#define random_range(lower, upper) random_range_(_count, lower, upper)
#define select(condition, ifTrue, ifFalse) mix(ifFalse, ifTrue, float(condition))
