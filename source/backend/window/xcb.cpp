#include <backend/window.hpp>
#include <backend/keyboard.hpp>

#include <pipeline.hpp>

#include <core/debug.hpp>

#if PIPELINE_LINUX

#if RENDERER

#include <GL/glx.h>

#endif

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/keysyms.h>
#include <xcb/xutil.h>

// TODO: vendor
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

static struct
{
  XCB_Connection *connection;
  XCB_Window window;

  Display *display;
  XCB_Window root;

  struct
  {
    XCB_Atom wm_delete_window;
    XCB_Atom protocols;
    XCB_Atom wm_state;
    XCB_Atom wm_state_fullscreen;
    // XCB_Atom wm_icon;
  } atoms;

  XCB_KeySymbols *key_symbols;
  XCB_GenericEvent *event;
  XCB_Screen *screen;

  bool is_running = false_value;
} handle = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static XCB_Atom intern_atom( XCB_Connection *connection, const char *atom_name )
{
  bool only_if_exists = false_value;

  xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, only_if_exists, strlen(atom_name), atom_name);
  xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);

  const XCB_Atom atom = static_cast<XCB_Atom>( reply->atom );

  char null_message[ 256 ];
  sprintf( null_message, "Failed to intern atom '%s'", atom_name );
  IfNullReturn( reply, atom, null_message );

  free(reply);

  return atom;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CoreWindow::init()
{
 // handle = {0};

  IfNullReturn(handle.display = XOpenDisplay(nullptr), false_value, "Failed to open X display");

  if (!(handle.connection = XGetXCBConnection(handle.display)))
  {

    TerminalDebug::Println(PrintColorType_Red, "Error: Connect to xcb server");

    XCloseDisplay(handle.display);
    handle.display = nullptr;

    return false_value;
  }

// TODO: Better implementation
#if RENDERER && API_OPENGL

  int visual_attribs[] =
      {
          GLX_RGBA,
          GLX_DOUBLEBUFFER,
          GLX_DEPTH_SIZE, 24,
          GLX_STENCIL_SIZE, 8,
          None};

  XVisualInfo *visual = glXChooseVisual(
      handle.display,
      DefaultScreen(handle.display),
      visual_attribs);

  IfNullReturn(visual, false_value, "GlXChooseVisual failed");

#endif

  const XCB_Setup *setup = xcb_get_setup(handle.connection);
  XCB_ScreenIterator iter = xcb_setup_roots_iterator(setup);
  handle.screen = iter.data;

  handle.window = xcb_generate_id(handle.connection);
  handle.root = handle.screen->root;

// TODO: Better implementation
#if RENDERER && API_OPENGL

  xcb_colormap_t colormap = xcb_generate_id(handle.connection);
  xcb_create_colormap(
      handle.connection,
      XCB_COLORMAP_ALLOC_NONE,
      colormap,
      handle.screen->root,
      visual->visualid);

  uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
  uint32_t value_list[3];
  value_list[0] = handle.screen->white_pixel;
  value_list[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;
  value_list[2] = colormap;

  xcb_create_window(
      handle.connection,
      visual->depth,
      handle.window,
      handle.screen->root,
      0, 0,
      WindowConfig::Get::width(), WindowConfig::Get::height(),
      0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT,
      visual->visualid,
      value_mask,
      value_list);

  GLXDrawable drawable = (GLXDrawable)handle.window;

  GLXContext context = glXCreateContext(
      handle.display,
      visual,
      nullptr,
      true_value);

  IfNullReturn(context, false_value, "Error: GlXChooseContext failed");

  glXMakeCurrent(
      handle.display,
      drawable,
      context);

#else

  uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  uint32_t value_list[2];
  value_list[0] = handle.screen->white_pixel; // White background
  value_list[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

  xcb_create_window(
      handle.connection,
      XCB_COPY_FROM_PARENT,
      handle.window,
      handle.screen->root,
      0, 0,
      WindowConfig::Get::width(), WindowConfig::Get::height(),
      0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT,
      handle.screen->root_visual,
      value_mask, value_list);

#endif

  X11_SetFixedSize_Hints(handle.connection, handle.window, WindowConfig::Get::width(), WindowConfig::Get::height());

  {
    const char instance_class[] = "Axel\0Engine";
    xcb_change_property(
        handle.connection,
        XCB_PROP_MODE_REPLACE,
        handle.window,
        XCB_ATOM_WM_CLASS,
        XCB_ATOM_STRING,
        8,
        sizeof(instance_class),
        instance_class);
  }

  // Atom Delete Window
  {
    handle.atoms.wm_delete_window = intern_atom(handle.connection, "WM_DELETE_WINDOW");
  }

  // Atom State Fullscreen
  {
    handle.atoms.wm_state_fullscreen = intern_atom(handle.connection, "_NET_WM_STATE_FULLSCREEN");
  }

  // Atom Protocols
  {
    handle.atoms.protocols = intern_atom(handle.connection, "WM_PROTOCOLS");
  }

  // Atom State
  {
    handle.atoms.wm_state = intern_atom(handle.connection, "_NET_WM_STATE");
  }

  // Set atoms for the window
  {
    xcb_change_property(
        handle.connection,
        XCB_PROP_MODE_REPLACE,
        handle.window,
        handle.atoms.protocols,
        XCB_ATOM_ATOM,
        32,
        1,
        &handle.atoms.wm_delete_window);

    xcb_flush(handle.connection);
  }

  // Update
  CoreWindow::update_config();

  handle.key_symbols = xcb_key_symbols_alloc(handle.connection);
  handle.is_running = true_value;

  TerminalDebug::Println(PrintColorType_Yellow, "Window XCB:");
  TerminalDebug::Println(PrintColorType_Yellow, "XCB Connection %p", handle.connection);
  TerminalDebug::Println(PrintColorType_Yellow, "XCB WINDOW %p", handle.window);

  return true_value;
}

void CoreWindow::show()
{
  // Show
  xcb_map_window(handle.connection, handle.window);
  xcb_flush(handle.connection);
}

void CoreWindow::swap_buffers()
{
#if RENDERER && API_OPENGL
  glXSwapBuffers(handle.display, (GLXDrawable)handle.window);

#elif !RENDERER
  xcb_flush(handle.connection);

#endif
}

void CoreWindow::update_config()
{
  TerminalDebug::Println(PrintColorType_Green, "%ix%i", WindowConfig::Get::width(), WindowConfig::Get::height());

  // Size config window update
  {
    int_32 dims[] = {WindowConfig::Get::width(), WindowConfig::Get::height()};
    xcb_configure_window(handle.connection, handle.window,
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, dims);
    xcb_flush(handle.connection);
  }

  // Title config window update
  {
    const char *title = WindowConfig::Get::title();
    xcb_change_property(
        handle.connection,
        XCB_PROP_MODE_REPLACE,
        handle.window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        strlen(title),
        title);

    xcb_flush(handle.connection);
  }

  // Fullscreen config window update
  {
    xcb_client_message_event_t event = {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = handle.window;
    event.type = handle.atoms.wm_state;
    event.data.data32[0] = WindowConfig::Get::is_fullscreen() ? 1 : 0; // 1 = _NET_WM_STATE_ADD, 0 = _NET_WM_STATE_REMOVE
    event.data.data32[1] = handle.atoms.wm_state_fullscreen;
    event.data.data32[2] = 0;
    event.data.data32[3] = 1;
    event.data.data32[4] = 0;

    xcb_send_event(
        handle.connection,
        false_value,
        handle.root,
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
        (const char *)&event);

    xcb_flush(handle.connection);
  }
}

void CoreWindow::change_resolution()
{
  if (WindowConfig::Get::is_fullscreen())
    WindowConfig::Set::size(handle.screen->width_in_pixels,
                            handle.screen->height_in_pixels);
  else
    WindowConfig::Set::size(WINDOW_DEFAULT_WIDTH,
                            WINDOW_DEFAULT_HEIGHT);

  CoreWindow::update_config();
}

bool CoreWindow::should_close()
{
  return !handle.is_running;
}

void CoreWindow::close()
{
  handle.is_running = false_value;
}

void CoreWindow::pool()
{
  while ((handle.event = xcb_poll_for_event(handle.connection)))
  {
    switch (handle.event->response_type & ~0x80)
    {
    case XCB_CLIENT_MESSAGE:
    {
      xcb_client_message_event_t *client_message = (xcb_client_message_event_t *)handle.event;

      if (client_message->data.data32[0] == handle.atoms.wm_delete_window)
      {
        handle.is_running = false_value;
      }
    }
    break;

    case XCB_KEY_PRESS:
    {
      xcb_key_press_event_t *kp = (xcb_key_press_event_t *)handle.event;
      xcb_keysym_t keysym = xcb_key_press_lookup_keysym(handle.key_symbols, kp, 0);
      uint_8 key = keysym & 0xFF;

      Keyboard::state().keyCurrent[key] = true_value;
    }
    break;

    case XCB_KEY_RELEASE:
    {
      xcb_key_release_event_t *kr = (xcb_key_release_event_t *)handle.event;
      xcb_keysym_t keysym = xcb_key_press_lookup_keysym(handle.key_symbols, kr, 0);
      uint_8 key = keysym & 0xFF;

      Keyboard::state().keyCurrent[key] = false_value;
    }
    break;

    default:
      break;
    }

    free(handle.event);
  }
}

void CoreWindow::terminate()
{
  if (handle.key_symbols)
  {
    xcb_key_symbols_free(handle.key_symbols);
    handle.key_symbols = nullptr;
  }

  if (handle.window)
  {
    xcb_destroy_window(handle.connection, handle.window);
    handle.window = 0;
  }

  if (handle.display)
  {
    XCloseDisplay(handle.display);
    handle.display = nullptr;
  }

  handle.connection = nullptr;
  handle.is_running = false_value;
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
