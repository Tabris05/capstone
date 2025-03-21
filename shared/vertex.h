#ifndef VERTEX_H
#define VERTEX_H

#ifdef __cplusplus
    #include <glm/glm.hpp>
    #define GLM glm::
#else
    #define GLM
#endif

struct Vertex {
    GLM vec3 position;
    GLM vec3 normal;
    GLM vec4 tangent;
    GLM vec2 uv;
};

#undef GLM

#endif
