#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_maximal_reconvergence : require
#extension GL_ARB_fragment_shader_interlock : require

#include "../shared/vertex.h"
#include "../shared/material.h"
#include "../shared/oitnode.h"

#include "utils.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inUV;
layout(location = 5) flat in i32 inMaterialIndex;

layout(set = 0, binding = 0) uniform sampler2D imageHeap[];
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube radianceMap;
layout(set = 1, binding = 2) uniform sampler2D brdfIntegralTex;

layout(buffer_reference, scalar) restrict buffer OITBuffer {
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
    mat4 modelTransform;
    mat4 cameraTransform;
    mat3 normalTransform;
    vec3 cameraPosition;
    u32 frameBufferWidth;
} pcs;

#include "pbr.glsl"

void swapNodes(inout OITNode a, inout OITNode b) {
    OITNode tmp = a;
    a = b;
    b = tmp;
}

layout(pixel_interlock_ordered) in;
void main() {
    vec3 view = normalize(pcs.cameraPosition - inPosition);
    Material mat = pcs.materialBuffer.materials[inMaterialIndex];

    PBRMaterial pbr = getPBRMaterial(mat, inUV);
    vec3 outputColor = directionalLight(view, pbr) + ambientLight(view, pbr) + pbr.emission;

    uvec2 screenCoords = uvec2(gl_FragCoord.xy - vec2(0.5f));
    u32 baseIndex = (screenCoords.y * pcs.frameBufferWidth + screenCoords.x) * 4;
    OITNode cur = OITNode(packe5bgr9(vec4(outputColor, 1.0f)), gl_FragCoord.z, 1.0f - pbr.albedo.a);

    beginInvocationInterlockARB();

    for(u32 i = 0; i < 4; i++) {
        if(pcs.oitBuffer.nodes[baseIndex + i].depth < cur.depth) {
            swapNodes(cur, pcs.oitBuffer.nodes[baseIndex + i]);
        }
    }

    endInvocationInterlockARB();
}