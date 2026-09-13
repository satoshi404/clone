#pragma once

#include <core/types.hpp>

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Memory
{
    INLINE void* alloc( usize size )
    {
        return malloc( size );
    }

    INLINE void* alloc_zero( usize size )
    {
        void* p = malloc( size );
        if ( p )
            memset( p, 0, size );
        return p;
    }

    INLINE void* realloc( void* ptr, usize size )
    {
        return ::realloc( ptr, size );
    }

    INLINE void free( void* ptr )
    {
        ::free( ptr );
    }

    INLINE void copy( void* dst, const void* src, usize size )
    {
        memcpy( dst, src, size );
    }

    INLINE void set( void* dst, int value, usize size )
    {
        memset( dst, value, size );
    }

    INLINE void zero( void* dst, usize size )
    {
        memset( dst, 0, size );
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////