#pragma once

#include <windows.h>
#include <d3d9.h>

void ImGuiManager_Init(HWND hwnd, IDirect3DDevice9* device);
void ImGuiManager_Shutdown();

void ImGuiManager_BeginFrame();
void ImGuiManager_Render();

void ImGuiManager_OnLostDevice();
void ImGuiManager_OnResetDevice();

bool ImGuiManager_IsOpen();
void ImGuiManager_Toggle();