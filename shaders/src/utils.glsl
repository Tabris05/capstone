#ifndef UTILS_GLSL
#define UTILS_GLSL

#include "types.glsl"
#define EPSILON 0.000001f

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

vec3 agx(vec3 color) {  
    const mat3 matrix = {
    	{ 0.842479062253094f,  0.0423282422610123f, 0.0423756549057051f },
    	{ 0.0784335999999992f, 0.878468636469772f,  0.0784336f          },
    	{ 0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f  }
    };
    const mat3 inverse = {
    	{  1.19687900512017f,   -0.0528968517574562f, -0.0529716355144438f },
    	{ -0.0980208811401368f,  1.15190312990417f,   -0.0980434501171241f },
    	{ -0.0990297440797205f, -0.0989611768448433f,  1.15107367264116f   }
    };
    const vec3 minEv = vec3(-12.47393f);
    const vec3 maxEv = vec3(4.026069f);

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

    return color;
}

vec3 aces(vec3 color){	
	const mat3 m1 = {
        { 0.59719f, 0.07600f, 0.02840f, },
        { 0.35458f, 0.90834f, 0.13383f, },
        { 0.04823f, 0.01566f, 0.83777f  }
    };
	const mat3 m2 = {
        {  1.60475f, -0.10208f, -0.00327f, },
        { -0.53108f,  1.10813f, -0.07276f, },
        { -0.07367f, -0.00605f,  1.07602f  }
    };
	vec3 v = m1 * color;    
	vec3 a = v * (v + 0.0245786f) - 0.000090537f;
	vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
        
	return m2 * (a / b);
}

vec3 khronos(vec3 color) {
  const f32 startCompression = 0.8f - 0.04f;
  const f32 desaturation = 0.15f;

  f32 x = min(color.r, min(color.g, color.b));
  f32 offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
  color -= offset;

  f32 peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const f32 d = 1.0f - startCompression;
  f32 newPeak = 1.0f - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  f32 g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
  return mix(color, newPeak * vec3(1.0f), g);
}

vec3 reinhard(vec3 color) {
	const f32 exposure2 = 25.0f;
	f32 oldLuminance = dot(color, vec3(0.2126f, 0.7152f, 0.0722f));
	f32 newLuminance = oldLuminance * (1.0f + oldLuminance / exposure2) / (1.0f + oldLuminance);
	return color * (newLuminance / (oldLuminance + EPSILON));
}

vec3 srgbToLinear(vec3 color) {
    return mix(color / 12.92f, pow((color + 0.055f) / 1.055f, vec3(2.4f)), greaterThan(color, vec3(0.04045f)));
}

vec3 linearToSrgb(vec3 color) {
    return mix(color * 12.92f, 1.055f * pow(color, vec3(1.0 / 2.4)) - 0.055, greaterThan(color, vec3(0.0031308f)));
}

#endif