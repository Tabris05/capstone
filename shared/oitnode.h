#ifndef OITNODE_H
#define OITNODE_H

#ifdef __cplusplus
	#include <tbrs/types.hpp>
#else
	#include "../shaders/types.glsl"
#endif

struct OITNode {
	u32 packedColor;
	u32 packedDepthTransmittance;
};

#endif