// ForgedLCE loader core: mod discovery, lifecycle, the FLCE_Api table,
// logging and mod display strings.
//
// Mods come from three places, in this order:
//   1. built-in code mods compiled into the game (g_staticMods)
//   2. code-mod modules in the mods folder (.dll / .xex / .suprx), where the
//      platform can load them
//   3. data mods: subfolders of the mods folder with a forgedlce.json
#include "stdafx.h"
#include "FLCE_Internal.h"

#include <string.h>

namespace ForgedLCE {

namespace {

struct LoadedMod {
    std::wstring       source;  // where it came from, for log messages
    FLCE_ModInfo       info;
    DataMods::DataMod *data;    // non-NULL for data mods
};

LoadPhase              g_phase = PHASE_NOT_STARTED;
std::vector<LoadedMod> g_mods;

// Mod display strings. Written only during loading, read afterwards.
std::vector<std::wstring> g_strings;

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
    return l ? Backend::GetTile(l, x, y, z) : 0;
}

int FLCE_CALL Api_GetBlockData(FLCE_Level *l, int x, int y, int z)
{
    return l ? Backend::GetData(l, x, y, z) : 0;
}

int FLCE_CALL Api_SetBlock(FLCE_Level *l, int x, int y, int z, int id, int data)
{
    if (l == NULL || id < 0 || id > Registry::BLOCK_ID_MAX) return 0;
    if (id != 0 && !Backend::IdExists(id)) return 0;
    return Backend::SetTile(l, x, y, z, id, data) ? 1 : 0;
}

int FLCE_CALL Api_IsClientSide(FLCE_Level *l)
{
    return l && Backend::IsClientSide(l) ? 1 : 0;
}

void FLCE_CALL Api_GetEntityPos(FLCE_Entity *e, double *x, double *y, double *z)
{
    double px = 0, py = 0, pz = 0;
    if (e != NULL) Backend::GetEntityPos(e, &px, &py, &pz);
    if (x) *x = px;
    if (y) *y = py;
    if (z) *z = pz;
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

FLCE_Api g_api; // filled in LoadAllMods, lives for the process

// ---- discovery --------------------------------------------------------

bool IsDuplicate(const wchar_t *modId, const std::wstring &source)
{
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (wcscmp(g_mods[i].info.modId, modId) == 0) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", source + L": duplicate modId " + modId + L"; skipped");
            return true;
        }
    }
    return false;
}

void Announce(const LoadedMod &lm)
{
    std::wstring version = lm.info.version != NULL && lm.info.version[0] != 0
                         ? std::wstring(L" ") + lm.info.version : std::wstring();
    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Found mod " + std::wstring(lm.info.modId) + version + L" (" + lm.source + L")");
}

// Runs a code mod's entry point and records it. Returns false if the mod
// declined or is invalid.
bool AddCodeMod(FLCE_ModEntryFn entry, const std::wstring &source)
{
    LoadedMod lm;
    lm.source = source;
    lm.data = NULL;
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
    if (IsDuplicate(lm.info.modId, source)) return false;
    g_mods.push_back(lm);
    Announce(lm);
    return true;
}

void DiscoverStaticMods()
{
    for (const StaticMod *m = g_staticMods; m->modId != NULL; ++m) {
        AddCodeMod(m->entry, std::wstring(L"built in: ") + m->modId);
    }
}

void DiscoverModModules(const std::wstring &dir)
{
    const wchar_t *ext = Platform::ModModuleExtension();
    if (ext == NULL || dir.empty()) return;

    std::vector<std::wstring> files;
    Platform::ListDirectory(dir, false, files);
    std::wstring wanted(ext);
    for (size_t i = 0; i < files.size(); ++i) {
        const std::wstring &name = files[i];
        if (name.size() <= wanted.size()) continue;
        // Case-insensitive extension match (ASCII).
        bool match = true;
        for (size_t k = 0; k < wanted.size(); ++k) {
            wchar_t a = name[name.size() - wanted.size() + k], b = wanted[k];
            if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
            if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
            if (a != b) { match = false; break; }
        }
        if (!match) continue;

        std::wstring error;
        FLCE_ModEntryFn entry = Platform::LoadModModule(dir + name, error);
        if (entry == NULL) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", name + L": " + error);
            continue;
        }
        // Loaded modules are never unloaded: the game holds pointers into them.
        AddCodeMod(entry, name);
    }
}

void DiscoverDataMods(const std::wstring &dir)
{
    if (dir.empty()) return;
    std::vector<std::wstring> folders;
    Platform::ListDirectory(dir, true, folders);
    for (size_t i = 0; i < folders.size(); ++i) {
        std::wstring modDir = dir + folders[i] + Platform::PathSeparator();
        DataMods::DataMod *dm = DataMods::Load(modDir, folders[i]);
        if (dm == NULL) continue;

        std::wstring source = folders[i] + Platform::PathSeparator() + L"forgedlce.json";
        if (IsDuplicate(DataMods::ModId(dm), source)) continue; // DataMod is leaked; it's small

        LoadedMod lm;
        lm.source = source;
        lm.data = dm;
        memset(&lm.info, 0, sizeof(lm.info));
        lm.info.structSize = sizeof(lm.info);
        lm.info.modId   = DataMods::ModId(dm);
        lm.info.name    = DataMods::Name(dm);
        lm.info.version = DataMods::Version(dm);
        g_mods.push_back(lm);
        Announce(lm);
    }
}

void RunPhase(LoadPhase phase)
{
    g_phase = phase;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        LoadedMod &mod = g_mods[i];
        if (mod.data != NULL) {
            if (phase == PHASE_PREINIT) DataMods::PreInit(mod.data);
            else if (phase == PHASE_INIT) DataMods::Init(mod.data);
            continue;
        }
        FLCE_LifecycleFn fn = phase == PHASE_PREINIT ? mod.info.preInit
                            : phase == PHASE_INIT    ? mod.info.init
                            :                          mod.info.postInit;
        if (fn != NULL) fn(&g_api);
    }
}

} // namespace

// ---------------------------------------------------------------------

LoadPhase CurrentPhase() { return g_phase; }

bool IsValidModId(const wchar_t *id)
{
    if (id == NULL || id[0] == 0) return false;
    for (const wchar_t *p = id; *p; ++p) {
        bool ok = (*p >= L'a' && *p <= L'z') || (*p >= L'0' && *p <= L'9') || *p == L'_';
        if (!ok) return false;
    }
    return true;
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
    Platform::WriteLog(std::wstring(L"[ForgedLCE] [") + LevelName(level) + L"] [" + source + L"] " + message + L"\n");
}

unsigned int AddModString(const std::wstring &text)
{
    g_strings.push_back(text);
    return (unsigned int)(STRING_ID_BASE + (int)g_strings.size() - 1);
}

size_t ModStringCount() { return g_strings.size(); }

const wchar_t *GetModString(int id)
{
    if (id < STRING_ID_BASE) return NULL;
    size_t index = (size_t)(id - STRING_ID_BASE);
    return index < g_strings.size() ? g_strings[index].c_str() : NULL;
}

void LoadAllMods()
{
    if (g_phase != PHASE_NOT_STARTED) return;
    g_phase = PHASE_CONSTRUCT;

    Platform::Init();
    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Loader starting (API version " + IntToString(FLCE_API_VERSION) +
        L", " + Backend::Name() + L" backend)");

    g_api = MakeApi();
    Registry::LoadIdMap();

    // Built-in mods first, so their ids are assigned in a fixed order.
    std::wstring dir = Platform::ModsDir();
    DiscoverStaticMods();
    DiscoverModModules(dir);
    DiscoverDataMods(dir);

    RunPhase(PHASE_PREINIT);
    RunPhase(PHASE_INIT);
    RunPhase(PHASE_POSTINIT);

    Registry::SaveIdMap();
    Registry::FinishRecipes();

    g_phase = PHASE_DONE;
    Log(FLCE_LOG_INFO, L"ForgedLCE", IntToString((int)g_mods.size()) + L" mod(s) loaded");
}

} // namespace ForgedLCE
