#include <backend/window.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Window
{
	u32 width       	= 800;
	u32 height          = 800;
	u32 x               = 0;
	u32 y               = 0;
	const char* title   = "Clone";
	float scale         = 1.0f;

	extern bool is_running;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WindowConfig::running()
{
	return Window::is_running;
}

void WindowConfig::size( const u32 width, const u32 height )
{
	Window::width  = width;
	Window::height = height;
}

void WindowConfig::pos( const u32 x, const u32 y )
{
	Window::x = x;
	Window::y = y;
}

void WindowConfig::scale( const float scale )
{
	Window::scale = scale;
}

void WindowConfig::title( const char* title )
{
	Window::title = title;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 WindowConfig::get_width()
{
	return Window::width;
}

u32 WindowConfig::get_height()
{
	return Window::height;
}

u32 WindowConfig::get_posx()
{
	return Window::x;
}

u32 WindowConfig::get_posy()
{
	return Window::y;
}

u32 WindowConfig::get_width_in_pixels()
{
	return static_cast<i32>( Window::width * Window::scale );
}

u32 WindowConfig::get_height_in_pixels()
{
	return static_cast<i32>( Window::height * Window::scale );
}

const char* WindowConfig::get_title()
{
	return Window::title;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
