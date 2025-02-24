#include "types.h"

u32 packe5bgr9(vec4 color) {
    f32 maxChannelVal = max(max(color.r, color.g), color.b);
    if (maxChannelVal == 0.0) {
        return 0;
    }

    i32 exp = clamp(i32(floor(log2(maxChannelVal))) + 15, 0, 31);

    f32 scale = exp2(f32(9 - exp));
    u32 r = u32(round(color.r * scale)) & 0x1FF;
    u32 g = u32(round(color.g * scale)) & 0x1FF;
    u32 b = u32(round(color.b * scale)) & 0x1FF;

    return (u32(exp) << 27) | (b << 18) | (g << 9) | r;
}