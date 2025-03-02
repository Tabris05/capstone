#version 460

#extension GL_EXT_maximal_reconvergence : require

#include "utils.glsl"

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform samplerCube skyboxTex;

void main() {
	fragColor = textureLod(skyboxTex, inPosition, 0.0f);
}