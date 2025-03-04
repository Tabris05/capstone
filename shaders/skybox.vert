#version 460

#include "extensions.glsl"

#define EPSILON 1.1920929e-7f;

vec3 vertices[36] = {
    { -1.0f,  1.0f, -1.0f },
    { -1.0f,  1.0f,  1.0f },
    { -1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f,  1.0f },
    { -1.0f, -1.0f, -1.0f },
    { -1.0f,  1.0f,  1.0f },
    { -1.0f, -1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    { -1.0f, -1.0f, -1.0f },
    {  1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f, -1.0f },
    {  1.0f, -1.0f,  1.0f },
    {  1.0f, -1.0f, -1.0f },
    {  1.0f,  1.0f, -1.0f },
    { -1.0f, -1.0f, -1.0f },
    { -1.0f,  1.0f, -1.0f },
    { -1.0f, -1.0f, -1.0f },
    {  1.0f,  1.0f, -1.0f },
    {  1.0f, -1.0f, -1.0f },
    {  1.0f, -1.0f,  1.0f },
    {  1.0f,  1.0f, -1.0f },
    {  1.0f,  1.0f,  1.0f },
    {  1.0f,  1.0f, -1.0f },
    {  1.0f, -1.0f,  1.0f },
    { -1.0f,  1.0f, -1.0f },
    {  1.0f,  1.0f, -1.0f },
    { -1.0f,  1.0f,  1.0f },
    {  1.0f,  1.0f,  1.0f },
    { -1.0f,  1.0f,  1.0f },
    {  1.0f,  1.0f, -1.0f },
    { -1.0f, -1.0f,  1.0f },
    { -1.0f,  1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    {  1.0f,  1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    { -1.0f,  1.0f,  1.0f }
};

layout(location = 0) out vec3 outPosition;

layout(push_constant, scalar) uniform PushConstants {
    mat4 camMatrix;
} pcs;

void main() {
    vec3 xyz = vertices[gl_VertexIndex];
    outPosition = xyz;
    
    vec4 transformedPos = pcs.camMatrix * vec4(xyz, 1.0f);
    transformedPos.z = EPSILON;
	gl_Position = transformedPos;
}