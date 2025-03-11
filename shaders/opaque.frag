#version 460

#include "extensions.glsl"
#include "utils.glsl"

layout(location = 0) in vec4 inPositionLight;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec2 inUV;
layout(location = 6) flat in i32 inMaterialIndex;

layout(location = 0) out vec4 fragColor;

#include "pbr.glsl"

void main() {
    vec3 view = normalize(pcs.cameraPosition - inPosition);
    Material mat = pcs.materialBuffer.materials[inMaterialIndex];

    PBRMaterial pbr = getPBRMaterial(mat, inUV);
    vec3 outputColor = directionalLight(view, pcs.lightAngle, inPositionLight.xyz / inPositionLight.w, pcs.lightColor.rgb * pcs.lightColor.a, pbr) + ambientLight(view, pbr) + pbr.emission;

    fragColor = vec4(outputColor, 1.0f);
}