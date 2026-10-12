// South Park: The Stick of Truth — first-pass second-controller movement probe.
// For the exact x86 executable matched below. No global AI patches are performed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include "CompanionCoop.h"
#include "framework.h"
#include "MinHook.h"
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace {
struct Vec3 { float x, y, z; };
using UpdateFn = void(__thiscall*)(void*, float);
using MovementFn = void(__thiscall*)(void*, const Vec3*, float, float);
using SpeedFn = float(__thiscall*)(void*);
using OwnerFn = void*(__thiscall*)(void*);
using ComponentFn = void*(__cdecl*)(void*, int);
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

constexpr uintptr_t kImageBase = 0x400000;
constexpr DWORD kExpectedTimestamp = 0x533903EE;
constexpr DWORD kExpectedImageSize = 0x01E5B000;
constexpr uintptr_t kUpdateVA = 0x889100;
constexpr uintptr_t kMoveVA = 0x8892F0;
constexpr uintptr_t kSpeedVA = 0x889650;
constexpr uintptr_t kOwnerVA = 0x6F54E0;
constexpr uintptr_t kComponentVA = 0x70E9C0;
constexpr uintptr_t kDerivedVtableVA = 0xBE3714;
constexpr int kMovementComponent = 14;
constexpr int kAIComponent = 38;

HMODULE g_exe = nullptr;
HMODULE g_xinput = nullptr;
GetStateFn g_getState = nullptr;
UpdateFn g_original = nullptr;
MovementFn g_move = nullptr;
SpeedFn g_speed = nullptr;
OwnerFn g_owner = nullptr;
ComponentFn g_component = nullptr;
void* g_hookTarget = nullptr;
FILE* g_log = nullptr;
std::vector<void*> g_candidates;
void* g_selected = nullptr;
bool g_enabled = false;
bool g_prevF8 = false, g_prevF9 = false;
DWORD g_lastInputPoll = 0;
DWORD g_lastDiag = 0;
float g_speedScale = 1.0f;
DWORD g_controllerIndex = 1;

void Log(const char* format, ...) {
    char buffer[1024]{};
    va_list args; va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    Framework_Log(buffer);
    if (g_log) { fputs(buffer, g_log); fputc('\n', g_log); fflush(g_log); }
}
uintptr_t Rel(uintptr_t staticVA) {
    return reinterpret_cast<uintptr_t>(g_exe) + (staticVA - kImageBase);
}
bool Executable(const void* address) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    return mbi.Protect == PAGE_EXECUTE || mbi.Protect == PAGE_EXECUTE_READ ||
           mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_EXECUTE_WRITECOPY;
}
bool ValidBuild() {
    auto base = reinterpret_cast<const unsigned char*>(g_exe);
    if (!base || reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_lfanew < 0 || dos->e_lfanew > 4096) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    if (nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
        nt->OptionalHeader.SizeOfImage != kExpectedImageSize) return false;
    if (!Executable(reinterpret_cast<void*>(Rel(kUpdateVA))) ||
        !Executable(reinterpret_cast<void*>(Rel(kMoveVA)))) return false;
    // Verified against the IDA dump of 0x008892F0; don't hook a different build.
    static const unsigned char movePrologue[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C};
    return memcmp(reinterpret_cast<const void*>(Rel(kMoveVA)), movePrologue, sizeof(movePrologue)) == 0;
}

bool KeyPressed(int vk, bool& wasDown) {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}
void SelectNext() {
    if (g_candidates.empty()) { Log("[COOP] No AI-equipped movement controllers discovered yet"); return; }
    auto it = std::find(g_candidates.begin(), g_candidates.end(), g_selected);
    size_t i = it == g_candidates.end() ? 0 : ((it - g_candidates.begin() + 1) % g_candidates.size());
    g_selected = g_candidates[i];
    g_enabled = false; // Require explicit F9 for each selected actor.
    Log("[COOP] Selected movement=%p owner=%p (%zu/%zu), movement OFF", g_selected,
        g_owner(g_selected), i + 1, g_candidates.size());
}
void PollKeys() {
    DWORD now = GetTickCount();
    if (now - g_lastInputPoll < 60) return;
    g_lastInputPoll = now;
    if (KeyPressed(VK_F8, g_prevF8)) SelectNext();
    if (KeyPressed(VK_F9, g_prevF9)) {
        if (!g_selected) { Log("[COOP] F9 ignored: select an actor with F8 first"); return; }
        g_enabled = !g_enabled;
        Log("[COOP] Controller-2 injection %s on %p", g_enabled ? "ON" : "OFF", g_selected);
    }
}
void __fastcall UpdateHook(void* movement, void*, float dt) {
    // The engine invokes this on its own update thread. Keep our work on that thread.
    if (!movement || !g_original) return;
    if (g_candidates.size() < 128 &&
        *reinterpret_cast<const uintptr_t*>(movement) == Rel(kDerivedVtableVA) &&
        std::find(g_candidates.begin(), g_candidates.end(), movement) == g_candidates.end()) {
        void* owner = g_owner(movement);
        // Only consider actors with an AI component. Never auto-select a player.
        if (owner && g_component(owner, kAIComponent)) {
            g_candidates.push_back(movement);
            Log("[COOP] Candidate #%zu movement=%p owner=%p", g_candidates.size(), movement, owner);
        }
    }
    PollKeys();
    g_original(movement, dt);
    if (!g_enabled || movement != g_selected || !g_getState || !(dt > 0.0f && dt < 0.2f)) return;
    if (*reinterpret_cast<const uintptr_t*>(movement) != Rel(kDerivedVtableVA)) return;
    // Don't override restrictive engine movement states.
    if (*reinterpret_cast<const DWORD*>(reinterpret_cast<const char*>(movement) + 0x94) == 1 ||
        *reinterpret_cast<const DWORD*>(reinterpret_cast<const char*>(movement) + 0x130) != 0) return;
    XINPUT_STATE state{};
    if (g_getState(g_controllerIndex, &state) != ERROR_SUCCESS) return;
    const float x = static_cast<float>(state.Gamepad.sThumbLX) / 32768.0f;
    const float y = static_cast<float>(state.Gamepad.sThumbLY) / 32768.0f;
    const float len = std::sqrt(x*x + y*y);
    constexpr float deadzone = 0.22f;
    if (len < deadzone) return;
    // x/z plane is provisional; change mapping after testing camera/world orientation.
    const float amount = (std::min)(1.0f, (len - deadzone) / (1.0f - deadzone));
    const Vec3 dir{ x / len, 0.0f, y / len };
    float speed = g_speed(movement) * g_speedScale * amount;
    if (!std::isfinite(speed) || speed <= 0.0f || speed > 10000.0f) return;
    g_move(movement, &dir, speed, dt);
    const DWORD now = GetTickCount();
    if (now - g_lastDiag > 2000) {
        g_lastDiag = now;
        Log("[COOP] P2 movement submitted dir=(%.2f, %.2f, %.2f), speed=%.3f, dt=%.4f", dir.x, dir.y, dir.z, speed, dt);
    }
}
}

void CompanionCoop_Init() {
#if !defined(_M_IX86)
    Framework_Log("[COOP] Requires Win32/x86; disabled");
    return;
#endif
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *slash = L'\0';
    wchar_t logPath[MAX_PATH]{};
    swprintf_s(logPath, L"%s\\SouthParkFrameWork\\CompanionCoop.log", exePath);
    _wfopen_s(&g_log, logPath, L"w");
    Log("[COOP] Initializing x86 companion movement probe");
    g_candidates.clear(); g_selected = nullptr; g_enabled = false;
    g_exe = GetModuleHandleW(nullptr);
    if (!ValidBuild()) { Log("[COOP] Unsupported executable or movement bytes: hook NOT installed"); return; }
    wchar_t cfgPath[MAX_PATH]{};
    swprintf_s(cfgPath, L"%s\\SouthParkFrameWork\\CompanionCoop.ini", exePath);
    g_controllerIndex = GetPrivateProfileIntW(L"Input", L"ControllerIndex", 1, cfgPath);
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Movement", L"SpeedScale", L"1.0", value, 64, cfgPath);
    g_speedScale = static_cast<float>(_wtof(value));
    if (!std::isfinite(g_speedScale) || g_speedScale <= 0.0f || g_speedScale > 5.0f) g_speedScale = 1.0f;
    const wchar_t* xinputNames[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (auto name : xinputNames) {
        g_xinput = LoadLibraryW(name);
        if (g_xinput) { g_getState = reinterpret_cast<GetStateFn>(GetProcAddress(g_xinput, "XInputGetState")); if (g_getState) break; FreeLibrary(g_xinput); g_xinput = nullptr; }
    }
    if (!g_getState) { Log("[COOP] XInputGetState unavailable"); return; }
    g_owner = reinterpret_cast<OwnerFn>(Rel(kOwnerVA));
    g_component = reinterpret_cast<ComponentFn>(Rel(kComponentVA));
    g_move = reinterpret_cast<MovementFn>(Rel(kMoveVA));
    g_speed = reinterpret_cast<SpeedFn>(Rel(kSpeedVA));
    g_hookTarget = reinterpret_cast<void*>(Rel(kUpdateVA));
    if (MH_CreateHook(g_hookTarget, reinterpret_cast<void*>(&UpdateHook), reinterpret_cast<void**>(&g_original)) != MH_OK) {
        Log("[COOP] MH_CreateHook failed"); g_hookTarget = nullptr; return;
    }
    // Framework_MainThread enables all hooks after this initializer returns.
    Log("[COOP] Active. F8 cycle AI actors, F9 opt-in/out. Controller index=%lu, speed scale=%.2f", g_controllerIndex, g_speedScale);
}
void CompanionCoop_Shutdown() {
    g_enabled = false;
    if (g_hookTarget) { MH_DisableHook(g_hookTarget); MH_RemoveHook(g_hookTarget); g_hookTarget = nullptr; }
    Log("[COOP] Unloaded");
    if (g_log) { fclose(g_log); g_log = nullptr; }
    if (g_xinput) { FreeLibrary(g_xinput); g_xinput = nullptr; }
}

