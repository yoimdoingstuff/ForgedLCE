// ForgedLCE platform services for SOURCE builds (compiled into the game).
//
//  - Windows64: a "mods" folder next to the game executable, DLL code mods,
//    a log file and OutputDebugString.
//  - Consoles: no mods folder (code mods are built in), logging through the
//    game's app.DebugPrintf. For mods from USB or a memory card on a console
//    without recompiling, use the injected plugin instead (inject/).
#include "stdafx.h"
#include "FLCE_Internal.h"

#include <stdio.h>

namespace ForgedLCE {
namespace Platform {

namespace {
CRITICAL_SECTION g_lock;
bool             g_ready = false;
#ifdef _WINDOWS64
FILE            *g_logFile = NULL;
#endif
}

#ifdef _WINDOWS64

void Init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_lock);
    g_ready = true;
    std::wstring dir = ModsDir();
    if (!dir.empty()) {
        CreateDirectoryW(dir.c_str(), NULL); // fine if it already exists
        g_logFile = _wfopen((dir + L"forgedlce.log").c_str(), L"wt, ccs=UTF-8");
    }
}

void WriteLog(const std::wstring &line)
{
    if (g_ready) EnterCriticalSection(&g_lock);
    OutputDebugStringW(line.c_str());
    if (g_logFile != NULL) {
        fputws(line.c_str(), g_logFile);
        fflush(g_logFile);
    }
    if (g_ready) LeaveCriticalSection(&g_lock);
}

std::wstring ModsDir()
{
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    std::wstring dir(exe, n);
    size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    return dir + L"mods\\";
}

const wchar_t *PathSeparator() { return L"\\"; }

bool ReadFile(const std::wstring &path, std::string &bytes)
{
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (f == NULL) return false;
    char buf[4096];
    size_t n;
    bytes.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
    fclose(f);
    return true;
}

bool WriteFile(const std::wstring &path, const std::string &bytes)
{
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (f == NULL) return false;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return fclose(f) == 0 && ok;
}

void ListDirectory(const std::wstring &dir, bool directories, std::vector<std::wstring> &names)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir != directories) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

const wchar_t *ModModuleExtension() { return L".dll"; }

FLCE_ModEntryFn LoadModModule(const std::wstring &path, std::wstring &error)
{
    HMODULE mod = LoadLibraryW(path.c_str());
    if (mod == NULL) {
        error = L"could not be loaded";
        return NULL;
    }
    FLCE_ModEntryFn entry = (FLCE_ModEntryFn)GetProcAddress(mod, FLCE_MOD_ENTRY_NAME);
    if (entry == NULL) {
        FreeLibrary(mod);
        error = L"has no flce_mod_entry export";
    }
    return entry;
}

#else // consoles

void Init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_lock);
    g_ready = true;
}

void WriteLog(const std::wstring &line)
{
    // Console debug output is narrow; non-ASCII characters become '?'.
    std::string narrow;
    narrow.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        wchar_t c = line[i];
        narrow += (c > 0 && c < 128) ? (char)c : '?';
    }
    if (g_ready) EnterCriticalSection(&g_lock);
    app.DebugPrintf("%s", narrow.c_str());
    if (g_ready) LeaveCriticalSection(&g_lock);
}

std::wstring ModsDir() { return std::wstring(); }
const wchar_t *PathSeparator() { return L"/"; }
bool ReadFile(const std::wstring &, std::string &) { return false; }
bool WriteFile(const std::wstring &, const std::string &) { return false; }
void ListDirectory(const std::wstring &, bool, std::vector<std::wstring> &) {}
const wchar_t *ModModuleExtension() { return NULL; }

FLCE_ModEntryFn LoadModModule(const std::wstring &, std::wstring &error)
{
    error = L"code mods can't be loaded from files on this platform";
    return NULL;
}

#endif

} // namespace Platform
} // namespace ForgedLCE
