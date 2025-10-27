#version 460

#include "extensions.glsl"

#include "shared/vertex.h"
#include "shared/material.h"

layout(buffer_reference, scalar) restrict readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant, scalar) uniform constants {
    u64 oitBuffer;
    VertexBuffer vertexBuffer;
    u64 materialBuffer;
    u64 poissonDiskBuffer;
    mat4 cameraTransform;
    mat4x3 modelTransform;
    vec4 lightColor;
    vec3 cameraPosition;
    vec3 lightAngle;
    f32 orthoSize;
    u32 frameBufferWidth;
    u32 irradianceMapIdx;
    u32 radianceMapIdx;
    u32 brdfIntegralTexIdx;
    u32 shadowMapTexIdx;
} pcs;

void main() {
    Vertex v = pcs.vertexBuffer.vertices[gl_VertexIndex];

    mat4 modelTransform = mat4(pcs.modelTransform);

    gl_Position = pcs.cameraTransform * (modelTransform * vec4(v.position, 1.0f));
}