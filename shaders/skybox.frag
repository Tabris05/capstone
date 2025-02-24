#version 460

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform samplerCube skyboxTex;

void main() {
	fragColor = vec4(pow(textureLod(skyboxTex, inPosition, 0.0f).rgb, vec3(1.0f / 2.2f)), 1.0f);
}