#pragma once

#include <core/types.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace FS
{
	extern bool has_file( const char* path );
	extern bool has_dir( const char* path );

	extern void rename_file( const char* path, const char* name );
	extern void rename_dir( const char* path, const char* name );

	extern void delete_file( const char* path );
	extern void delete_dir( const char* path );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
