#define rand() _gr_rand(_gr_seed)
#define random_range(lower, upper) mix(lower, upper, rand())
#define select(condition, ifTrue, ifFalse) mix(ifFalse, ifTrue, float(condition))

float _gr_noise(vec2 co)
{
	return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float _gr_init_seed()
{
	return _gr_noise(vec2(_time, _gr_noise(gl_FragCoord.xy)));
}

float _gr_rand(inout float seed)
{
	seed = _gr_noise(vec2(gl_FragCoord.x * seed, gl_FragCoord.y * _time));
	return seed;
}
