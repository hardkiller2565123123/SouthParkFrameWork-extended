#include "Input.h"
#include "framework.h"
#include "ImGuiManager.h"

#include <windows.h>

static bool g_LastF1 = false;
static bool g_LastInsert = false;

void Input_Init()
{
    Framework_Log("[INPUT] Input system initialized");
    Framework_Log("[INPUT] F1 or Insert = toggle ImGui menu");
}

void Input_Update()
{
    bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;

    bool insert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if ((f1 && !g_LastF1) || (insert && !g_LastInsert))
    {
        ImGuiManager_Toggle();
    }

    g_LastF1 = f1;
    g_LastInsert = insert;
}