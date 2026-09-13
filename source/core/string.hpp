#pragma once

// TODO:
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <core/types.hpp>
#include <core/debug.hpp>

class String
{

public:
    String() : _data(nullptr), _size(0) {}

    explicit String(const char* cstr)
    {
        init(cstr);
    }

    String(const String& other)
    {
        _size = other._size;
        _data = other._data ? strdup(other._data) : nullptr;
    }

    String& operator=(const String& other)
    {
        if (this != &other)
        {
            free(_data);
            _size = other._size;
            _data = other._data ? strdup(other._data) : nullptr;
        }
        return *this;
    }

    String(String&& other) noexcept
        : _data(other._data), _size(other._size)
    {
        other._data = nullptr;
        other._size = 0;
    }

    String& operator=(String&& other) noexcept
    {
        if (this != &other)
        {
            free(_data);
            _data = other._data;
            _size = other._size;
            other._data = nullptr;
            other._size = 0;
        }
        return *this;
    }

    ~String()
    {
        free(_data);
    }

    void init(const char* cstr = "")
    {
        free( _data );
        _data = strdup(cstr);
        _size = static_cast<u32>(strlen(cstr));
    }

    char* data() const { return _data; }
    u32 size() const { return _size; }

    String& append( const char* cstr, bool whitespace = false, const i32 repeat = 1 )
	{
        for (int i = 0; i < repeat; i++)
        {
	        u32 add_size = static_cast<u32>( strlen( cstr ) );
	        u32 sep_size = whitespace ? 1u : 0u;
	        u32 new_total = _size + sep_size + add_size;

	        char* new_data = static_cast<char*>(malloc(new_total + 1));
	        memcpy(new_data, _data, _size);
	        if (whitespace)
	            new_data[_size] = ' ';
	        memcpy(new_data + _size + sep_size, cstr, add_size + 1);

	        free(_data);
	        _data = new_data;
	        _size = new_total;
        }

	    return *this;
	}

	bool save_data( const char* path  )
	{
		FILE* fp;
		fp = fopen( path, "w" );

		if ( fp == nullptr ) return false;

		fwrite( _data, 1, _size, fp );

		fclose( fp );

		return true;
	};

	bool read_data( const char* path )
	{
		FILE* fp;
		fp = fopen( path, "rb" );

		if ( fp == nullptr ) return false;

		fread( _data, sizeof( _data ), _size, fp );

		fclose( fp );

		return true;
	}

    void reset()
    {

    }

private:
    char* _data;
    u32 _size;
};