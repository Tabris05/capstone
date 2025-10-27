#version 460

#include "types.glsl"
#include "extensions.glsl"

#include "shared/vertex.h"

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outPositionLight;
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

    vec3 worldPosition = vec3(modelTransform * vec4(v.position, 1.0f));

    outPosition = worldPosition;
    outPositionLight = vec3(makeLightMatrix(pcs.lightAngle, pcs.orthoSize) * vec4(worldPosition, 1.0f));
    outNormal = normalTransform * v.normal;
    outTangent = normalTransform * v.tangent.xyz;
    outBitangent = cross(normalize(outNormal), normalize(outTangent)) * v.tangent.w;
    outUV = v.uv;
    outMaterialIndex = gl_BaseInstance;

    gl_Position = pcs.cameraTransform * vec4(worldPosition, 1.0f);
}