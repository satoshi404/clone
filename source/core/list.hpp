#pragma once

#include <core/types.hpp>
#include <core/memory.hpp>

// TODO:
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename T>
struct List
{
    T*      data     = nullptr;
    u64     count    = 0;
    u64     capacity = 0;

    void init( u64 initial_capacity = 8 )
    {
        if ( initial_capacity < 1 )
            initial_capacity = 8;

        capacity = initial_capacity;
        count    = 0;
        data     = static_cast<T*>( malloc( capacity * sizeof( T ) ) );
    }

    void free()
    {
        if ( data )
        {
            ::free( data );
            data = nullptr;
        }
        count    = 0;
        capacity = 0;
    }

    void clear()
    {
        count = 0;
    }

    bool reserve( u64 new_capacity )
    {
        if ( new_capacity <= capacity )
            return true;

        T* new_data = static_cast<T*>( realloc( data, new_capacity * sizeof( T ) ) );
        if ( !new_data )
            return false;

        data     = new_data;
        capacity = new_capacity;
        return true;
    }

    bool ensure_capacity( u64 needed )
    {
        if ( needed <= capacity )
            return true;

        u64 new_cap = capacity ? capacity * 2 : 8;
        while ( new_cap < needed )
            new_cap *= 2;

        return reserve( new_cap );
    }

    bool append( const T& value )
    {
        if ( !ensure_capacity( count + 1 ) )
            return false;

        data[ count++ ] = value;
        return true;
    }

    bool push( const T& value )
    {
        return append( value );
    }

    T pop()
    {
        if ( count == 0 )
            return T{};

        return data[ --count ];
    }

    NO_DISCARD T& operator[]( u64 index )
    {
        return data[ index ];
    }

    NO_DISCARD const T& operator[]( u64 index ) const
    {
        return data[ index ];
    }

    NO_DISCARD T get( u64 index ) const
    {
        if ( index >= count )
            return T{};
        return data[ index ];
    }

    NO_DISCARD T* try_get( u64 index )
    {
        if ( index >= count )
            return nullptr;
        return &data[ index ];
    }

    NO_DISCARD u64 size() const
    {
        return count;
    }

    NO_DISCARD bool empty() const
    {
        return count == 0;
    }

    NO_DISCARD T* begin() { return data; }
    NO_DISCARD T* end()   { return data + count; }

    NO_DISCARD const T* begin() const { return data; }
    NO_DISCARD const T* end()   const { return data + count; }

    bool remove_at( u64 index )
    {
        if ( index >= count )
            return false;

        data[ index ] = data[ count - 1 ];
        --count;
        return true;
    }

    bool remove_swap( u64 index )
    {
        return remove_at( index );
    }

    bool remove_ordered( u64 index )
    {
        if ( index >= count )
            return false;

        for ( u64 i = index; i < count - 1; ++i )
            data[ i ] = data[ i + 1 ];

        --count;
        return true;
    }
};