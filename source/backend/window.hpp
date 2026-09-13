#pragma once

#include <core/types.hpp>

#include <pipeline.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Window
{
  extern bool init();
  extern void pool();
  extern void show();
  extern void terminate();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace WindowConfig
{
  extern bool running();
  extern void size( const u32 width, const u32 height );
  extern void pos( const u32 x, const u32 y );
  extern void title( const char *title);
  extern void scale( const float scale );

  extern const char *get_title();
  extern u32 get_width();
  extern u32 get_height();
  extern u32 get_posx();
  extern u32 get_posy();
  extern u32 get_width_in_pixels();
  extern u32 get_height_in_pixels();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
