#version 460

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

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 1.0f);
    outColor = colors[gl_VertexIndex];
}