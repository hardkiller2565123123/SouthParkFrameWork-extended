#include "ImGuiManager.h"

#include "framework.h"
#include "Config.h"
#include "ModLoader.h"
#include "PatchFileSystem.h"
#include "VirtualFileSystem.h"
#include "WindowManager.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx9.h"

#include <windows.h>

static bool g_Initialized = false;
static bool g_Open = false;
static bool g_ShowStatusBar = false;
static HWND g_Hwnd = NULL;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static WNDPROC g_PreviousWndProc = NULL;
static LRESULT CALLBACK FrameworkWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_Open && ImGui::GetCurrentContext() != NULL) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_CHAR)
            return 0;
    }
    return g_PreviousWndProc ? CallWindowProcW(g_PreviousWndProc, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}
static IDirect3DDevice9* g_Device = NULL;

static int g_Page = 0;
static bool g_NoBorder = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

bool ImGuiManager_IsOpen()
{
    return g_Open;
}

void ImGuiManager_Toggle()
{
    g_Open = !g_Open;

    if (g_Open)
        while (ShowCursor(TRUE) < 0) {}
}

static void ApplySouthParkTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;

    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.ItemInnerSpacing = ImVec2(6, 5);

    ImVec4* c = style.Colors;

    c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.86f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.45f, 0.40f, 1.00f);

    c[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.040f, 0.035f, 0.96f);
    c[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.060f, 0.050f, 0.95f);
    c[ImGuiCol_PopupBg] = ImVec4(0.045f, 0.050f, 0.045f, 0.98f);

    c[ImGuiCol_Border] = ImVec4(0.18f, 0.55f, 0.16f, 0.85f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    c[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.085f, 0.070f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.35f, 0.12f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.55f, 0.16f, 1.00f);

    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.08f, 0.06f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.26f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);

    c[ImGuiCol_Button] = ImVec4(0.10f, 0.30f, 0.09f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.48f, 0.13f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.70f, 0.18f, 1.00f);

    c[ImGuiCol_Header] = ImVec4(0.10f, 0.30f, 0.09f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.48f, 0.13f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.70f, 0.18f, 1.00f);

    c[ImGuiCol_Tab] = ImVec4(0.08f, 0.18f, 0.07f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.55f, 0.16f, 1.00f);
    c[ImGuiCol_TabActive] = ImVec4(0.13f, 0.40f, 0.11f, 1.00f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.08f, 0.05f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.08f, 0.20f, 0.07f, 1.00f);

    c[ImGuiCol_CheckMark] = ImVec4(0.35f, 1.00f, 0.25f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.65f, 0.16f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 1.00f, 0.25f, 1.00f);
}

void ImGuiManager_Init(HWND hwnd, IDirect3DDevice9* device)
{
    if (g_Initialized)
        return;

    g_Hwnd = hwnd;
    g_Device = device;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;

    ApplySouthParkTheme();

    ImGui_ImplWin32_Init(g_Hwnd);
    ImGui_ImplDX9_Init(g_Device);
    if (g_Hwnd && IsWindow(g_Hwnd))
        g_PreviousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&FrameworkWndProc)));

    g_NoBorder = Config_GetBool("Window", "NoBorder", false);
    g_ShowStatusBar = Config_GetBool("Overlay", "StatusBar", false);

    g_Initialized = true;

    Framework_Log("[IMGUI] ImGuiManager initialized");
}

void ImGuiManager_Shutdown()
{
    if (!g_Initialized)
        return;

    if (g_Hwnd && IsWindow(g_Hwnd) && g_PreviousWndProc) {
        SetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_PreviousWndProc));
        g_PreviousWndProc = NULL;
    }
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    g_Initialized = false;

    Framework_Log("[IMGUI] ImGuiManager shutdown");
}

// Release ImGui's D3D9 resources before IDirect3DDevice9::Reset.
void ImGuiManager_OnLostDevice()
{
    if (!g_Initialized || ImGui::GetCurrentContext() == NULL)
        return;

    ImGui_ImplDX9_InvalidateDeviceObjects();
}

// Recreate GPU resources once a device reset has succeeded.
void ImGuiManager_OnResetDevice()
{
    if (!g_Initialized || ImGui::GetCurrentContext() == NULL)
        return;

    ImGui_ImplDX9_CreateDeviceObjects();
}

void ImGuiManager_BeginFrame()
{
    if (!g_Initialized)
        return;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

static void DrawTopBar()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 28), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("SPFW_TopBar", NULL, flags);

    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.25f, 1.0f), "SouthParkFrameWork");
    ImGui::SameLine();

    if (ImGui::Button("Home"))
        g_Page = 0;

    ImGui::SameLine();

    if (ImGui::Button("Patches"))
        g_Page = 1;

    ImGui::SameLine();

    if (ImGui::Button("Mods"))
        g_Page = 2;

    ImGui::SameLine();

    if (ImGui::Button("Tools"))
        g_Page = 3;

    ImGui::SameLine();

    if (ImGui::Button("Debug"))
        g_Page = 4;

    ImGui::SameLine();
    if (ImGui::Button("Multiplayer"))
        g_Page = 5;

    ImGui::SameLine(ImGui::GetWindowWidth() - 210);
    ImGui::Text("v1.00 | F1 Menu");

    ImGui::End();
}

static void DrawHomePage()
{
    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.25f, 1.0f), "Framework Status");
    ImGui::Separator();

    ImGui::Text("Renderer: Direct3D9");
    ImGui::Text("Framework: initialized");
    ImGui::Text("VFS: %s", VFS_IsEnabled() ? "enabled" : "disabled");
    ImGui::Text("DLL Mods Loaded: %d", ModLoader_GetLoadedCount());
    ImGui::Text("File Patches Found: %d", PatchFileSystem_GetPatchCount());

    ImGui::Spacing();

    if (ImGui::Checkbox("Show framework status bar", &g_ShowStatusBar))
        Config_SetBool("Overlay", "StatusBar", g_ShowStatusBar);

    if (ImGui::Checkbox("Borderless fullscreen window", &g_NoBorder))
    {
        Config_SetBool("Window", "NoBorder", g_NoBorder);

        if (g_NoBorder)
            WindowManager_EnableNoBorder();
        else
            WindowManager_RestoreWindow();
    }

    bool vfs = VFS_IsEnabled();

    if (ImGui::Checkbox("Virtual File System", &vfs))
        VFS_SetEnabled(vfs);
}

static void DrawPatchesPage()
{
    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.25f, 1.0f), "File Replacement Patches");
    ImGui::Separator();

    if (ImGui::Button("Refresh Patches"))
        PatchFileSystem_Reload();

    ImGui::SameLine();

    ImGui::Text("Folder: SouthParkFrameWork\\Patches");

    ImGui::Spacing();

    int count = PatchFileSystem_GetPatchCount();

    if (count <= 0)
    {
        ImGui::TextDisabled("No patches found.");
        return;
    }

    for (int i = 0; i < count; i++)
    {
        bool enabled = PatchFileSystem_IsPatchEnabled(i);
        const char* name = PatchFileSystem_GetPatchName(i);

        ImGui::PushID(i);

        if (ImGui::Checkbox(name, &enabled))
            PatchFileSystem_TogglePatch(i);

        ImGui::SameLine();
        ImGui::TextDisabled("file patch");

        ImGui::PopID();
    }
}

static void DrawModsPage()
{
    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.25f, 1.0f), "DLL Mods");
    ImGui::Separator();

    if (ImGui::Button("Reload DLL Mods"))
        ModLoader_Reload();

    ImGui::SameLine();

    ImGui::Text("Folder: SouthParkFrameWork\\Mods");

    ImGui::Spacing();

    ImGui::Text("Loaded DLL Mods: %d / %d detected", ModLoader_GetLoadedCount(), ModLoader_GetDetectedCount());
    for (int i = 0; i < ModLoader_GetDetectedCount(); ++i) {
        ImGui::PushID(i);
        bool enabled = ModLoader_IsModEnabled(i);
        if (ImGui::Checkbox(ModLoader_GetModName(i), &enabled)) ModLoader_ToggleMod(i);
        ImGui::PopID();
    }
    ImGui::TextDisabled("Changes to DLL mods may require restarting the game.");
}

static void DrawToolsPage()
{
    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.25f, 1.0f), "Tools");
    ImGui::Separator();

    ImGui::BulletText("Asset Browser");
    ImGui::BulletText("Texture Browser");
    ImGui::BulletText("File Redirect Viewer");
    ImGui::BulletText("Pattern Scanner");
    ImGui::BulletText("Memory Tools");

    ImGui::TextDisabled("Coming soon.");
}

static void DrawDebugPage()
{
    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.25f, 1.0f), "Debug");
    ImGui::Separator();

    ImGuiIO& io = ImGui::GetIO();

    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Text("Mouse: %.0f, %.0f", io.MousePos.x, io.MousePos.y);
    ImGui::Text("Menu Open: %s", g_Open ? "yes" : "no");
    ImGui::Text("VFS: %s", VFS_IsEnabled() ? "enabled" : "disabled");
    ImGui::Text("Patches: %d", PatchFileSystem_GetPatchCount());
    ImGui::Text("Mods Loaded: %d", ModLoader_GetLoadedCount());
}

static void DrawMultiplayerPage()
{
    ImGui::Text("Online Multiplayer - development status");
    ImGui::Separator();
    ImGui::TextWrapped("Target: two PCs, independent characters and locations. No online gameplay or host/join protocol has been implemented yet.");
    ImGui::TextDisabled("Legacy controller companion hook: disabled by default for stability.");
    ImGui::TextDisabled("Native menu integration and player/world replication are still required.");
}

static void DrawMainWindow()
{
    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 70), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("SouthParkFrameWork Control Center", &g_Open))
    {
        ImGui::End();
        return;
    }

    ImGui::Columns(2, NULL, false);
    ImGui::SetColumnWidth(0, 160);

    if (ImGui::Selectable("Home", g_Page == 0))
        g_Page = 0;

    if (ImGui::Selectable("Patches", g_Page == 1))
        g_Page = 1;

    if (ImGui::Selectable("Mods", g_Page == 2))
        g_Page = 2;

    if (ImGui::Selectable("Tools", g_Page == 3))
        g_Page = 3;

    if (ImGui::Selectable("Debug", g_Page == 4))
        g_Page = 4;

    if (ImGui::Selectable("Multiplayer", g_Page == 5))
        g_Page = 5;

    ImGui::NextColumn();

    if (g_Page == 0)
        DrawHomePage();
    else if (g_Page == 1)
        DrawPatchesPage();
    else if (g_Page == 2)
        DrawModsPage();
    else if (g_Page == 3)
        DrawToolsPage();
    else if (g_Page == 4)
        DrawDebugPage();
    else if (g_Page == 5)
        DrawMultiplayerPage();

    ImGui::Columns(1);

    ImGui::End();
}

void ImGuiManager_Render()
{
    if (!g_Initialized)
        return;

    if (g_Open || g_ShowStatusBar) DrawTopBar();

    if (g_Open)
        DrawMainWindow();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}