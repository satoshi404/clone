#pragma once

#include <pipeline.hpp>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Inline and no discard macros

#define INLINE inline
#define ALWAYS_INLINE __attribute__((always_inline)) inline

#define NO_DISCARD [[ nodiscard ]]
#define NO_INLINE __attribute__((noinline))
#define NO_RETURN __attribute__((noreturn))
#define UNUSED __attribute__((unused))
#define PACKED __attribute__((packed))

#define ALIGN( alignment ) __attribute__((aligned(alignment)))

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using i8 = signed char;
#define I8_MAX ( 127 )              // 0x7F
#define I8_MIN ( -128 )             // 0x80

using i16 = signed short;
#define I16_MAX ( 32767 )           // 0x7FFF
#define I16_MIN ( -32768 )          // 0x8000

using i32 = signed int;
#define I32_MAX ( 2147483647 )      // 0x7FFFFFFF
#define I32_MIN ( -2147483647 - 1 )

using i64 = signed long long;
#define I64_MAX ( 9223372036854775807LL )  // 0x7FFFFFFFFFFFFFFF
#define I64_MIN ( -9223372036854775807LL - 1LL ) // 0x8000000000000000

using u8 = unsigned char;
#define U8_MAX ( 255 )              // 0xFF
#define U8_MIN ( 0 )

using u16 = unsigned short;
#define U16_MAX ( 65535 )           // 0xFFFF
#define U16_MIN ( 0 )

using u32 = unsigned int;
#define U32_MAX ( 4294967295U )     // 0xFFFFFFFF
#define U32_MIN ( 0 )

using u64 = unsigned long long;
#define U64_MAX ( 18446744073709551615ULL ) // 0xFFFFFFFFFFFFFFFF
#define U64_MIN ( 0 )

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using f32 = float;
#define FLOAT32_MAX ( 3.402823466e+38F )
#define FLOAT32_MIN ( 1.175494351e-38F )
using f64 = double;
#define FLOAT64_MAX ( 1.7976931348623158e+308 )
#define FLOAT64_MIN ( 2.2250738585072014e-308 )

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if ARCH_X86_64
    using isize = i64;
    using usize = u64;
#elif ARCH_X86
    using isize = i32;
    using usize = u32;
#else
    #error "Unsupported architecture"
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXIT_FAILED ( -1 )
#define EXIT_SUCCESS ( 0 )

#define ARRAY_SIZE( x ) ( sizeof( x ) / sizeof( x[0] ) )

#define GLOBAL extern
#define LOCAL static
#define IMPORT

#define UNUSED_VAR( x ) (void)x

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
