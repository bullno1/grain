@declare uniform mat4x4 uMVP;

float alive = float(life > 0.0);
gl_Position = uMVP * vec4(position.x, position.y, 0.0, alive);
