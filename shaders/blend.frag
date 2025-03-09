#version 460

#extension GL_ARB_fragment_shader_interlock : require

#include "extensions.glsl"
#include "utils.glsl"

#include "../shared/vertex.h"
#include "../shared/material.h"
#include "../shared/oitnode.h"

layout(location = 0) in vec4 inPositionLight;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec2 inUV;
layout(location = 6) flat in i32 inMaterialIndex;

layout(set = 0, binding = 0) uniform sampler2D imageHeap[];
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube radianceMap;
layout(set = 1, binding = 2) uniform sampler2D brdfIntegralTex;
layout(set = 1, binding = 3) uniform sampler2DShadow shadowMapTex;

layout(buffer_reference, scalar) restrict coherent buffer OITBuffer {
    OITNode nodes[];
};

layout(buffer_reference, scalar) restrict readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, scalar) restrict readonly buffer MaterialBuffer {
    Material materials[];
};

layout(push_constant, scalar) uniform constants {
    OITBuffer oitBuffer;
    VertexBuffer vertexBuffer;
    MaterialBuffer materialBuffer;
    mat4 cameraTransform;
    mat4 lightTransform;
    mat4x3 modelTransform;
    vec4 lightColor;
    vec3 cameraPosition;
    vec3 lightAngle;
    u32 frameBufferWidth;
} pcs;

#include "pbr.glsl"

void swapNodes(inout OITNode a, inout OITNode b) {
    OITNode tmp = a;
    a = b;
    b = tmp;
}

layout(early_fragment_tests, pixel_interlock_ordered) in;
void main() {
    vec3 view = normalize(pcs.cameraPosition - inPosition);
    Material mat = pcs.materialBuffer.materials[inMaterialIndex];

    PBRMaterial pbr = getPBRMaterial(mat, inUV);
    vec3 outputColor = directionalLight(view, pcs.lightAngle, inPositionLight.xyz / inPositionLight.w, pcs.lightColor.rgb * pcs.lightColor.a, pbr) + ambientLight(view, pbr) + pbr.emission;

    uvec2 screenCoords = uvec2(gl_FragCoord.xy - vec2(0.5f));
    u32 baseIndex = (screenCoords.y * pcs.frameBufferWidth + screenCoords.x) * 4;
    OITNode cur = OITNode(packe5bgr9(vec4(outputColor, 1.0f)), packDepthTransmittance(gl_FragCoord.z, 1.0f - pbr.albedo.a));

    beginInvocationInterlockARB();

    for(u32 i = 0; i < 4; i++) {
        if(unpackDepth(pcs.oitBuffer.nodes[baseIndex + i].packedDepthTransmittance) < unpackTransmittance(cur.packedDepthTransmittance)) {
            swapNodes(pcs.oitBuffer.nodes[baseIndex + i], cur);
        }
    }
    if(cur.packedDepthTransmittance != 0) {
        OITNode last = pcs.oitBuffer.nodes[baseIndex + 3];
        f32 lastDepth = unpackDepth(last.packedDepthTransmittance);
        f32 lastTransmittance = unpackTransmittance(last.packedDepthTransmittance);
        pcs.oitBuffer.nodes[baseIndex + 3].packedColor = packe5bgr9(vec4(mix(unpacke5bgr9(last.packedColor).rgb, unpacke5bgr9(cur.packedColor).rgb, lastTransmittance), 1.0f));
        pcs.oitBuffer.nodes[baseIndex + 3].packedDepthTransmittance = packDepthTransmittance(lastDepth, lastTransmittance * unpackTransmittance(cur.packedDepthTransmittance));
    }

    endInvocationInterlockARB();
}