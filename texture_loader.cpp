#include "texture_loader.hpp"

#include <core/debug.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TextureData::~TextureData()
{
	if ( pixels )
	{
		stbi_image_free( pixels ); // não usar free() direto — stb tem sua própria rotina de liberação
		pixels = nullptr;
	}
}

TextureData::TextureData( TextureData&& other ) noexcept
{
	pixels = other.pixels;
	width  = other.width;
	height = other.height;

	other.pixels = nullptr;
	other.width  = 0;
	other.height = 0;
}

TextureData& TextureData::operator=( TextureData&& other ) noexcept
{
	if ( this != &other )
	{
		if ( pixels ) stbi_image_free( pixels );

		pixels = other.pixels;
		width  = other.width;
		height = other.height;

		other.pixels = nullptr;
		other.width  = 0;
		other.height = 0;
	}
	return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool texture_load( const char* path, TextureData& out )
{
	// stb_image usa origem (0,0) no canto superior-esquerdo por padrão, que já é a
	// convenção esperada pelas UVs exportadas por a maioria dos DCCs (Blender inclusive)
	// pra APIs D3D-style. Se a textura aparecer de cabeça pra baixo, é aqui que se inverte.
	stbi_set_flip_vertically_on_load( false );

	int width = 0, height = 0, channelsInFile = 0;
	unsigned char* data = stbi_load( path, &width, &height, &channelsInFile, 4 ); // força RGBA8
	if ( !data )
	{
		TerminalDebug::println( PrintColorType_Red, "TextureLoader::Error: failed to load '%s' (%s)", path, stbi_failure_reason() );
		return false;
	}

	out.pixels = data;
	out.width  = (uint32_t)width;
	out.height = (uint32_t)height;

	TerminalDebug::println( PrintColorType_Green, "TextureLoader: loaded '%s' (%dx%d, %d channels in file)", path, width, height, channelsInFile );
	return true;
}