// ForgedLCE platform services for the injected-mode tests on a PC.
// Plays the part of the Xbox 360 / PS Vita plugin platform files: a mods
// folder, file access, logging and loading code-mod modules (.dll / .so).
#include "stdafx.h"
#include "FLCE_Platform.h"
#include "PlatformHost.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <dlfcn.h>
#  include <pthread.h>
#  include <sys/stat.h>
#endif

namespace ForgedLCE {
namespace Platform {

namespace {

std::wstring g_modsDir;
std::string  g_log;

#ifdef _WIN32
CRITICAL_SECTION g_lock;
#else
pthread_mutex_t  g_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
bool g_ready = false;

void Lock()
{
#ifdef _WIN32
    if (g_ready) EnterCriticalSection(&g_lock);
#else
    pthread_mutex_lock(&g_lock);
#endif
}

void Unlock()
{
#ifdef _WIN32
    if (g_ready) LeaveCriticalSection(&g_lock);
#else
    pthread_mutex_unlock(&g_lock);
#endif
}

std::string Utf8(const std::wstring &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned long cp = (unsigned long)s[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned long)s[++i] - 0xDC00);
        }
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

FILE *Open(const std::wstring &path, const char *mode)
{
#ifdef _WIN32
    std::wstring wmode;
    for (const char *p = mode; *p; ++p) wmode += (wchar_t)*p;
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return fopen(Utf8(path).c_str(), mode);
#endif
}

} // namespace

void HostSetModsDir(const std::wstring &dir) { g_modsDir = dir; }

std::string HostLog()
{
    Lock();
    std::string copy = g_log;
    Unlock();
    return copy;
}

std::wstring HostWiden(const std::string &s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) out += (wchar_t)(unsigned char)s[i]; // ASCII paths only
    return out;
}

void Init()
{
#ifdef _WIN32
    if (!g_ready) InitializeCriticalSection(&g_lock);
#endif
    g_ready = true;
}

void WriteLog(const std::wstring &line)
{
    std::string text = Utf8(line);
    Lock();
    g_log += text;
    fputs(text.c_str(), stdout);
    Unlock();
}

std::wstring ModsDir() { return g_modsDir; }

const wchar_t *PathSeparator()
{
#ifdef _WIN32
    return L"\\";
#else
    return L"/";
#endif
}

bool ReadFile(const std::wstring &path, std::string &bytes)
{
    FILE *f = Open(path, "rb");
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
    FILE *f = Open(path, "wb");
    if (f == NULL) return false;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return fclose(f) == 0 && ok;
}

void ListDirectory(const std::wstring &dir, bool directories, std::vector<std::wstring> &names)
{
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir != directories || wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    std::string base = Utf8(dir);
    DIR *d = opendir(base.c_str());
    if (d == NULL) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        struct stat st;
        if (stat((base + e->d_name).c_str(), &st) != 0) continue;
        bool isDir = S_ISDIR(st.st_mode) != 0;
        if (isDir != directories) continue;
        names.push_back(HostWiden(e->d_name));
    }
    closedir(d);
#endif
}

const wchar_t *ModModuleExtension()
{
#if defined(_WIN32)
    return L".dll";
#elif defined(__APPLE__)
    return L".dylib";
#else
    return L".so";
#endif
}

FLCE_ModEntryFn LoadModModule(const std::wstring &path, std::wstring &error)
{
#ifdef _WIN32
    HMODULE mod = LoadLibraryW(path.c_str());
    if (mod == NULL) { error = L"could not be loaded"; return NULL; }
    FLCE_ModEntryFn entry = (FLCE_ModEntryFn)GetProcAddress(mod, FLCE_MOD_ENTRY_NAME);
#else
    void *mod = dlopen(Utf8(path).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (mod == NULL) { error = L"could not be loaded"; return NULL; }
    FLCE_ModEntryFn entry = (FLCE_ModEntryFn)dlsym(mod, FLCE_MOD_ENTRY_NAME);
#endif
    if (entry == NULL) error = L"has no flce_mod_entry export";
    return entry;
}

} // namespace Platform
} // namespace ForgedLCE
