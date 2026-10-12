#include "framework.h"
#include "Input.h"
#include "PatchManager.h"
#include "HookManager.h"
#include "WindowManager.h"
#include "Overlay.h"
#include "MinHook.h"
#include "Config.h"
#include "ModLoader.h"
#include "Notification.h"
#include "PatchToggles.h"
#include "PatchFileSystem.h"
#include "VirtualFileSystem.h"
#include "CompanionCoop.h"
#include <cstdio>


static CRITICAL_SECTION g_LogLock;
static bool g_LogReady = false;
void Framework_Log(const char* text)
{
    if (!text || !g_LogReady) return;
    EnterCriticalSection(&g_LogLock);
    std::wstring path = Framework_GetFolder() + L"\\SouthParkFrameWork.log";
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"a");
    if (file) { fprintf(file, "%s\n", text); fclose(file); }
    printf("%s\n", text);
    LeaveCriticalSection(&g_LogLock);
}

std::wstring Framework_GetExeFolder()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);

    std::wstring p = path;
    size_t slash = p.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        p = p.substr(0, slash);

    return p;
}

std::wstring Framework_GetFolder()
{
    return Framework_GetExeFolder() + L"\\SouthParkFrameWork";
}

static void LoadLogger()
{
    CreateDirectoryW(Framework_GetFolder().c_str(), nullptr);
    InitializeCriticalSection(&g_LogLock);
    g_LogReady = true;
    if (AllocConsole()) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        SetConsoleTitleW(L"South Park: The Stick of Truth - XINPUT1_3 Mod Framework");
    }
    Framework_Log("[BOOT] XINPUT1_3.dll loaded - South Park framework");
}

static void LogDiscoveredEngineSystems()
{
    Framework_Log("[SCAN] D3D9 renderer strings detected");
    Framework_Log("[SCAN] Scaleform D3D9 HAL strings detected");
    Framework_Log("[SCAN] DrawText Manager strings detected");
    Framework_Log("[SCAN] Debug text command strings detected");
    Framework_Log("[SCAN] Texture and D3DX loader strings detected");
}

DWORD WINAPI Framework_MainThread(LPVOID)
{
    LoadLogger();

    Framework_Log("[FRAMEWORK] XINPUT1_3.dll initialized");

    if (MH_Initialize() != MH_OK)
    {
        Framework_Log("[FRAMEWORK] MinHook failed");
        return 0;
    }

    Framework_Log("[FRAMEWORK] MinHook initialized");

    LogDiscoveredEngineSystems();

    Config_Init();
    Notification_Init();
    PatchToggles_Init();
    ModLoader_Init();
    PatchFileSystem_Init();
    VFS_Init();

    HookManager_Init();
    Input_Init();
    WindowManager_Init();
    // Disabled by default: the legacy controller-2 memory hook is unverified and
    // can destabilize this game when a controller is connected.
    if (Config_GetBool("LegacyCompanionCoop", "UnsafeControllerHook", false))
        CompanionCoop_Init();
    else
        Framework_Log("[COOP] Legacy controller hook disabled (online multiplayer is separate)");

    if (MH_EnableHook(MH_ALL_HOOKS) == MH_OK)
        Framework_Log("[FRAMEWORK] Hooks enabled");
    else
        Framework_Log("[FRAMEWORK] Hook enable failed");

    Framework_Log("[FRAMEWORK] Runtime systems initialized");

    if (Config_GetBool("Mods", "Enabled", true))
        ModLoader_LoadAll();

    while (true)
    {
        Input_Update();
        WindowManager_Update();

        Sleep(16);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        CreateThread(
            NULL,
            0,
            Framework_MainThread,
            NULL,
            0,
            NULL
        );
    }

    if (reason == DLL_PROCESS_DETACH)
    {   
        // Avoid loader-lock unsafe work on process detach.
    }

    return TRUE;
}