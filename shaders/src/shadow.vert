#version 460

#include "extensions.glsl"

#include "shared/vertex.h"
#include "shared/material.h"

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

mat4 makeLightMatrix(vec3 lightAngle, f32 orthoSize) {
    vec3 up = vec3(0.0f, 1.0f, 0.0f);
    vec3 f = lightAngle;
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 view = mat4(transpose(mat3(s, u, -f)));

    mat4 projection = mat4(
        vec4(-orthoSize, 0.0f,       0.0f,              0.0f),
        vec4(0.0f,       -orthoSize, 0.0f,              0.0f),
        vec4(0.0f,       0.0f,       -orthoSize / 2.0f, 0.0f),
        vec4(0.0f,       0.0f,       0.5f,              1.0f)
    );

    return projection * view;
}

void main() {
    Vertex v = pcs.vertexBuffer.vertices[gl_VertexIndex];

    mat4 modelTransform = mat4(pcs.modelTransform);
    mat3 normalTransform = mat3(cross(modelTransform[1].xyz, modelTransform[2].xyz), cross(modelTransform[2].xyz, modelTransform[0].xyz), cross(modelTransform[0].xyz, modelTransform[1].xyz));
    
    vec4 offset = vec4(normalize(normalTransform * v.normal) * SHADOW_MAP_TEXEL_SIZE, 0.0f);
    gl_Position = makeLightMatrix(pcs.lightAngle, pcs.orthoSize) * (modelTransform * vec4(v.position, 1.0f) - offset);
}