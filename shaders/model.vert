#version 460

#include "types.glsl"
#include "extensions.glsl"

#include "../shared/vertex.h"

layout(location = 0) out vec4 outPositionLight;
layout(location = 1) out vec3 outPosition;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
layout(location = 5) out vec2 outUV;
layout(location = 6) flat out i32 outMaterialIndex;

layout(buffer_reference, scalar) restrict readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant, scalar) uniform constants {
    u64 oitBuffer;
    VertexBuffer vertexBuffer;
    u64 materialBuffer;
    mat4 cameraTransform;
    mat4 lightTransform;
    mat4x3 modelTransform;
    vec4 lightColor;
    vec3 cameraPosition;
    vec3 lightAngle;
    u32 frameBufferWidth;
} pcs;

void main() {
    Vertex v = pcs.vertexBuffer.vertices[gl_VertexIndex];

    mat4 modelTransform = mat4(pcs.modelTransform);
    mat3 normalTransform = mat3(cross(modelTransform[1].xyz, modelTransform[2].xyz), cross(modelTransform[2].xyz, modelTransform[0].xyz), cross(modelTransform[0].xyz, modelTransform[1].xyz));

    vec3 worldPosition = vec3(modelTransform * vec4(v.position, 1.0f));

    outPositionLight = pcs.lightTransform * vec4(worldPosition, 1.0f);
    outPosition = worldPosition;
    outNormal = normalTransform * v.normal;
    outTangent = normalTransform * v.tangent.xyz;
    outBitangent = cross(normalize(outNormal), normalize(outTangent)) * v.tangent.w;
    outUV = v.uv;
    outMaterialIndex = gl_BaseInstance;

    gl_Position = pcs.cameraTransform * vec4(worldPosition, 1.0f);
}