layout(set = 0, binding = 0) uniform image2D img2DHeap[];
layout(set = 0, binding = 0) uniform uimageCube uimgCubeHeap[];
layout(set = 0, binding = 0) uniform texture2D tex2DHeap[];
layout(set = 0, binding = 0) uniform textureCube texCubeHeap[];
layout(set = 0, binding = 1) uniform sampler samplerHeap[];

#define UNPACK_IMG_2D(handle) img2DHeap[nonuniformEXT(handle)]
#define UNPACK_UIMG_CUBE(handle) uimgCubeHeap[nonuniformEXT(handle)]

#define UNPACK_TEX_2D(handle) sampler2D(tex2DHeap[nonuniformEXT(handle & ((1 << 20) - 1))], samplerHeap[nonuniformEXT(handle >> 20)])
#define UNPACK_TEX_CUBE(handle) samplerCube(texCubeHeap[nonuniformEXT(handle & ((1 << 20) - 1))], samplerHeap[nonuniformEXT(handle >> 20)])
#define UNPACK_TEX_SHADOW(handle) sampler2DShadow(tex2DHeap[nonuniformEXT(handle & ((1 << 20) - 1))], samplerHeap[nonuniformEXT(handle >> 20)])