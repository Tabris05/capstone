#version 460

#include "extensions.glsl"

#include "../shared/vertex.h"
#include "../shared/material.h"

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

    gl_Position = pcs.cameraTransform * (modelTransform * vec4(v.position, 1.0f));
}