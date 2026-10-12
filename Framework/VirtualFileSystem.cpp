#include "VirtualFileSystem.h"
#include "framework.h"
#include "Config.h"
#include "PatchFileSystem.h"
#include "MinHook.h"

#include <windows.h>
#include <string>

typedef HANDLE(WINAPI* CreateFileA_t)(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

typedef HANDLE(WINAPI* CreateFileW_t)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

typedef DWORD(WINAPI* GetFileAttributesA_t)(LPCSTR);
typedef DWORD(WINAPI* GetFileAttributesW_t)(LPCWSTR);

static CreateFileA_t Original_CreateFileA = NULL;
static CreateFileW_t Original_CreateFileW = NULL;
static GetFileAttributesA_t Original_GetFileAttributesA = NULL;
static GetFileAttributesW_t Original_GetFileAttributesW = NULL;

static bool g_Enabled = true;

static std::wstring NormalizePath(std::wstring path)
{
    for (size_t i = 0; i < path.size(); i++)
    {
        if (path[i] == L'/')
            path[i] = L'\\';
    }

    return path;
}

static bool EndsWithI(const std::wstring& text, const wchar_t* ext)
{
    size_t len = wcslen(ext);

    if (text.length() < len)
        return false;

    return _wcsicmp(text.c_str() + text.length() - len, ext) == 0;
}

static bool ContainsI(const std::wstring& text, const wchar_t* find)
{
    std::wstring a = text;
    std::wstring b = find;

    for (size_t i = 0; i < a.size(); i++)
        a[i] = towlower(a[i]);

    for (size_t i = 0; i < b.size(); i++)
        b[i] = towlower(b[i]);

    return a.find(b) != std::wstring::npos;
}

static std::wstring GetRelativePath(const wchar_t* requested)
{
    if (!requested)
        return L"";

    std::wstring input = NormalizePath(requested);
    std::wstring exeFolder = NormalizePath(Framework_GetExeFolder());

    while (!input.empty() && input[0] == L'\\')
        input.erase(input.begin());

    if (input.find(exeFolder) == 0)
    {
        input = input.substr(exeFolder.length());

        while (!input.empty() && input[0] == L'\\')
            input.erase(input.begin());
    }

    size_t dataPos = input.find(L"data\\");

    if (dataPos != std::wstring::npos)
        input = input.substr(dataPos);

    return input;
}

static bool IsReadOnlyRequest(DWORD desiredAccess, DWORD creationDisposition)
{
    if (desiredAccess & GENERIC_WRITE)
        return false;

    if (desiredAccess & FILE_WRITE_DATA)
        return false;

    if (desiredAccess & FILE_APPEND_DATA)
        return false;

    if (
        creationDisposition == CREATE_ALWAYS ||
        creationDisposition == CREATE_NEW ||
        creationDisposition == TRUNCATE_EXISTING
        )
    {
        return false;
    }

    return true;
}

static bool IsSafeRedirectAsset(const std::wstring& rel)
{
    if (rel.empty())
        return false;

    if (rel.find(L"data\\") != 0)
        return false;

    if (ContainsI(rel, L"SouthParkFrameWork\\"))
        return false;

    if (
        EndsWithI(rel, L".dll") ||
        EndsWithI(rel, L".exe") ||
        EndsWithI(rel, L".ini") ||
        EndsWithI(rel, L".log")
        )
    {
        return false;
    }

    // Do NOT redirect bk2. SkipIntro handles videos as "missing".
    if (EndsWithI(rel, L".bk2"))
        return false;

    return
        EndsWithI(rel, L".gfx") ||
        EndsWithI(rel, L".ogd") ||
        EndsWithI(rel, L".xml") ||
        EndsWithI(rel, L".txt") ||

        // Texture formats
        EndsWithI(rel, L".dds") ||
        EndsWithI(rel, L".tga") ||
        EndsWithI(rel, L".png") ||
        EndsWithI(rel, L".jpg") ||
        EndsWithI(rel, L".jpeg") ||
        EndsWithI(rel, L".bmp");
}

static bool IsSkipIntroMovie(const std::wstring& rel)
{
    if (rel.find(L"data\\movies\\") != 0)
        return false;

    if (!EndsWithI(rel, L".bk2"))
        return false;

    return
        ContainsI(rel, L"000_ubisoft") ||
        ContainsI(rel, L"000_obsidian") ||
        ContainsI(rel, L"000_autodesk") ||
        ContainsI(rel, L"000_spds");
}

static bool IsSkipIntroEnabled()
{
    return Config_GetBool("Patch_SkipIntro", "Enabled", false);
}

static bool TryResolvePatchFileW(const wchar_t* requested, std::wstring& outPath)
{
    if (!g_Enabled || !requested)
        return false;

    std::wstring rel = GetRelativePath(requested);

    if (!IsSafeRedirectAsset(rel))
        return false;

    wchar_t patchPath[MAX_PATH * 2];

    if (PatchFileSystem_TryResolveW(rel.c_str(), patchPath, MAX_PATH * 2))
    {
        outPath = patchPath;
        return true;
    }

    return false;
}

static void PretendFileMissing()
{
    SetLastError(ERROR_FILE_NOT_FOUND);
}

static HANDLE WINAPI Hooked_CreateFileW(
    LPCWSTR fileName,
    DWORD desiredAccess,
    DWORD shareMode,
    LPSECURITY_ATTRIBUTES security,
    DWORD creationDisposition,
    DWORD flags,
    HANDLE templateFile
)
{
    std::wstring rel = GetRelativePath(fileName);

    if (
        g_Enabled &&
        IsReadOnlyRequest(desiredAccess, creationDisposition) &&
        IsSkipIntroEnabled() &&
        IsSkipIntroMovie(rel)
        )
    {
        PretendFileMissing();
        return INVALID_HANDLE_VALUE;
    }

    if (IsReadOnlyRequest(desiredAccess, creationDisposition))
    {
        std::wstring redirected;

        if (TryResolvePatchFileW(fileName, redirected))
        {
            return Original_CreateFileW(
                redirected.c_str(),
                desiredAccess,
                shareMode,
                security,
                creationDisposition,
                flags,
                templateFile
            );
        }
    }

    return Original_CreateFileW(
        fileName,
        desiredAccess,
        shareMode,
        security,
        creationDisposition,
        flags,
        templateFile
    );
}

static HANDLE WINAPI Hooked_CreateFileA(
    LPCSTR fileName,
    DWORD desiredAccess,
    DWORD shareMode,
    LPSECURITY_ATTRIBUTES security,
    DWORD creationDisposition,
    DWORD flags,
    HANDLE templateFile
)
{
    wchar_t wide[MAX_PATH * 2] = { 0 };

    if (fileName)
    {
        MultiByteToWideChar(
            CP_ACP,
            0,
            fileName,
            -1,
            wide,
            MAX_PATH * 2
        );
    }

    std::wstring rel = GetRelativePath(wide);

    if (
        g_Enabled &&
        IsReadOnlyRequest(desiredAccess, creationDisposition) &&
        IsSkipIntroEnabled() &&
        IsSkipIntroMovie(rel)
        )
    {
        PretendFileMissing();
        return INVALID_HANDLE_VALUE;
    }

    if (fileName && IsReadOnlyRequest(desiredAccess, creationDisposition))
    {
        std::wstring redirected;

        if (TryResolvePatchFileW(wide, redirected))
        {
            char ansi[MAX_PATH * 2];

            WideCharToMultiByte(
                CP_ACP,
                0,
                redirected.c_str(),
                -1,
                ansi,
                MAX_PATH * 2,
                NULL,
                NULL
            );

            return Original_CreateFileA(
                ansi,
                desiredAccess,
                shareMode,
                security,
                creationDisposition,
                flags,
                templateFile
            );
        }
    }

    return Original_CreateFileA(
        fileName,
        desiredAccess,
        shareMode,
        security,
        creationDisposition,
        flags,
        templateFile
    );
}

static DWORD WINAPI Hooked_GetFileAttributesW(LPCWSTR fileName)
{
    std::wstring rel = GetRelativePath(fileName);

    if (
        g_Enabled &&
        IsSkipIntroEnabled() &&
        IsSkipIntroMovie(rel)
        )
    {
        PretendFileMissing();
        return INVALID_FILE_ATTRIBUTES;
    }

    std::wstring redirected;

    if (TryResolvePatchFileW(fileName, redirected))
        return Original_GetFileAttributesW(redirected.c_str());

    return Original_GetFileAttributesW(fileName);
}

static DWORD WINAPI Hooked_GetFileAttributesA(LPCSTR fileName)
{
    wchar_t wide[MAX_PATH * 2] = { 0 };

    if (fileName)
    {
        MultiByteToWideChar(
            CP_ACP,
            0,
            fileName,
            -1,
            wide,
            MAX_PATH * 2
        );
    }

    std::wstring rel = GetRelativePath(wide);

    if (
        g_Enabled &&
        IsSkipIntroEnabled() &&
        IsSkipIntroMovie(rel)
        )
    {
        PretendFileMissing();
        return INVALID_FILE_ATTRIBUTES;
    }

    std::wstring redirected;

    if (fileName && TryResolvePatchFileW(wide, redirected))
    {
        char ansi[MAX_PATH * 2];

        WideCharToMultiByte(
            CP_ACP,
            0,
            redirected.c_str(),
            -1,
            ansi,
            MAX_PATH * 2,
            NULL,
            NULL
        );

        return Original_GetFileAttributesA(ansi);
    }

    return Original_GetFileAttributesA(fileName);
}

static void HookFunction(HMODULE module, const char* name, void* detour, void** original)
{
    void* target = GetProcAddress(module, name);

    if (!target)
        return;

    if (MH_CreateHook(target, detour, original) == MH_OK)
    {
        MH_EnableHook(target);

        char buffer[128];
        sprintf_s(buffer, sizeof(buffer), "[VFS] %s hooked", name);
        Framework_Log(buffer);
    }
}

void VFS_Init()
{
    g_Enabled = Config_GetBool("VFS", "Enabled", true);

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");

    if (!kernel32)
    {
        Framework_Log("[VFS] kernel32.dll not found");
        return;
    }

    HookFunction(kernel32, "CreateFileA", &Hooked_CreateFileA, reinterpret_cast<void**>(&Original_CreateFileA));
    HookFunction(kernel32, "CreateFileW", &Hooked_CreateFileW, reinterpret_cast<void**>(&Original_CreateFileW));
    HookFunction(kernel32, "GetFileAttributesA", &Hooked_GetFileAttributesA, reinterpret_cast<void**>(&Original_GetFileAttributesA));
    HookFunction(kernel32, "GetFileAttributesW", &Hooked_GetFileAttributesW, reinterpret_cast<void**>(&Original_GetFileAttributesW));

    CreateDirectoryW((Framework_GetFolder() + L"\\Patches").c_str(), NULL);

    Framework_Log(
        g_Enabled
        ? "[VFS] Virtual file system initialized enabled"
        : "[VFS] Virtual file system initialized disabled"
    );
}

void VFS_Shutdown()
{}

bool VFS_IsEnabled()
{
    return g_Enabled;
}

void VFS_SetEnabled(bool enabled)
{
    g_Enabled = enabled;

    Config_SetBool("VFS", "Enabled", enabled);

    Framework_Log(
        enabled
        ? "[VFS] Virtual files enabled"
        : "[VFS] Virtual files disabled"
    );
}