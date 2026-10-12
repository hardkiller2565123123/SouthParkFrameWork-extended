#include "WindowManager.h"
#include "framework.h"

#include <windows.h>

static HWND g_GameWindow = NULL;

static LONG g_OriginalStyle = 0;
static LONG g_OriginalExStyle = 0;
static HWND FindGameWindow() {
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    if (pid == GetCurrentProcessId()) return foreground;
    struct Search { HWND window; DWORD pid; } search = { nullptr, GetCurrentProcessId() };
    EnumWindows([](HWND h, LPARAM param) -> BOOL {
        auto* ctx = reinterpret_cast<Search*>(param);
        DWORD candidate = 0; GetWindowThreadProcessId(h, &candidate);
        if (candidate == ctx->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) { ctx->window = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}
static RECT g_OriginalRect = { 0 };

static bool g_NoBorderEnabled = false;

void WindowManager_Init()
{
    Framework_Log("[WINDOW] WindowManager initialized");
}

void WindowManager_Update()
{
    // reserved for future updates
}

bool WindowManager_EnableNoBorder()
{
    if (g_NoBorderEnabled)
        return true;

    g_GameWindow = FindGameWindow();

    if (!g_GameWindow)
    {
        Framework_Log("[WINDOW] Failed to find game window");
        return false;
    }

    g_OriginalStyle =
        GetWindowLong(g_GameWindow, GWL_STYLE);

    g_OriginalExStyle = GetWindowLong(g_GameWindow, GWL_EXSTYLE);

    GetWindowRect(
        g_GameWindow,
        &g_OriginalRect
    );

    SetWindowLong(
        g_GameWindow,
        GWL_STYLE,
        (g_OriginalStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)) | WS_POPUP
    );

    SetWindowLong(g_GameWindow, GWL_EXSTYLE, g_OriginalExStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE));

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);

    GetMonitorInfo(
        MonitorFromWindow(
            g_GameWindow,
            MONITOR_DEFAULTTOPRIMARY
        ),
        &mi
    );

    SetWindowPos(
        g_GameWindow,
        HWND_TOP,
        mi.rcMonitor.left,
        mi.rcMonitor.top,
        mi.rcMonitor.right - mi.rcMonitor.left,
        mi.rcMonitor.bottom - mi.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW
    );

    g_NoBorderEnabled = true;

    Framework_Log("[WINDOW] No border enabled");

    return true;
}

bool WindowManager_RestoreWindow()
{
    if (!g_GameWindow)
        return false;

    SetWindowLong(g_GameWindow, GWL_STYLE, g_OriginalStyle);
    SetWindowLong(g_GameWindow, GWL_EXSTYLE, g_OriginalExStyle);

    SetWindowPos(
        g_GameWindow,
        NULL,
        g_OriginalRect.left,
        g_OriginalRect.top,
        g_OriginalRect.right - g_OriginalRect.left,
        g_OriginalRect.bottom - g_OriginalRect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW
    );

    g_NoBorderEnabled = false;

    Framework_Log("[WINDOW] Window restored");

    return true;
}