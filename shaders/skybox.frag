#version 460

#include "utils.glsl"

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform samplerCube skyboxTex;

void main() {
	fragColor = agx(textureLod(skyboxTex, inPosition, 0.0f).rgb);
}