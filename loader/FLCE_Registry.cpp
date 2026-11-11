// ForgedLCE registry (core): validates registrations, assigns ids, keeps the
// name->id map, and validates recipes. Creating the actual game objects is
// the backend's job (FLCE_Backend.h).
//
// Id persistence: where the platform has a mods folder, forgedlce_ids.txt in
// it keeps each name on the same id across runs. Without one (consoles
// running a source build), built-in mods register in a fixed order, so ids
// stay the same as long as the mod list and each mod's registrations don't
// change. (A per-world id map in the save is planned.)
#include "stdafx.h"
#include "FLCE_Internal.h"

#include <stdlib.h>

namespace ForgedLCE {
namespace Registry {

namespace {

std::map<std::wstring, int> g_savedIds;    // from forgedlce_ids.txt
std::map<std::wstring, int> g_assignedIds; // registered this run

std::wstring IdMapPath()
{
    std::wstring dir = Platform::ModsDir();
    return dir.empty() ? std::wstring() : dir + L"forgedlce_ids.txt";
}

// "modid:name" in printable ASCII: exactly one colon, not at either end,
// no '=' (the id file's separator) and no spaces.
bool IsValidRegistryName(const wchar_t *name)
{
    if (name == NULL || name[0] == 0) return false;
    int colons = 0;
    size_t len = 0, colonAt = 0;
    for (const wchar_t *p = name; *p; ++p, ++len) {
        if (*p < 0x21 || *p > 0x7E || *p == L'=') return false;
        if (*p == L':') { ++colons; colonAt = len; }
    }
    return colons == 1 && colonAt != 0 && colonAt != len - 1;
}

// Prefer the id saved for this name; otherwise the lowest free id in range.
int AllocateId(const std::wstring &name, int lo, int hi)
{
    std::map<std::wstring, int>::const_iterator saved = g_savedIds.find(name);
    if (saved != g_savedIds.end()) {
        int id = saved->second;
        if (id >= lo && id <= hi && !Backend::IdInUse(id)) return id;
        Log(FLCE_LOG_WARN, L"ForgedLCE",
            L"Saved id for " + name + L" is unavailable; worlds that used it may show the wrong block or item");
    }
    for (int id = lo; id <= hi; ++id) {
        if (Backend::IdInUse(id)) continue;
        // Don't steal an id another (possibly absent) mod saved.
        bool reserved = false;
        for (std::map<std::wstring, int>::const_iterator it = g_savedIds.begin(); it != g_savedIds.end(); ++it) {
            if (it->second == id && it->first != name) { reserved = true; break; }
        }
        if (!reserved) return id;
    }
    return -1;
}

bool CheckPhase(LoadPhase required, const wchar_t *what)
{
    if (CurrentPhase() == required) return true;
    Log(FLCE_LOG_ERROR, L"ForgedLCE", std::wstring(what) + L" called outside its loading phase");
    return false;
}

bool CheckPhaseAtLeast(LoadPhase minimum, const wchar_t *what)
{
    LoadPhase p = CurrentPhase();
    if (p >= minimum && p != PHASE_DONE) return true;
    Log(FLCE_LOG_ERROR, L"ForgedLCE", std::wstring(what) + L" called outside its loading phase");
    return false;
}

// Registry names are ASCII (checked above), so the id file is plain ASCII.
std::string Narrow(const std::wstring &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) out += (char)(s[i] & 0x7F);
    return out;
}

std::wstring Widen(const std::string &s)
{
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) out += (wchar_t)(unsigned char)s[i];
    return out;
}

} // namespace

// ---- id map file ----------------------------------------------------

void LoadIdMap()
{
    g_savedIds.clear();
    std::wstring path = IdMapPath();
    std::string text;
    if (path.empty() || !Platform::ReadFile(path, text)) return; // none yet

    // Skip a UTF-8 byte order mark (older versions wrote one).
    size_t pos = text.size() >= 3 && (unsigned char)text[0] == 0xEF ? 3 : 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        int id = atoi(line.c_str() + eq + 1);
        if (id > 0) g_savedIds[Widen(line.substr(0, eq))] = id;
    }
}

void SaveIdMap()
{
    std::wstring path = IdMapPath();
    if (path.empty()) return;

    // Keep saved entries for mods that are not installed right now, so
    // re-adding a mod gets its old ids back.
    std::map<std::wstring, int> all = g_savedIds;
    for (std::map<std::wstring, int>::const_iterator it = g_assignedIds.begin(); it != g_assignedIds.end(); ++it) {
        all[it->first] = it->second;
    }
    std::string text =
        "# ForgedLCE id map. Keeps mod blocks/items on the same ids across runs.\n"
        "# Do not edit while worlds that use these mods exist.\n";
    for (std::map<std::wstring, int>::const_iterator it = all.begin(); it != all.end(); ++it) {
        text += Narrow(it->first) + "=" + Narrow(IntToString(it->second)) + "\n";
    }
    if (!Platform::WriteFile(path, text)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not write " + path);
    }
}

// ---- blocks and items -----------------------------------------------

int RegisterBlock(const FLCE_BlockDesc *desc)
{
    if (!CheckPhase(PHASE_PREINIT, L"registerBlock")) return -1;
    if (desc == NULL || desc->structSize < sizeof(FLCE_BlockDesc)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerBlock: bad descriptor (structSize)");
        return -1;
    }
    if (!IsValidRegistryName(desc->registryName)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerBlock: registry name must look like modid:name");
        return -1;
    }
    std::wstring name(desc->registryName);
    if (g_assignedIds.count(name)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerBlock: duplicate name " + name);
        return -1;
    }

    int id = AllocateId(name, BLOCK_ID_MIN, BLOCK_ID_MAX);
    if (id < 0) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerBlock: no free block ids left (limit is 255) for " + name);
        return -1;
    }

    std::wstring display = desc->displayName ? desc->displayName : name;
    std::wstring texture = desc->textureName ? desc->textureName : L"stone";
    std::wstring error;
    if (!Backend::CreateBlock(id, *desc, texture, AddModString(display), error)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerBlock: " + name + L": " + error);
        return -1;
    }

    g_assignedIds[name] = id;
    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Registered block " + name + L" as id " + IntToString(id));
    return id;
}

int RegisterItem(const FLCE_ItemDesc *desc)
{
    if (!CheckPhase(PHASE_PREINIT, L"registerItem")) return -1;
    if (desc == NULL || desc->structSize < sizeof(FLCE_ItemDesc)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerItem: bad descriptor (structSize)");
        return -1;
    }
    if (!IsValidRegistryName(desc->registryName)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerItem: registry name must look like modid:name");
        return -1;
    }
    std::wstring name(desc->registryName);
    if (g_assignedIds.count(name)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerItem: duplicate name " + name);
        return -1;
    }

    int id = AllocateId(name, ITEM_ID_MIN, ITEM_ID_MAX);
    if (id < 0) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerItem: no free item ids left for " + name);
        return -1;
    }

    std::wstring display = desc->displayName ? desc->displayName : name;
    std::wstring texture = desc->textureName ? desc->textureName : L"stick";
    std::wstring error;
    if (!Backend::CreateItem(id, *desc, texture, AddModString(display), error)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"registerItem: " + name + L": " + error);
        return -1;
    }

    g_assignedIds[name] = id;
    Log(FLCE_LOG_INFO, L"ForgedLCE", L"Registered item " + name + L" as id " + IntToString(id));
    return id;
}

int FindId(const wchar_t *registryName)
{
    if (registryName == NULL) return -1;
    std::map<std::wstring, int>::const_iterator it = g_assignedIds.find(registryName);
    return it == g_assignedIds.end() ? -1 : it->second;
}

// ---- recipes --------------------------------------------------------

int AddShapedRecipe(int resultId, int resultCount, int resultData,
                    const wchar_t *const *rows, int rowCount,
                    const FLCE_RecipeKey *keys, int keyCount)
{
    if (!CheckPhaseAtLeast(PHASE_INIT, L"addShapedRecipe")) return 0;
    if (!Backend::IdExists(resultId) || rows == NULL || rowCount < 1 || rowCount > 3 || (keyCount > 0 && keys == NULL)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapedRecipe: bad arguments");
        return 0;
    }
    int width = rows[0] ? (int)wcslen(rows[0]) : 0;
    if (width < 1 || width > 3) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapedRecipe: rows must be 1-3 characters");
        return 0;
    }
    for (int r = 1; r < rowCount; ++r) {
        if (rows[r] == NULL || (int)wcslen(rows[r]) != width) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapedRecipe: all rows must be the same length");
            return 0;
        }
    }
    for (int k = 0; k < keyCount; ++k) {
        if (!Backend::IdExists(keys[k].id)) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapedRecipe: key refers to an unknown id");
            return 0;
        }
    }

    int slotIds[9], slotData[9];
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < width; ++c) {
            wchar_t ch = rows[r][c];
            int slot = r * width + c;
            slotIds[slot] = 0;
            slotData[slot] = -1;
            if (ch == L' ') continue;
            int k = 0;
            while (k < keyCount && keys[k].symbol != ch) ++k;
            if (k == keyCount) {
                Log(FLCE_LOG_ERROR, L"ForgedLCE", std::wstring(L"addShapedRecipe: no key for symbol '") + ch + L"'");
                return 0;
            }
            slotIds[slot] = keys[k].id;
            slotData[slot] = keys[k].data < 0 ? -1 : keys[k].data;
        }
    }

    if (!Backend::AddShaped(width, rowCount, slotIds, slotData,
                            resultId, resultCount > 0 ? resultCount : 1, resultData)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapedRecipe: the game rejected the recipe");
        return 0;
    }
    return 1;
}

int AddShapelessRecipe(int resultId, int resultCount, int resultData, const int *ids, int count)
{
    if (!CheckPhaseAtLeast(PHASE_INIT, L"addShapelessRecipe")) return 0;
    if (!Backend::IdExists(resultId) || ids == NULL || count < 1 || count > 9) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapelessRecipe: bad arguments");
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        if (!Backend::IdExists(ids[i])) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapelessRecipe: ingredient refers to an unknown id");
            return 0;
        }
    }
    if (!Backend::AddShapeless(ids, count, resultId, resultCount > 0 ? resultCount : 1, resultData)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapelessRecipe: the game rejected the recipe");
        return 0;
    }
    return 1;
}

int AddSmelting(int inputId, int resultId, int resultCount, int resultData, float xp)
{
    if (!CheckPhaseAtLeast(PHASE_INIT, L"addSmelting")) return 0;
    if (!Backend::IdExists(inputId) || !Backend::IdExists(resultId)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addSmelting: unknown id");
        return 0;
    }
    if (!Backend::AddSmelting(inputId, resultId, resultCount > 0 ? resultCount : 1, resultData, xp)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addSmelting: the game rejected the recipe");
        return 0;
    }
    return 1;
}

void FinishRecipes()
{
    Backend::FinishRecipes();
}

} // namespace Registry
} // namespace ForgedLCE
