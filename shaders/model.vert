#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "../shared/vertex.h"
#include "../shared/material.h"

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out vec2 outUV;
layout(location = 5) flat out i32 outMaterialIndex;

layout(buffer_reference, scalar) restrict readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, scalar) restrict readonly buffer MaterialBuffer {
    Material materials[];
};

layout(push_constant, scalar) uniform constants {
    VertexBuffer vertexBuffer;
    MaterialBuffer materialBuffer;
    mat4 modelTransform;
    mat4 cameraTransform;
    mat3 normalTransform;
    vec3 cameraPosition;
} pcs;

void main() {
    Vertex v = pcs.vertexBuffer.vertices[gl_VertexIndex];

    vec3 worldPosition = vec3(pcs.modelTransform * vec4(v.position, 1.0f));

    outPosition = worldPosition;
    outNormal = pcs.normalTransform * v.normal;
    outTangent = pcs.normalTransform * v.tangent.xyz;
    outBitangent = cross(normalize(outNormal), normalize(outTangent)) * v.tangent.w;
    outUV = v.uv;
    outMaterialIndex = gl_BaseInstance;

    gl_Position = pcs.cameraTransform * vec4(worldPosition, 1.0f);
}