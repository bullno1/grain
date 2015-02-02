@declare uniform mat4x4 uMVP;

float alive = float(particle.life > 0.0);
gl_Position = uMVP * vec4(particle.position.xy, 0.0, alive);
