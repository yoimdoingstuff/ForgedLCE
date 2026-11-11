// ForgedLCE PS Vita plugin: loads mods into the retail Minecraft: PlayStation
// Vita Edition without rebuilding the game.
//
// !! UNTESTED: written without a PS Vita. The loader core, injected backend
// and hook functions it uses are tested on a PC
// (standalone/tests/run_inject_tests.cpp); this file (paths, logging, taiHEN
// hooking) is not. docs/VITA_PLUGIN.md has the build steps (free VitaSDK).
//
// Install as a taiHEN plugin for the game's title id (ux0:tai/config.txt).
// taiHEN loads it before the game runs, so the hooks are in place in time.
//
// Files:
//   ux0:data/ForgedLCE/addresses/*.txt              one address map per game version
//   ux0:data/ForgedLCE/mods/*.suprx                  code mods
//   ux0:data/ForgedLCE/mods/<name>/forgedlce.json    data mods
//   ux0:data/ForgedLCE/forgedlce.log                 written by the plugin
#include "stdafx.h"
#include "FLCE_Internal.h"
#include "FLCE_InjectGame.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <taihen.h>

using namespace ForgedLCE;

namespace {

const char ROOT[] = "ux0:data/ForgedLCE/";

SceKernelLwMutexWork g_logLock;
bool                 g_ready = false;
SceUID               g_logFile = -1;

SceUID       g_gameModule = -1;
Inject::Addr g_segmentBase[4];

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

// ---- address resolution and hooks (taiHEN) ------------------------------

Inject::Addr Resolve(int segment, Inject::Addr value)
{
    if (segment < 0) return value;
    if (segment >= 4) return 0;
    return g_segmentBase[segment] + value; // keeps bit 0 (Thumb) for code
}

// taiHEN calls the original through its hook reference, so each hook gets a
// small "continue" function that ForgedLCE's hook code calls as the original.
enum { H_STARTUP, H_GETSTRING, H_CLIENTTICK, H_SERVERTICK, H_DESTROYBLOCK, H_ADDENTITY, H_COUNT };
tai_hook_ref_t g_ref[H_COUNT];
SceUID         g_hookId[H_COUNT];

void           C_Startup()                                  { TAI_CONTINUE(void, g_ref[H_STARTUP]); }
const wchar_t *C_GetString(void *s, int id)                 { return TAI_CONTINUE(const wchar_t *, g_ref[H_GETSTRING], s, id); }
void           C_ClientTick(void *s, bool first, bool up)   { TAI_CONTINUE(void, g_ref[H_CLIENTTICK], s, first, up); }
void           C_ServerTick(void *s)                        { TAI_CONTINUE(void, g_ref[H_SERVERTICK], s); }
bool           C_DestroyBlock(void *s, int x, int y, int z) { return TAI_CONTINUE(bool, g_ref[H_DESTROYBLOCK], s, x, y, z); }
bool           C_AddEntity(void *s, void *sp)               { return TAI_CONTINUE(bool, g_ref[H_ADDENTITY], s, sp); }

struct HookSlot { const char *name; void *cont; };
const HookSlot SLOTS[H_COUNT] = {
    { "hook.MobCategory_staticCtor", (void *)&C_Startup },
    { "hook.GetString", (void *)&C_GetString },
    { "hook.Minecraft_tick", (void *)&C_ClientTick },
    { "hook.MinecraftServer_tick", (void *)&C_ServerTick },
    { "hook.ServerPlayerGameMode_destroyBlock", (void *)&C_DestroyBlock },
    { "hook.Level_addEntity", (void *)&C_AddEntity },
};

bool InstallHook(const char *name, Inject::Addr target, void *replacement, void **original, std::wstring &error)
{
    int slot = -1;
    for (int i = 0; i < H_COUNT; ++i) if (strcmp(SLOTS[i].name, name) == 0) slot = i;
    if (slot < 0) { error = L"unknown hook"; return false; }

    // taiHEN wants the offset in segment 0 and the Thumb bit separately.
    Inject::Addr offset = target - g_segmentBase[0];
    int thumb = (int)(offset & 1);
    g_hookId[slot] = taiHookFunctionOffset(&g_ref[slot], g_gameModule, 0, (uint32_t)(offset & ~(Inject::Addr)1),
                                           thumb, replacement);
    if (g_hookId[slot] < 0) {
        char buf[32];
        sceClibSnprintf(buf, sizeof(buf), "taiHEN error 0x%08X", (unsigned int)g_hookId[slot]);
        error = Widen(buf);
        return false;
    }
    *original = SLOTS[slot].cont;
    return true;
}

bool BindMatchingMap()
{
    std::vector<std::wstring> files;
    std::wstring dir = Widen(ROOT) + L"addresses/";
    Platform::ListDirectory(dir, false, files);
    for (size_t i = 0; i < files.size(); ++i) {
        std::string text;
        if (!Platform::ReadFile(dir + files[i], text)) continue;
        Inject::AddressMap map;
        std::wstring error;
        if (!map.parse(text, error) || !Inject::BindGame(map, Resolve, error)) {
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

// Code mods (.suprx) receive this through module_start's argp and call it
// with their entry point (include/forgedlce/ForgedLCE_VitaModule.h).
FLCE_ModEntryFn g_registeredEntry = NULL;
void FLCE_CALL RegisterModEntry(FLCE_ModEntryFn entry) { g_registeredEntry = entry; }

} // namespace

// ---------------------------------------------------------------------
// Platform services (FLCE_Platform.h)
// ---------------------------------------------------------------------

namespace ForgedLCE {
namespace Platform {

void Init()
{
    if (g_ready) return;
    sceKernelCreateLwMutex(&g_logLock, "flce_log", 0, 0, NULL);
    g_ready = true;
    sceIoMkdir("ux0:data/ForgedLCE", 0777);
    g_logFile = sceIoOpen("ux0:data/ForgedLCE/forgedlce.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
}

void WriteLog(const std::wstring &line)
{
    std::string text = Narrow(line);
    if (g_ready) sceKernelLockLwMutex(&g_logLock, 1, NULL);
    sceClibPrintf("%s", text.c_str());
    if (g_logFile >= 0) sceIoWrite(g_logFile, text.data(), text.size());
    if (g_ready) sceKernelUnlockLwMutex(&g_logLock, 1);
}

std::wstring ModsDir() { return Widen(ROOT) + L"mods/"; }
const wchar_t *PathSeparator() { return L"/"; }

bool ReadFile(const std::wstring &path, std::string &bytes)
{
    SceUID fd = sceIoOpen(Narrow(path).c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    bytes.clear();
    char buf[4096];
    int n;
    while ((n = sceIoRead(fd, buf, sizeof(buf))) > 0) bytes.append(buf, (size_t)n);
    sceIoClose(fd);
    return true;
}

bool WriteFile(const std::wstring &path, const std::string &bytes)
{
    SceUID fd = sceIoOpen(Narrow(path).c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return false;
    int n = sceIoWrite(fd, bytes.data(), bytes.size());
    sceIoClose(fd);
    return n == (int)bytes.size();
}

void ListDirectory(const std::wstring &dir, bool directories, std::vector<std::wstring> &names)
{
    SceUID d = sceIoDopen(Narrow(dir).c_str());
    if (d < 0) return;
    SceIoDirent entry;
    memset(&entry, 0, sizeof(entry));
    while (sceIoDread(d, &entry) > 0) {
        bool isDir = SCE_S_ISDIR(entry.d_stat.st_mode);
        if (isDir == directories && strcmp(entry.d_name, ".") != 0 && strcmp(entry.d_name, "..") != 0) {
            names.push_back(Widen(entry.d_name));
        }
        memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(d);
}

const wchar_t *ModModuleExtension() { return L".suprx"; }

FLCE_ModEntryFn LoadModModule(const std::wstring &path, std::wstring &error)
{
    FLCE_VitaRegisterFn reg = &RegisterModEntry;
    g_registeredEntry = NULL;
    int status = 0;
    SceUID mod = sceKernelLoadStartModule(Narrow(path).c_str(), sizeof(reg), &reg, 0, NULL, &status);
    if (mod < 0) {
        error = L"could not be loaded";
        return NULL;
    }
    if (g_registeredEntry == NULL) {
        error = L"didn't register an entry point (include forgedlce/ForgedLCE_VitaModule.h in the mod)";
    }
    return g_registeredEntry;
}

} // namespace Platform
} // namespace ForgedLCE

// ---------------------------------------------------------------------
// Plugin entry points
// ---------------------------------------------------------------------

extern "C" void _start() __attribute__((weak, alias("module_start")));

// Built with -nostartfiles, so C++ global constructors (the loader's
// containers) have to be run by hand.
extern "C" void __libc_init_array(void);

extern "C" int module_start(SceSize, const void *)
{
    __libc_init_array();
    Platform::Init();

    tai_module_info_t info;
    info.size = sizeof(info);
    if (taiGetModuleInfo(TAI_MAIN_MODULE, &info) < 0) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not find the game module");
        return SCE_KERNEL_START_SUCCESS;
    }
    g_gameModule = info.modid;

    SceKernelModuleInfo moduleInfo;
    memset(&moduleInfo, 0, sizeof(moduleInfo));
    moduleInfo.size = sizeof(moduleInfo);
    if (sceKernelGetModuleInfo(info.modid, &moduleInfo) < 0) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not read the game module's segments");
        return SCE_KERNEL_START_SUCCESS;
    }
    for (int i = 0; i < 4; ++i) g_segmentBase[i] = (Inject::Addr)(size_t)moduleInfo.segments[i].vaddr;

    Log(FLCE_LOG_INFO, L"ForgedLCE", L"PS Vita plugin loaded into " + Widen(info.name));
    if (!BindMatchingMap()) return SCE_KERNEL_START_SUCCESS;
    if (!Inject::InstallHooks(InstallHook)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not hook the game's startup; mods not loaded");
    }
    return SCE_KERNEL_START_SUCCESS;
}

extern "C" int module_stop(SceSize, const void *)
{
    for (int i = 0; i < H_COUNT; ++i) {
        if (g_hookId[i] > 0) taiHookRelease(g_hookId[i], g_ref[i]);
    }
    return SCE_KERNEL_STOP_SUCCESS;
}
