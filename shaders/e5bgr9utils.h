#include "types.h"

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