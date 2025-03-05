#ifndef UTILS_GLSL
#define UTILS_GLSL

#include "types.glsl"

b8 bitmaskGet(u32 mask, u32 value) {
	return b8(mask & value);
}

float clampedDot(vec3 a, vec3 b) {
	return max(dot(a, b), 0.0f);
}

u32 calcColor(f32 color, i32 exp) {
    return u32(color / exp2(exp - 24) + 0.5f);
}

u32 packe5bgr9(vec4 color) {
    const f32 maxVal = 16384.0f;
    vec3 clampedColor = clamp(color, 0.0f, maxVal).rgb;

    f32 maxChannel = max(max(clampedColor.r, clampedColor.g), clampedColor.b);
    i32 exp = i32(max(-16, floor(log2(maxChannel))) + 16);

    if(calcColor(maxChannel, exp) == 512) {
        exp += 1;
    }

    u32 r = calcColor(clampedColor.r, exp) & 0x1FF;
    u32 g = calcColor(clampedColor.g, exp) & 0x1FF;
    u32 b = calcColor(clampedColor.b, exp) & 0x1FF;

    return (u32(exp) << 27) | (b << 18) | (g << 9) | r;
}

u32 packDepthTransmittance(f32 depth, f32 transmittance) {
    return (uint(depth * 16777215.0f) << 8) | (uint(transmittance * 255.0f) & 0xFF);
}

f32 unpackDepth(u32 packedDepthTransmittance) {
    return f32(packedDepthTransmittance >> 8) / 16777215.0f;
}

f32 unpackTransmittance(u32 packedDepthTransmittance) {
    return f32(packedDepthTransmittance & 0xFF) / 255.0f;
}

vec4 unpacke5bgr9(u32 color) {
    f32 exp = f32(color >> 27);
    f32 r = f32(color & 0x1FF);
    f32 g = f32((color >> 9) & 0x1FF);
    f32 b = f32((color >> 18) & 0x1FF);

    return vec4(vec3(r, g, b) * exp2(exp - 15.0f) / 511.0f, 1.0f);
}

f32 countMips(ivec2 dimensions) {
    return floor(log2(max(dimensions.x, dimensions.y))) + 1.0f;
}

vec4 agx(vec3 color) {  
    const mat3 matrix = {
    	{ 0.842479062253094, 0.0423282422610123, 0.0423756549057051 },
    	{ 0.0784335999999992, 0.878468636469772, 0.0784336 },
    	{ 0.0792237451477643, 0.0791661274605434, 0.879142973793104 }
    };
    const mat3 inverse = {
    	{ 1.19687900512017, -0.0528968517574562, -0.0529716355144438 },
    	{ -0.0980208811401368, 1.15190312990417, -0.0980434501171241 },
    	{ -0.0990297440797205, -0.0989611768448433, 1.15107367264116 }
    };
    const vec3 minEv = vec3(-12.47393);
    const vec3 maxEv = vec3(4.026069);

    color = matrix * color;
    color = clamp(vec3(log2(color)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);

    vec3 color2 = color * color;
    vec3 color4 = color2 * color2;

    color = 15.5f * color4 * color2
    	- 40.14f * color4 * color
    	+ 31.96f * color4
    	- 6.868f * color2 * color
    	+ 0.4298f * color2
    	+ 0.1191f * color
    	- 0.00232f;

    color = inverse * color;
    color = clamp(color, vec3(0.0f), vec3(1.0f));

    return vec4(color, 1.0f);
}

#endif