// ForgedLCE loader core: mod discovery, lifecycle, the FLCE_Api table,
// logging and mod display strings.
//
// Platforms:
//  - every platform: mods compiled into the game (g_staticMods)
//  - Windows64 only: additionally, DLL mods from the "mods" folder next to
//    the game executable
#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.entity.h"

#include "FLCE_Internal.h"

#include <stdio.h>
#include <string.h>

namespace ForgedLCE {

namespace {

struct LoadedMod {
    std::wstring source; // "static" or the DLL file name
    FLCE_ModInfo info;
};

LoadPhase              g_phase = PHASE_NOT_STARTED;
std::vector<LoadedMod> g_mods;

// Mod display strings. Written only during loading, read afterwards.
std::vector<std::wstring> g_strings;

// Log() can be called from the main and server threads.
CRITICAL_SECTION g_logLock;
bool             g_logLockReady = false;
#ifdef _WINDOWS64
FILE            *g_logFile = NULL;
#endif

const wchar_t *LevelName(int level)
{
    switch (level) {
    case FLCE_LOG_DEBUG: return L"DEBUG";
    case FLCE_LOG_WARN:  return L"WARN";
    case FLCE_LOG_ERROR: return L"ERROR";
    default:             return L"INFO";
    }
}

// ---- API table functions --------------------------------------------

void FLCE_CALL Api_Log(const wchar_t *modId, int level, const wchar_t *message)
{
    Log(level, modId ? modId : L"?", message ? message : L"");
}

int FLCE_CALL Api_Subscribe(int type, FLCE_EventHandler handler, int priority, void *userData)
{
    return Events::Subscribe(type, handler, priority, userData) ? 1 : 0;
}

int FLCE_CALL Api_RegisterBlock(const FLCE_BlockDesc *d) { return Registry::RegisterBlock(d); }
int FLCE_CALL Api_RegisterItem(const FLCE_ItemDesc *d)   { return Registry::RegisterItem(d); }
int FLCE_CALL Api_FindId(const wchar_t *name)             { return Registry::FindId(name); }

int FLCE_CALL Api_AddShaped(int id, int count, int data, const wchar_t *const *rows, int rowCount,
                            const FLCE_RecipeKey *keys, int keyCount)
{
    return Registry::AddShapedRecipe(id, count, data, rows, rowCount, keys, keyCount);
}

int FLCE_CALL Api_AddShapeless(int id, int count, int data, const int *ids, int n)
{
    return Registry::AddShapelessRecipe(id, count, data, ids, n);
}

int FLCE_CALL Api_AddSmelting(int in, int out, int count, int data, float xp)
{
    return Registry::AddSmelting(in, out, count, data, xp);
}

int FLCE_CALL Api_GetBlock(FLCE_Level *l, int x, int y, int z)
{
    return l ? reinterpret_cast<Level *>(l)->getTile(x, y, z) : 0;
}

int FLCE_CALL Api_GetBlockData(FLCE_Level *l, int x, int y, int z)
{
    return l ? reinterpret_cast<Level *>(l)->getData(x, y, z) : 0;
}

int FLCE_CALL Api_SetBlock(FLCE_Level *l, int x, int y, int z, int id, int data)
{
    if (l == NULL || id < 0 || id > Registry::BLOCK_ID_MAX) return 0;
    if (id != 0 && Tile::tiles[id] == NULL) return 0;
    return reinterpret_cast<Level *>(l)->setTileAndData(x, y, z, id, data) ? 1 : 0;
}

int FLCE_CALL Api_IsClientSide(FLCE_Level *l)
{
    return l && reinterpret_cast<Level *>(l)->isClientSide ? 1 : 0;
}

void FLCE_CALL Api_GetEntityPos(FLCE_Entity *e, double *x, double *y, double *z)
{
    Entity *ent = reinterpret_cast<Entity *>(e);
    if (x) *x = ent ? ent->x : 0.0;
    if (y) *y = ent ? ent->y : 0.0;
    if (z) *z = ent ? ent->z : 0.0;
}

FLCE_Api MakeApi()
{
    FLCE_Api api;
    memset(&api, 0, sizeof(api));
    api.apiVersion         = FLCE_API_VERSION;
    api.structSize         = sizeof(api);
    api.log                = Api_Log;
    api.subscribe          = Api_Subscribe;
    api.registerBlock      = Api_RegisterBlock;
    api.registerItem       = Api_RegisterItem;
    api.findId             = Api_FindId;
    api.addShapedRecipe    = Api_AddShaped;
    api.addShapelessRecipe = Api_AddShapeless;
    api.addSmelting        = Api_AddSmelting;
    api.getBlock           = Api_GetBlock;
    api.getBlockData       = Api_GetBlockData;
    api.setBlock           = Api_SetBlock;
    api.isClientSide       = Api_IsClientSide;
    api.getEntityPos       = Api_GetEntityPos;
    return api;
}

FLCE_Api g_api; // filled in OnStaticCtors, lives for the process

// ---- discovery --------------------------------------------------------

bool IsValidModId(const wchar_t *id)
{
    if (id == NULL || id[0] == 0) return false;
    for (const wchar_t *p = id; *p; ++p) {
        bool ok = (*p >= L'a' && *p <= L'z') || (*p >= L'0' && *p <= L'9') || *p == L'_';
        if (!ok) return false;
    }
    return true;
}

// Runs a mod's entry point and records it. Returns false if the mod
// declined or is invalid.
bool AddMod(FLCE_ModEntryFn entry, const std::wstring &source)
{
    LoadedMod lm;
    lm.source = source;
    memset(&lm.info, 0, sizeof(lm.info));
    lm.info.structSize = sizeof(lm.info);

    if (!entry(FLCE_API_VERSION, &lm.info)) {
        Log(FLCE_LOG_INFO, L"ForgedLCE", source + L" declined to load");
        return false;
    }
    if (!IsValidModId(lm.info.modId)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", source + L": modId must be non-empty lowercase a-z, 0-9, _");
        return false;
    }
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (wcscmp(g_mods[i].info.modId, lm.info.modId) == 0) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", source + L": duplicate modId " + lm.info.modId + L"; skipped");
            return false;
        }
    }
    g_mods.push_back(lm);
    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Found mod " + std::wstring(lm.info.modId) + L" " +
        (lm.info.version ? lm.info.version : L"") + L" (" + source + L")");
    return true;
}

void DiscoverStaticMods()
{
    for (const StaticMod *m = g_staticMods; m->modId != NULL; ++m) {
        AddMod(m->entry, std::wstring(L"built in: ") + m->modId);
    }
}

void DiscoverDllMods()
{
#ifdef _WINDOWS64
    std::wstring dir = ModsDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        Log(FLCE_LOG_INFO, L"ForgedLCE", L"No DLL mods in " + dir);
        return;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = dir + fd.cFileName;

        HMODULE mod = LoadLibraryW(path.c_str());
        if (mod == NULL) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not load " + path);
            continue;
        }
        FLCE_ModEntryFn entry = (FLCE_ModEntryFn)GetProcAddress(mod, FLCE_MOD_ENTRY_NAME);
        if (entry == NULL) {
            Log(FLCE_LOG_WARN, L"ForgedLCE", std::wstring(fd.cFileName) + L" has no flce_mod_entry export; skipped");
            FreeLibrary(mod);
            continue;
        }
        // Loaded mods are never unloaded: the game holds pointers into them.
        if (!AddMod(entry, fd.cFileName)) FreeLibrary(mod);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#endif
}

void RunPhase(LoadPhase phase)
{
    g_phase = phase;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const FLCE_ModInfo &info = g_mods[i].info;
        FLCE_LifecycleFn fn = phase == PHASE_PREINIT ? info.preInit
                            : phase == PHASE_INIT    ? info.init
                            :                          info.postInit;
        if (fn != NULL) fn(&g_api);
    }
}

} // namespace

// ---------------------------------------------------------------------

LoadPhase CurrentPhase() { return g_phase; }

std::wstring ModsDir()
{
#ifdef _WINDOWS64
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    std::wstring dir(exe, n);
    size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    return dir + L"mods\\";
#else
    // Consoles: no user-writable mods folder; mods are built in.
    return std::wstring();
#endif
}

std::wstring IntToString(int value)
{
    wchar_t buf[16];
    wchar_t *p = buf + 15;
    *p = 0;
    unsigned int v = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
    do { *--p = (wchar_t)(L'0' + v % 10); v /= 10; } while (v != 0);
    if (value < 0) *--p = L'-';
    return std::wstring(p);
}

void Log(int level, const wchar_t *source, const std::wstring &message)
{
    std::wstring line = std::wstring(L"[ForgedLCE] [") + LevelName(level) + L"] [" + source + L"] " + message + L"\n";

    if (g_logLockReady) EnterCriticalSection(&g_logLock);
#ifdef _WINDOWS64
    OutputDebugStringW(line.c_str());
    if (g_logFile != NULL) {
        fputws(line.c_str(), g_logFile);
        fflush(g_logFile);
    }
#else
    // Console debug output is narrow; non-ASCII characters become '?'.
    std::string narrow;
    narrow.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        wchar_t c = line[i];
        narrow += (c > 0 && c < 128) ? (char)c : '?';
    }
    app.DebugPrintf("%s", narrow.c_str());
#endif
    if (g_logLockReady) LeaveCriticalSection(&g_logLock);
}

unsigned int AddModString(const std::wstring &text)
{
    g_strings.push_back(text);
    return (unsigned int)(STRING_ID_BASE + (int)g_strings.size() - 1);
}

const wchar_t *GetModString(int id)
{
    if (id < STRING_ID_BASE) return NULL;
    size_t index = (size_t)(id - STRING_ID_BASE);
    return index < g_strings.size() ? g_strings[index].c_str() : NULL;
}

void OnStaticCtors()
{
    if (g_phase != PHASE_NOT_STARTED) return;

    InitializeCriticalSection(&g_logLock);
    g_logLockReady = true;

#ifdef _WINDOWS64
    std::wstring dir = ModsDir();
    if (!dir.empty()) {
        CreateDirectoryW(dir.c_str(), NULL); // fine if it already exists
        g_logFile = _wfopen((dir + L"forgedlce.log").c_str(), L"wt, ccs=UTF-8");
    }
#endif

    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Loader starting (API version " + IntToString(FLCE_API_VERSION) + L")");

    g_api = MakeApi();
    g_phase = PHASE_CONSTRUCT;
    Registry::LoadIdMap();

    // Built-in mods first, so their ids are assigned in a fixed order.
    DiscoverStaticMods();
    DiscoverDllMods();

    RunPhase(PHASE_PREINIT);
    RunPhase(PHASE_INIT);
    RunPhase(PHASE_POSTINIT);

    Registry::SaveIdMap();
    Registry::FinishRecipes();

    g_phase = PHASE_DONE;
    Log(FLCE_LOG_INFO, L"ForgedLCE", IntToString((int)g_mods.size()) + L" mod(s) loaded");
}

} // namespace ForgedLCE
