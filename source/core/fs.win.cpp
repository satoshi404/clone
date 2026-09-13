#include <core/fs.hpp>
#include <core/debug.hpp>

#include <pipeline.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PIPELINE_WINDOWS

#include <windows.h>

static DWORD get_attributes( const char* path )
{
    return GetFileAttributesA( path );
}


bool FS::has_file( const char* path )
{
	DWORD attrs = get_attributes( path );
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool FS::has_dir( const char* path )
{
	DWORD attrs = get_attributes(path);
	return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void FS::rename_file( const char* path, const char* name )
{
    MoveFileA(path, name);
}

void FS::rename_dir( const char* path, const char* name )
{
    MoveFileA(path, name);
}

void FS::delete_file( const char* path )
{
    DeleteFileA(path);
}

void FS::delete_dir(const char* path)
{

    RemoveDirectoryA(path);
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////
