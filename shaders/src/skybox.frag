#version 460

#include "extensions.glsl"
#include "texture_heap.glsl"
#include "types.glsl"
#include "utils.glsl"

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 fragColor;

layout(push_constant, scalar) uniform constants {
    mat4 camMatrix;
    u32 skyboxTex;
} pcs;

void main() {
	if((pcs.skyboxTex & ((1 << 20) - 1)) == ((1 << 20) - 1)) {
		fragColor = vec4(0.0f);
	}
	else {
		fragColor = textureLod(UNPACK_TEX_CUBE(pcs.skyboxTex), inPosition, 0.0f);
	}
}