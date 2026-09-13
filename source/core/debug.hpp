#pragma once

#include <core/types.hpp>

#include <stdarg.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum PrintColorType : u32
{
	PrintColorType_Default  = 0,
	PrintColorType_Black 	= 30,
	PrintColorType_Red 		= 31,
	PrintColorType_Green 	= 32,
	PrintColorType_Yellow 	= 33,
	PrintColorType_Blue 	= 34,
	PrintColorType_Magenta 	= 35,
	PrintColorType_Cyan 	= 36,
	PrintColorType_White 	= 37,
};

#define color_format(color) ((u8)(color >> 24) << 16 | (u8)(color >> 16) << 8 | (u8)(color >> 8) & 0xFF)

namespace TerminalDebug
{
	extern void print( const char *format, ... );
	extern void print( PrintColorType color_type, const char *format, ... );
	extern void println( const char *format, ... );
	extern void println( PrintColorType color_type, const char *format, ...);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define IfNullReturn(ptr, return_value, message)                \
	if (!(ptr))                                                 \
	{                                                           \
		TerminalDebug::println(PrintColorType_Red, "Error: %s\n", message); \
		return return_value;                                    \
	}\

#define ThrowIfFailed( ptr )                                        \
	do {                                                            \
		if (!(ptr))                                                 \
		{                                                           \
			TerminalDebug::println(PrintColorType_Red, "Failed: %d:%d\n", __LINE__, __builtin_COLUMN()); \
			__debugbreak();                                         \
		}                                                           \
	} while(0)


#define todo( message ) \
	TerminalDebug::println( PrintColorType_Yellow, "TODO: %s at %s:%d:%d ", message, __FILE__, __LINE__, __builtin_COLUMN() );\
	__debugbreak();