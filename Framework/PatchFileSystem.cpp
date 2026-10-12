#include "PatchFileSystem.h"
#include "framework.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

struct PatchEntry { std::wstring path; std::string name; bool enabled; };
static std::vector<PatchEntry> g_patches;
static std::wstring Base() { return Framework_GetFolder() + L"\\Patches"; }
static bool IsSafe(const std::wstring& p) {
    if (p.empty() || p[0] == L'\\' || p[0] == L'/' || p.find(L':') != std::wstring::npos) return false;
    size_t start = 0;
    while (start < p.size()) {
        size_t end = p.find_first_of(L"\\/", start);
        if (end == std::wstring::npos) end = p.size();
        if (p.substr(start, end-start) == L"..") return false;
        start = end + 1;
    }
    return true;
}
static std::wstring Normalize(std::wstring p) {
    std::replace(p.begin(), p.end(), L'/', L'\\');
    std::transform(p.begin(), p.end(), p.begin(), towlower);
    return p;
}
static void Discover(const std::wstring& root, const std::wstring& relative) {
    WIN32_FIND_DATAW d = {};
    HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &d);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(d.cFileName, L".") == 0 || wcscmp(d.cFileName, L"..") == 0) continue;
        std::wstring name = relative.empty() ? d.cFileName : relative + L"\\" + d.cFileName;
        std::wstring full = root + L"\\" + d.cFileName;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Discover(full, name);
        else {
            PatchEntry e;
            e.path = Normalize(name);
            int needed = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (needed > 0) {
                std::vector<char> buffer(needed);
                WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, buffer.data(), needed, nullptr, nullptr);
                e.name = buffer.data();
            }
            e.enabled = true;
            g_patches.push_back(e);
        }
    } while (FindNextFileW(find, &d));
    FindClose(find);
}
void PatchFileSystem_Reload() {
    g_patches.clear();
    CreateDirectoryW(Framework_GetFolder().c_str(), nullptr);
    CreateDirectoryW(Base().c_str(), nullptr);
    Discover(Base(), L"");
    Framework_Log("[PATCH] Patch file index refreshed");
}
void PatchFileSystem_Init() { PatchFileSystem_Reload(); }
int PatchFileSystem_GetPatchCount() { return static_cast<int>(g_patches.size()); }
const char* PatchFileSystem_GetPatchName(int i) { return i >= 0 && i < (int)g_patches.size() ? g_patches[i].name.c_str() : ""; }
bool PatchFileSystem_IsPatchEnabled(int i) { return i >= 0 && i < (int)g_patches.size() && g_patches[i].enabled; }
void PatchFileSystem_TogglePatch(int i) { if (i >= 0 && i < (int)g_patches.size()) g_patches[i].enabled = !g_patches[i].enabled; }
bool PatchFileSystem_TryResolveW(const wchar_t* path, wchar_t* out, int count) {
    if (!path || !out || count <= 0) return false;
    std::wstring rel = Normalize(path);
    if (!IsSafe(rel)) return false;
    for (const auto& p : g_patches) {
        if (!p.enabled || p.path != rel) continue;
        std::wstring full = Base() + L"\\" + p.path;
        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        if (full.size() + 1 > static_cast<size_t>(count)) return false;
        wcscpy_s(out, count, full.c_str());
        return true;
    }
    return false;
}
