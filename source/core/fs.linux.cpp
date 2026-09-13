#include <core/fs.hpp>
#include <core/debug.hpp>

#include <pipeline.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PIPELINE_LINUX

bool FS::has_file( const char* path )
{
	todo( "FS::has_file" );
}

bool FS::has_dir( const char* path )
{
	todo( "FS::has_dir" );
}

void FS::rename_file( const char* path, const char* name )
{
    todo( "FS::rename_file" );
}

void FS::rename_dir( const char* path, const char* name )
{
    todo( "FS::rename_dir" );
}

void FS::delete_file( const char* path )
{
    todo( "FS::delete_file" );
}

void FS::delete_dir(const char* path)
{
	todo( "FS::delete_dir" );
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////
