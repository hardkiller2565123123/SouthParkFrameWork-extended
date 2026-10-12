#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define XINPUTAPI
#include <Xinput.h>

static HMODULE RealXInput() {
    static HMODULE module = []() -> HMODULE {
        wchar_t directory[MAX_PATH] = {};
        if (!GetSystemDirectoryW(directory, MAX_PATH)) return nullptr;
        if (wcscat_s(directory, L"\\xinput1_3.dll") != 0) return nullptr;
        return LoadLibraryW(directory);
    }();
    return module;
}
template <typename T> static T Resolve(const char* name) {
    HMODULE mod = RealXInput();
    return mod ? reinterpret_cast<T>(GetProcAddress(mod, name)) : nullptr;
}
extern "C" DWORD WINAPI XInputGetState(DWORD i, XINPUT_STATE* s) {
    auto fn = Resolve<decltype(&XInputGetState)>("XInputGetState");
    return fn ? fn(i,s) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI XInputSetState(DWORD i, XINPUT_VIBRATION* v) {
    auto fn = Resolve<decltype(&XInputSetState)>("XInputSetState");
    return fn ? fn(i,v) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD flags, XINPUT_CAPABILITIES* c) {
    auto fn = Resolve<decltype(&XInputGetCapabilities)>("XInputGetCapabilities");
    return fn ? fn(i,flags,c) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI XInputEnable(BOOL enabled) {
    auto fn = Resolve<decltype(&XInputEnable)>("XInputEnable");
    if (fn) fn(enabled);
}
extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD i, GUID* render, GUID* capture) {
    auto fn = Resolve<decltype(&XInputGetDSoundAudioDeviceGuids)>("XInputGetDSoundAudioDeviceGuids");
    return fn ? fn(i,render,capture) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI XInputGetBatteryInformation(DWORD i, BYTE type, XINPUT_BATTERY_INFORMATION* out) {
    auto fn = Resolve<decltype(&XInputGetBatteryInformation)>("XInputGetBatteryInformation");
    return fn ? fn(i,type,out) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI XInputGetKeystroke(DWORD i, DWORD reserved, PXINPUT_KEYSTROKE key) {
    auto fn = Resolve<decltype(&XInputGetKeystroke)>("XInputGetKeystroke");
    return fn ? fn(i,reserved,key) : ERROR_DEVICE_NOT_CONNECTED;
}
