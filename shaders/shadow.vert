#version 460

#include "extensions.glsl"

#include "../shared/vertex.h"
#include "../shared/material.h"

#define SHADOW_MAP_TEXEL_SIZE 1.0f / 2048.0f

layout(buffer_reference, scalar) restrict readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant, scalar) uniform constants {
    u64 oitBuffer;
    VertexBuffer vertexBuffer;
    u64 materialBuffer;
    u64 poissonDiskBuffer;
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
    
    vec4 offset = vec4(normalize(normalTransform * v.normal) * SHADOW_MAP_TEXEL_SIZE, 0.0f);
    gl_Position = pcs.lightTransform * (modelTransform * vec4(v.position, 1.0f) - offset);
}