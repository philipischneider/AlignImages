#include "core/DpiUtilsWin.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <shellscalingapi.h>

namespace align
{
namespace
{
float ClampScale(float value)
{
    if (value < 0.75f)
    {
        return 0.75f;
    }
    if (value > 3.0f)
    {
        return 3.0f;
    }
    return value;
}
} // namespace

void EnablePerMonitorDpiAwareness()
{
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    const auto user32 = LoadLibraryW(L"user32.dll");
    if (user32 != nullptr)
    {
        const auto setContext =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext != nullptr)
        {
            setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            FreeLibrary(user32);
            return;
        }
        FreeLibrary(user32);
    }

    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
}

float GetWindowDpiScale(GLFWwindow* window)
{
    if (window == nullptr)
    {
        return 1.0f;
    }

    const HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr)
    {
        return 1.0f;
    }

    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const auto user32 = LoadLibraryW(L"user32.dll");
    if (user32 != nullptr)
    {
        const auto getDpiForWindow =
            reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow != nullptr)
        {
            const UINT dpi = getDpiForWindow(hwnd);
            FreeLibrary(user32);
            return ClampScale(static_cast<float>(dpi) / 96.0f);
        }
        FreeLibrary(user32);
    }

    HDC screen = GetDC(hwnd);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(hwnd, screen);
    return ClampScale(static_cast<float>(dpi) / 96.0f);
}

float ResolveUiScaleForWindow(GLFWwindow* window, const UiPreferences& preferences)
{
    if (preferences.dpiMode == DpiMode::Manual)
    {
        return ClampScale(preferences.dpiOverride);
    }

    return GetWindowDpiScale(window);
}
} // namespace align

