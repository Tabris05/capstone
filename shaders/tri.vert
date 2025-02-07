#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) out vec3 outColor;

vec3 positions[3] = {
    {  0.5f,  0.5f,  0.0f },
    {  0.0f, -0.5f,  0.0f },
    { -0.5f,  0.5f,  0.0f }
};

vec3 colors[3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }
};

layout(push_constant, scalar) uniform constants {
    mat4 transform;
} pcs;

void main() {
    gl_Position = pcs.transform * vec4(positions[gl_VertexIndex], 1.0f);
    outColor = colors[gl_VertexIndex];
}