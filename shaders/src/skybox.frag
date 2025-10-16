#version 460

#include "extensions.glsl"
#include "utils.glsl"

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform samplerCube skyboxTex;

void main() {
	fragColor = textureLod(skyboxTex, inPosition, 0.0f);
}