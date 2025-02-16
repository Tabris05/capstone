#version 460

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D imageHeap[];

void main() {
    fragColor = vec4(pow(texture(imageHeap[0], inUV).rgb * vec3(dot(normalize(inNormal), vec3(0.0f, 0.0f, -1.0f))), vec3(1.0f / 2.2f)), 1.0f);
}