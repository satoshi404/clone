#pragma once

#include <core/types.hpp>

// TODO:
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////

#define RAD_180 ( M_PI / 180.f )

INLINE i32 int_clamp( const i32 value, const i32 min, const i32 max )
{
	if ( value <= min ) return min;
	else if ( value >= max ) return max;
	return value;
}

INLINE f32 float_clamp( const f32 value, const f32 min, const f32 max )
{
	if ( value <= min ) return min;
	else if ( value >= max ) return max;
	return value;
}

struct float_vec2 { f32 x; f32 y; };
struct float_vec3 { f32 x; f32 y; f32 z; };
struct float_vec4 { f32 x; f32 y; f32 z; f32 w; };

struct int_vec2 { i32 x; i32 y; };
struct int_vec3 { i32 x; i32 y; i32 z; };
struct int_vec4 { i32 x; i32 y; i32 z; i32 w; };

struct mat2_float_vec2 { float_vec2 x;  float_vec2 y; };
struct mat3_float_vec2 { float_vec2 x;  float_vec2 y; float_vec2 z; };
struct mat4_float_vec2 { float_vec2 x;  float_vec2 y; float_vec2 z;  float_vec2 w; };

struct mat2_float_vec3 { float_vec3 x;  float_vec3 y; };
struct mat3_float_vec3 { float_vec3 x;  float_vec3 y; float_vec3 z; };
struct mat4_float_vec3 { float_vec3 x;  float_vec3 y; float_vec3 z;  float_vec3 w; };

struct mat2_float_vec4 { float_vec4 x;  float_vec4 y; };
struct mat3_float_vec4 { float_vec4 x;  float_vec4 y; float_vec4 z; };
struct mat4_float_vec4 { float_vec4 x;  float_vec4 y; float_vec4 z;  float_vec4 w; };

struct mat2_int_vec2 { int_vec2 x;  int_vec2 y; };
struct mat3_int_vec2 { int_vec2 x;  int_vec2 y; int_vec2 z; };
struct mat4_int_vec2 { int_vec2 x;  int_vec2 y; int_vec2 z;  int_vec2 w; };

struct mat2_int_vec3 { int_vec3 x;  int_vec3 y; };
struct mat3_int_vec3 { int_vec3 x;  int_vec3 y; int_vec3 z; };
struct mat4_int_vec3 { int_vec3 x;  int_vec3 y; int_vec3 z;  float_vec3 w; };

struct mat2_int_vec4 { int_vec4 x;  int_vec4 y; };
struct mat3_int_vec4 { int_vec4 x;  int_vec4 y; int_vec4 z; };
struct mat4_int_vec4 { int_vec4 x;  int_vec4 y; int_vec4 z;  int_vec4 w; };

///////////////////////////////////////////////////////////////////////////////////////////////////////////

#define fvec2( x, y ) (float_vec2) { x, y }
#define fvec3( x, y, z ) (float_vec3) { x, y, z }
#define fvec4( x, y, z, w ) (float_vec4) { x, y, z, w }

#define ivec2( x, y ) (int_vec2) { x, y }
#define ivec3( x, y, z ) (int_vec3) { x, y, z }
#define ivec4( x, y, z, w ) (int_vec4) { x, y, z, w }

#define mat4_fvec4( x, y, z, w )( mat4_float_vec4 ) { x, y, z, w }

#define fvec2_zero fvec2( 0.f, 0.f )
#define fvec3_zero fvec3( 0.f, 0.f, 0.f )
#define fvec4_zero fvec4( 0.f, 0.f, 0.f, 0.f )

#define ivec2_zero ivec2( 0, 0 )
#define ivec3_zero ivec3( 0, 0, 0 )
#define ivec4_zero ivec4( 0, 0, 0, 0 )

// Zero

#define mat2_float_vec2_zero\
	( mat2_float_vec2 ) {\
		fvec2_zero,\
		fvec2_zero,\
	}

#define mat2_float_vec3_zero\
	( mat2_float_vec3 ) {\
		fvec3_zero,\
		fvec3_zero,\
	}

#define mat2_float_vec4_zero\
	( mat2_float_vec4 ) {\
		fvec4_zero,\
		fvec4_zero,\
	}


#define mat3_float_vec2_zero\
	( mat3_float_vec2 ) {\
		fvec2_zero,\
		fvec2_zero,\
		fvec2_zero,\
	}

#define mat3_float_vec3_zero\
	( mat3_float_vec3 ) {\
		fvec3_zero,\
		fvec3_zero,\
		fvec3_zero,\
	}

#define mat3_float_vec4_zero\
	( mat3_float_vec4 ) {\
		fvec4_zero,\
		fvec4_zero,\
		fvec4_zero,\
	}


#define mat4_float_vec2_zero\
	( mat4_float_vec2 ) {\
		fvec2_zero,\
		fvec2_zero,\
		fvec2_zero,\
		fvec2_zero,\
	}

#define mat4_float_vec3_zero\
	( mat4_float_vec3 ) {\
		fvec3_zero,\
		fvec3_zero,\
		fvec3_zero,\
		fvec3_zero,\
	}

#define mat4_float_vec4_zero\
	( mat4_float_vec4 ) {\
		fvec4_zero,\
		fvec4_zero,\
		fvec4_zero,\
		fvec4_zero,\
	}

// Identity

#define mat4_float_vec4_identity\
	( mat4_float_vec4 ) {\
		fvec4(1.f, 0.f, 0.f, 0.f),\
		fvec4(0.f, 1.f, 0.f, 0.f),\
		fvec4(0.f, 0.f, 1.f, 0.f),\
		fvec4(0.f, 0.f, 0.f, 1.f),\
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////