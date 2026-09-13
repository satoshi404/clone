#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined( __linux__ )
	#define PIPELINE_LINUX   ( 1 )
	#define PIPELINE_WINDOWS ( 0 )
#elif defined( __WIN32 ) || defined( __WIN64 )
	#define PIPELINE_WINDOWS ( 1 )
	#define PIPELINE_LINUX   ( 0 )
#else
	static_assert( false, "Platform not supported" );
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__x86_64__) || defined(__LP64__) || defined(_WIN64) || defined(_M_X64) || defined(__aarch64__)
	#define ARCH_X86_64 ( 1 )
	#define ARCH_X86    ( 0 )
#elif defined(__i386__) || defined(_X86_) || defined(_M_IX86) || defined(__arm__)
	#define ARCH_X86_64 ( 0 )
	#define ARCH_X86    ( 1 )
#else
	static_assert( false, "Unsupported architecture" );
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined( __GNUC__ ) || defined( __clang__ )
	#define COMPILER_GCC  ( 1 )
	#define COMPILER_MSVC ( 0 )
#elif defined(_MSC_VER)
	#define COMPILER_GCC  ( 0 )
	#define COMPILER_MSVC ( 1 )
#else
	static_assert( false, "Unsupported compiler" );
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////