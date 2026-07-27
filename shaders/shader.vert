#version 450
layout(set=0, binding=0) uniform UBO { mat4 proj; } u;
layout(location=0) in  vec2 inPos;
layout(location=1) in  vec4 inColor;
layout(location=0) out vec4 fragColor;
void main() {
    gl_Position = u.proj * vec4(inPos, 0.0, 1.0);
    fragColor   = inColor;
}