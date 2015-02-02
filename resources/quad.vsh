@declare #extension GL_ARB_explicit_attrib_location: require
@declare layout(location = 0) in vec2 aPos;
@declare uniform mat4x4 uMVP;

float alive = float(particle.life > 0.0);
gl_Position = uMVP * vec4(aPos * 4.0 + particle.position, 0.0, alive);
