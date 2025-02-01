#ifndef TYPES_HPP
#define TYPES_HPP

using f64 = double;
static_assert(sizeof(f64) == 8, "f64 must alias a 64-bit floating point type.");

using f32 = float;
static_assert(sizeof(f32) == 4, "f32 must alias a 32-bit floating point type.");

using i64 = long long;
static_assert(sizeof(i64) == 8, "i64 must alias a 64-bit signed integer type.");

using i32 = int;
static_assert(sizeof(i32) == 4, "i32 must alias a 32-bit signed integer type.");

using i16 = short;
static_assert(sizeof(i16) == 2, "i16 must alias an 16-bit signed integer type.");

using i8 = char;
static_assert(sizeof(i8) == 1, "i8 must alias an 8-bit signed integer type.");

using u64 = unsigned long long;
static_assert(sizeof(u64) == 8, "u64 must alias a 64-bit unsigned integer type.");

using u32 = unsigned int;
static_assert(sizeof(u32) == 4, "u32 must alias a 32-bit unsigned integer type.");

using u16 = unsigned short;
static_assert(sizeof(u16) == 2, "u16 must alias an 16-bit unsigned integer type.");

using u8 = unsigned char;
static_assert(sizeof(u8) == 1, "u8 must alias an 8-bit unsigned integer type.");

using byte = unsigned char;
static_assert(sizeof(byte) == 1, "byte must alias an 8-bit unsigned integer type.");

using b32 = u32;

using b8 = bool;
static_assert(sizeof(b8) == 1, "b8 must alias an 8-bit boolean type.");

#endif
