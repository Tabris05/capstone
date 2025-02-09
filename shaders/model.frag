#version 460

layout(location = 0) in vec3 inNormal;

layout(location = 0) out vec4 fragColor;

void main() {
    //fragColor = vec4(pow(vec3(dot(normalize(inNormal), vec3(0.0f, 0.0f, -1.0f))), vec3(1.0f / 2.2f)), 1.0f);
    fragColor = vec4(1.0f);
}