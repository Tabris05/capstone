#ifndef OITNODE_H
#define OITNODE_H

#ifdef __cplusplus
	#include <tbrs/types.hpp>
#else
	#include "../shaders/types.glsl"
#endif

struct OITNode {
	u32 packedColor;
	f32 depth;
	f32 transmittance;
};

#endif