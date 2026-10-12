# South Park: The Stick of Truth — Multiplayer Mod Framework

Single Visual Studio 2022 C++ DLL project. Build **Release | Win32** to produce `XINPUT1_3.dll`.

Place `XINPUT1_3.dll` next to the 32-bit game executable. The framework creates a `SouthParkFrameWork` folder containing its console/file log and configuration. Put asset overrides under `SouthParkFrameWork\Patches` using the original game's relative file structure. XInput functions are forwarded to the Windows system DLL; no Steam API proxy is used.

Requires the Windows SDK and June 2010 DirectX SDK (`DXSDK_DIR`) for `d3dx9.h` and `d3dx9.lib`. Bundled MinHook static library is **x86 only**. This source has not been compiled or tested on Windows here.

**Status:** Experimental framework and companion-coop hooks; this is not a complete networked multiplayer implementation. Preserve a backup of game files.

## MinHook static-link fix

This project does **not** link to `MinHook.x86.lib` and does not require `MinHook.x86.dll` at runtime. On the first build, Visual Studio runs `Framework/FetchMinHook.ps1` to download the official MinHook v1.3.3 sources, then compiles them into XINPUT1_3.dll. The first build requires internet access to GitHub; subsequent builds are offline. You can also manually place the official `src/` and `include/` trees in `Framework/third_party/minhook/`.

Rebuild **Win32**, not x64, and deploy the new `build/Debug/XINPUT1_3.dll` or `build/Release/XINPUT1_3.dll`. Remove any older XINPUT1_3.dll from the game directory before copying the new one. No MinHook.x86.dll is needed.

Note: runtime testing was not possible on this Linux environment. The framework may have unrelated issues with game-specific hooking.
