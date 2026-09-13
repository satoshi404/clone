#include <backend/window.hpp>

#include <core/types.hpp>
#include <core/debug.hpp>
#include <core/debug.hpp>
#include <pipeline.hpp>
#include <backend/window.hpp>

#include <core/types.hpp>
#include <core/debug.hpp>
#include <pipeline.hpp>

#include <backend/keyboard.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PIPELINE_WINDOWS

#include <windows.h>

namespace Window
{
    WNDCLASSW wndClass;
    HWND hwnd;
    bool is_running = false;
}

LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch( msg )
    {
        case WM_KEYDOWN:
		case WM_KEYUP:
        {
            // TODO:
            if ( wParam == VK_ESCAPE ) Window::is_running = false;

            Keyboard::state().keyCurrent[wParam] = ( msg == WM_KEYDOWN );
			Keyboard::state().keyRepeat[wParam] = ( msg == WM_KEYDOWN );

            return 0;
        }

        default: { return DefWindowProcW( hwnd, msg, wParam, lParam ); }
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        Window::is_running = false;
        return 0;
    }

    return CallWindowProcW( InputProc, hwnd, msg, wParam, lParam);
}

bool Window::init()
{
    HINSTANCE Instance = GetModuleHandleW(nullptr);

    wndClass = {};
    wndClass.style = CS_HREDRAW | CS_VREDRAW;

    #if GRAPHICS_API_OPENGL
        wndClass.style = 0;
    #endif

    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.lpfnWndProc = WindowProc;
    wndClass.hInstance = Instance;
    wndClass.lpszClassName = L"CloneClassWindow";

    if (!RegisterClassW(&wndClass))
    {
        return false;
    }

    UINT width = static_cast<UINT>(WindowConfig::get_width());
    UINT height = static_cast<UINT>(WindowConfig::get_height());

    hwnd = CreateWindowExW(
       // #if GRAPHICS_API_D3D12
        //    WS_EX_NOREDIRECTIONBITMAP,
        //#else
        0,

        wndClass.lpszClassName,
        L"Clone",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        nullptr,
        nullptr,
        Instance,
        nullptr
    );

    is_running = true;
    return true;
}

void Window::show()
{
    ShowWindow(hwnd, SW_NORMAL);
    UpdateWindow(hwnd);
}

void Window::pool()
{
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Window::terminate()
{
    if (hwnd)
    {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
    UnregisterClassW(wndClass.lpszClassName, GetModuleHandleW(nullptr));
    is_running = false;
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////