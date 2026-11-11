// ForgedLCE Xbox 360 plugin: loads mods into the retail Minecraft: Xbox 360
// Edition without rebuilding the game.
//
// !! UNTESTED: written without an Xbox 360, the XDK or xkelib. The loader
// core, injected backend, hook functions and PowerPC hook builder it uses are
// tested on a PC (standalone/tests/run_inject_tests.cpp); this file (paths,
// logging, finding the game, writing hooks to memory) is not. Expect to fix
// small things on the first real build. docs/XBOX360_PLUGIN.md has the build
// steps.
//
// Runs as a Dashlaunch plugin on a console that runs unsigned code (RGH/JTAG,
// or a Bad Update softmod with Dashlaunch loaded). When Minecraft starts it:
//   1. picks the address map in ForgedLCE\addresses\ that matches the game
//   2. hooks the game (before its startup reaches MobCategory::staticCtor)
//   3. the startup hook loads every mod from ForgedLCE\mods\
//
// Files, on the hard drive or a USB stick:
//   ForgedLCE\addresses\*.txt     one address map per game version
//   ForgedLCE\mods\*.xex          code mods (DLL-type XEX, flce_mod_entry = ordinal 1)
//   ForgedLCE\mods\<name>\forgedlce.json   data mods
//   ForgedLCE\forgedlce.log       written by the plugin
#include "stdafx.h"
#include "FLCE_Internal.h"
#include "FLCE_InjectGame.h"
#include "FLCE_HookPPC.h"

using namespace ForgedLCE;

namespace {

// Minecraft: Xbox 360 Edition. Check against your copy (Dashlaunch shows the
// running title id); different releases could differ.
const DWORD MINECRAFT_TITLE_ID = 0x584111F7;

// Drives ForgedLCE mounts itself, so it doesn't depend on Dashlaunch or the
// game to have mounted anything.
const char *const DRIVES[][2] = {
    { "FLCEHdd:", "\\Device\\Harddisk0\\Partition1" },
    { "FLCEUsb:", "\\Device\\Mass0" },
    { "FLCEUsb1:", "\\Device\\Mass1" },
};

std::string      g_root;          // e.g. "FLCEHdd:\ForgedLCE\"
CRITICAL_SECTION g_logLock;
bool             g_ready = false;
HANDLE           g_logFile = INVALID_HANDLE_VALUE;

std::string Narrow(const std::wstring &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) out += (s[i] > 0 && s[i] < 128) ? (char)s[i] : '?';
    return out;
}

std::wstring Widen(const std::string &s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) out += (wchar_t)(unsigned char)s[i];
    return out;
}

void Mount(const char *drive, const char *device)
{
    char link[64];
    sprintf_s(link, sizeof(link), "\\??\\%s", drive);
    ANSI_STRING linkName, deviceName;
    RtlInitAnsiString(&linkName, link);
    RtlInitAnsiString(&deviceName, device);
    ObDeleteSymbolicLink(&linkName);
    ObCreateSymbolicLink(&linkName, &deviceName);
}

bool DirectoryExists(const std::string &path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// ---- writing hooks into the game's code ------------------------------

// Trampolines must be in executable memory: this function's body is a pool
// of instructions that hooks overwrite.
#define FLCE_NOP8 __asm nop __asm nop __asm nop __asm nop __asm nop __asm nop __asm nop __asm nop
__declspec(naked) void TrampolinePool()
{
    __asm {
        FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8
        FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8 FLCE_NOP8
        blr
    }
}
const int POOL_WORDS = 128;
int g_poolUsed = 0;

void FlushCode(void *address, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += 4) __dcbst((int)i, address);
    __sync();
    __isync();
}

bool InstallHook(const char *, Inject::Addr target, void *replacement, void **original, std::wstring &error)
{
    PPC::Word *code = reinterpret_cast<PPC::Word *>((size_t)target);
    PPC::Word patch[4], trampoline[PPC::MAX_TRAMPOLINE_WORDS];
    int words = 0;
    const char *why = NULL;
    // On the Xbox 360 a function pointer is the address of its first instruction.
    if (!PPC::BuildHook(code, (PPC::Word)target, (PPC::Word)(size_t)replacement, patch, trampoline, &words, &why)) {
        error = Widen(why);
        return false;
    }
    if (g_poolUsed + words > POOL_WORDS) {
        error = L"out of trampoline space";
        return false;
    }
    PPC::Word *slot = reinterpret_cast<PPC::Word *>((size_t)&TrampolinePool) + g_poolUsed;
    memcpy(slot, trampoline, words * sizeof(PPC::Word));
    FlushCode(slot, words * sizeof(PPC::Word));
    g_poolUsed += words;
    *original = slot;

    memcpy(code, patch, sizeof(patch));
    FlushCode(code, sizeof(patch));
    return true;
}

Inject::Addr Resolve(int segment, Inject::Addr value)
{
    return segment < 0 ? value : 0; // the Xbox 360 game uses absolute addresses
}

// ---- attaching to the game ---------------------------------------------

// Tries every address map in ForgedLCE\addresses\ and keeps the one whose
// version check matches the running game.
bool BindMatchingMap()
{
    std::vector<std::wstring> files;
    std::wstring dir = Widen(g_root) + L"addresses\\";
    Platform::ListDirectory(dir, false, files);
    for (size_t i = 0; i < files.size(); ++i) {
        std::string text;
        if (!Platform::ReadFile(dir + files[i], text)) continue;
        Inject::AddressMap map;
        std::wstring error;
        if (!map.parse(text, error)) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", files[i] + L": " + error);
            continue;
        }
        if (!Inject::BindGame(map, Resolve, error)) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", files[i] + L": " + error);
            continue;
        }
        if (!Inject::VerifyGameVersion(error)) continue; // a map for another version
        Log(FLCE_LOG_INFO, L"ForgedLCE", L"Using address map " + files[i] + L" (" + Widen(map.game) + L")");
        return true;
    }
    Log(FLCE_LOG_ERROR, L"ForgedLCE", L"No address map in " + dir + L" matches this game version; mods not loaded");
    return false;
}

void AttachToGame()
{
    if (!BindMatchingMap()) return;
    if (Inject::StartupAlreadyPassed()) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"The game had already started when ForgedLCE attached; "
                                          L"restart Minecraft to load mods");
        return;
    }
    if (!Inject::InstallHooks(InstallHook)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not hook the game's startup; mods not loaded");
    }
}

DWORD WINAPI WatchForGame(LPVOID)
{
    for (size_t i = 0; i < sizeof(DRIVES) / sizeof(DRIVES[0]); ++i) Mount(DRIVES[i][0], DRIVES[i][1]);
    for (size_t i = 0; i < sizeof(DRIVES) / sizeof(DRIVES[0]) && g_root.empty(); ++i) {
        std::string root = std::string(DRIVES[i][0]) + "\\ForgedLCE\\";
        if (DirectoryExists(root)) g_root = root;
    }
    if (g_root.empty()) return 0; // no ForgedLCE folder anywhere: stay idle
    Platform::Init();
    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Xbox 360 plugin ready, using " + Widen(g_root));

    bool attached = false;
    for (;;) {
        DWORD title = XamGetCurrentTitleId();
        if (title == MINECRAFT_TITLE_ID && !attached) {
            attached = true;
            if (CurrentPhase() != PHASE_NOT_STARTED) {
                // The loader keeps its state for the console's uptime.
                Log(FLCE_LOG_WARN, L"ForgedLCE", L"Minecraft was restarted: reboot the console to load mods again");
            } else {
                AttachToGame();
            }
        } else if (title != MINECRAFT_TITLE_ID) {
            attached = false;
        }
        // Short sleep: the hooks must be in place before the game's startup
        // reaches MobCategory::staticCtor.
        Sleep(attached ? 500 : 1);
    }
}

} // namespace

// ---------------------------------------------------------------------
// Platform services (FLCE_Platform.h)
// ---------------------------------------------------------------------

namespace ForgedLCE {
namespace Platform {

void Init()
{
    if (g_ready) return;
    InitializeCriticalSection(&g_logLock);
    g_ready = true;
    std::string path = g_root + "forgedlce.log";
    g_logFile = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
}

void WriteLog(const std::wstring &line)
{
    std::string text = Narrow(line);
    if (g_ready) EnterCriticalSection(&g_logLock);
    DbgPrint("%s", text.c_str());
    if (g_logFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        ::WriteFile(g_logFile, text.data(), (DWORD)text.size(), &written, NULL);
        FlushFileBuffers(g_logFile);
    }
    if (g_ready) LeaveCriticalSection(&g_logLock);
}

std::wstring ModsDir() { return g_root.empty() ? std::wstring() : Widen(g_root) + L"mods\\"; }
const wchar_t *PathSeparator() { return L"\\"; }

bool ReadFile(const std::wstring &path, std::string &bytes)
{
    HANDLE f = CreateFileA(Narrow(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    bytes.clear();
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(f, buf, sizeof(buf), &n, NULL) && n > 0) bytes.append(buf, n);
    CloseHandle(f);
    return true;
}

bool WriteFile(const std::wstring &path, const std::string &bytes)
{
    HANDLE f = CreateFileA(Narrow(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = ::WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, NULL);
    CloseHandle(f);
    return ok && written == bytes.size();
}

void ListDirectory(const std::wstring &dir, bool directories, std::vector<std::wstring> &names)
{
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile((Narrow(dir) + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir != directories || strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        names.push_back(Widen(fd.cFileName));
    } while (FindNextFile(h, &fd));
    FindClose(h);
}

const wchar_t *ModModuleExtension() { return L".xex"; }

FLCE_ModEntryFn LoadModModule(const std::wstring &path, std::wstring &error)
{
    HANDLE module = NULL;
    // 8 = XEX_MODULE_FLAG_DLL
    if (XexLoadImage(Narrow(path).c_str(), 8, 0, &module) < 0 || module == NULL) {
        error = L"could not be loaded (is it a DLL-type XEX?)";
        return NULL;
    }
    void *entry = NULL;
    if (XexGetProcedureAddress(module, 1, &entry) < 0 || entry == NULL) {
        error = L"has no export with ordinal 1 (flce_mod_entry)";
        return NULL;
    }
    return reinterpret_cast<FLCE_ModEntryFn>(entry);
}

} // namespace Platform
} // namespace ForgedLCE

// ---------------------------------------------------------------------
// Plugin entry point
// ---------------------------------------------------------------------

BOOL APIENTRY DllMain(HANDLE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        // A system thread keeps running across title changes.
        HANDLE thread = NULL;
        DWORD id = 0;
        ExCreateThread(&thread, 0, &id, (VOID *)XapiThreadStartup, (LPTHREAD_START_ROUTINE)WatchForGame, NULL,
                       0x2 /* EX_CREATE_FLAG_SYSTEM */);
        if (thread != NULL) CloseHandle(thread);
    }
    return TRUE;
}
