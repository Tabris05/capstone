#ifndef MATERIAL_H
#define MATERIAL_H

#ifdef __cplusplus
    #include <glm/glm.hpp>
    #include <tbrs/types.hpp>
    #define GLM glm::
#else
    #define GLM
    #include "../shaders/types.glsl"
#endif

#define HAS_ALBEDO 0x01
#define HAS_NORMAL 0x02
#define HAS_OCCLUSION 0x04
#define HAS_METALLIC_ROUGHNESS 0x08
#define HAS_EMISSIVE 0x10

struct Material {
    GLM vec4 baseColor;
    GLM vec4 emissiveColor;
    f32 metallic;
    f32 roughness;
    u32 albedoIndex;
    u32 normalIndex;
    u32 occlusionIndex;
    u32 metallicRoughnessIndex;
    u32 emissiveIndex;
    u32 texBitfield;
};

#undef GLM

#endif