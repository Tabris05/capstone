#ifndef MATERIAL_H
#define MATERIAL_H

#ifdef __cplusplus
    #include <glm/glm.hpp>
    #include <tbrs/types.hpp>
    #include "../capstone/src/image_handle.hpp"
    #define GLM glm::
#else
    #define GLM
    #include "../shaders/src/types.glsl"
    #define ImageHandle u32
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
    ImageHandle albedoIndex;
    ImageHandle normalIndex;
    ImageHandle occlusionIndex;
    ImageHandle metallicRoughnessIndex;
    ImageHandle emissiveIndex;
    u32 texBitfield;
};

#undef GLM

#endif