#include "HookManager.h"
#include "framework.h"
#include "MinHook.h"
#include "ImGuiManager.h"

#include <windows.h>
#include <d3d9.h>

typedef HRESULT(APIENTRY* Direct3DCreate9_t)(UINT SDKVersion);
typedef IDirect3D9* (WINAPI* Direct3DCreate9Real_t)(UINT SDKVersion);

typedef HRESULT(APIENTRY* CreateDevice_t)(
    IDirect3D9* self,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface
    );

typedef HRESULT(APIENTRY* EndScene_t)(IDirect3DDevice9* device);
typedef HRESULT(APIENTRY* Reset_t)(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);

static Direct3DCreate9Real_t Original_Direct3DCreate9 = NULL;
static CreateDevice_t Original_CreateDevice = NULL;
static EndScene_t Original_EndScene = NULL;
static Reset_t Original_Reset = NULL;

static bool g_DeviceHooksCreated = false;
static HWND g_GameWindow = NULL;

static HRESULT APIENTRY Hooked_EndScene(IDirect3DDevice9* device)
{
    if (device)
    {
        if (!ImGuiManager_IsOpen())
        {
            // still render topbar/debug if you want later
        }

        ImGuiManager_BeginFrame();
        ImGuiManager_Render();
    }

    return Original_EndScene(device);
}

static HRESULT APIENTRY Hooked_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
    ImGuiManager_OnLostDevice();

    HRESULT hr = Original_Reset(device, params);

    if (SUCCEEDED(hr))
        ImGuiManager_OnResetDevice();

    return hr;
}

static void HookDevice(IDirect3DDevice9* device)
{
    if (!device || g_DeviceHooksCreated)
        return;

    void** vtable = *reinterpret_cast<void***>(device);

    void* endScene = vtable[42];
    void* reset = vtable[16];

    if (MH_CreateHook(endScene, &Hooked_EndScene, reinterpret_cast<void**>(&Original_EndScene)) == MH_OK)
    {
        MH_EnableHook(endScene);
        Framework_Log("[HOOK] EndScene hooked");
    }

    if (MH_CreateHook(reset, &Hooked_Reset, reinterpret_cast<void**>(&Original_Reset)) == MH_OK)
    {
        MH_EnableHook(reset);
        Framework_Log("[HOOK] Reset hooked");
    }

    g_DeviceHooksCreated = true;
}

static HRESULT APIENTRY Hooked_CreateDevice(
    IDirect3D9* self,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface
)
{
    HRESULT hr = Original_CreateDevice(
        self,
        Adapter,
        DeviceType,
        hFocusWindow,
        BehaviorFlags,
        pPresentationParameters,
        ppReturnedDeviceInterface
    );

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface)
    {
        g_GameWindow = hFocusWindow;

        ImGuiManager_Init(g_GameWindow, *ppReturnedDeviceInterface);
        HookDevice(*ppReturnedDeviceInterface);

        Framework_Log("[HOOK] CreateDevice succeeded");
    }

    return hr;
}

static IDirect3D9* WINAPI Hooked_Direct3DCreate9(UINT SDKVersion)
{
    IDirect3D9* d3d = Original_Direct3DCreate9(SDKVersion);

    if (!d3d)
        return d3d;

    void** vtable = *reinterpret_cast<void***>(d3d);
    void* createDevice = vtable[16];

    if (MH_CreateHook(createDevice, &Hooked_CreateDevice, reinterpret_cast<void**>(&Original_CreateDevice)) == MH_OK)
    {
        MH_EnableHook(createDevice);
        Framework_Log("[HOOK] CreateDevice hooked");
    }

    return d3d;
}

void HookManager_Init()
{
    Framework_Log("[HOOK] HookManager initialized");

    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");

    if (!d3d9)
        d3d9 = LoadLibraryA("d3d9.dll");

    if (!d3d9)
    {
        Framework_Log("[HOOK] d3d9.dll not found");
        return;
    }

    void* target = GetProcAddress(d3d9, "Direct3DCreate9");

    if (!target)
    {
        Framework_Log("[HOOK] Direct3DCreate9 not found");
        return;
    }

    if (MH_CreateHook(target, &Hooked_Direct3DCreate9, reinterpret_cast<void**>(&Original_Direct3DCreate9)) == MH_OK)
    {
        MH_EnableHook(target);
        Framework_Log("[HOOK] Direct3DCreate9 hooked");
    }
    else
    {
        Framework_Log("[HOOK] Direct3DCreate9 hook failed");
    }
}

void HookManager_Shutdown()
{
    ImGuiManager_Shutdown();
}