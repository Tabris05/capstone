#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "../shared/vertex.h"

layout(location = 0) out vec3 outColor;

layout(buffer_reference, scalar) buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant, scalar) uniform constants {
    VertexBuffer vertexBuffer;
    mat4 transform;
} pcs;

void main() {
    Vertex v = pcs.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = pcs.transform * vec4(v.position, 1.0f);
    outColor = v.color;
}