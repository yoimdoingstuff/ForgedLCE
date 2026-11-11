// ForgedLCE registries: block/item creation, the name->id map, recipes.
#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.level.material.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.crafting.h"
#include "net.minecraft.world.entity.player.h"
#include "Recipes.h"
#include "ShapedRecipy.h"
#include "ShapelessRecipy.h"
#include "FurnaceRecipes.h"

#include "FLCE_Internal.h"

#include <stdio.h>

// Id persistence:
//  - Windows64: mods\forgedlce_ids.txt keeps each name on the same id.
//  - Consoles: no file. Built-in mods register in a fixed order, so ids stay
//    the same as long as the mod list and each mod's registrations don't
//    change. (A per-world id map in the save is planned.)

namespace ForgedLCE {

// ---------------------------------------------------------------------
// Game classes driven by mod descriptors
// ---------------------------------------------------------------------

// Most Tile setters (setDestroyTime, setExplodeable, setSoundType,
// setLightEmission, init) are protected in LCE, so the subclass applies them.
class FLCE_ModTile : public Tile
{
public:
    FLCE_ModTile(int id, Material *material, bool solidRender, const Tile::SoundType *sound,
                 const FLCE_BlockDesc &desc, const wstring &texture, unsigned int nameId)
        : Tile(id, material, solidRender),
          m_dropId(desc.dropId),
          m_dropCount(desc.dropCount > 0 ? desc.dropCount : 1),
          m_onActivated(desc.onActivated),
          m_userData(desc.userData)
    {
        setDestroyTime(desc.hardness);
        setExplodeable(desc.resistance);
        setSoundType(sound);
        setLightEmission(desc.lightEmission);
        setTextureName(texture);
        setDescriptionId(nameId);
    }

    // Tile::init is protected; Tile::staticCtor calls it after creating
    // each block's TileItem, and so does RegisterBlock.
    void flceInit() { init(); }

    // Tile has two TestUse overloads and vanilla tiles override either one
    // (WorkbenchTile overrides the no-argument form), so override both.
    virtual bool TestUse()
    {
        return m_onActivated != NULL;
    }

    virtual int getResource(int data, Random *random, int playerBonusLevel)
    {
        return m_dropId > 0 ? m_dropId : id;
    }

    virtual int getResourceCount(Random *random)
    {
        return m_dropCount;
    }

    // Lets the console UI show a "Use" prompt.
    virtual bool TestUse(Level *level, int x, int y, int z, shared_ptr<Player> player)
    {
        return m_onActivated != NULL;
    }

    virtual bool use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace,
                     float clickX, float clickY, float clickZ, bool soundOnly = false)
    {
        if (m_onActivated == NULL || soundOnly) return false;
        return m_onActivated(reinterpret_cast<FLCE_Level *>(level), x, y, z,
                             reinterpret_cast<FLCE_Entity *>(static_cast<Entity *>(player.get())),
                             clickedFace, m_userData) != 0;
    }

private:
    int                   m_dropId;
    int                   m_dropCount;
    FLCE_BlockActivatedFn m_onActivated;
    void                 *m_userData;
};

class FLCE_ModItem : public Item
{
public:
    // `id` is the final numeric id; Item's constructor adds 256 itself.
    FLCE_ModItem(int id, const FLCE_ItemDesc &desc)
        : Item(id - 256), m_onUse(desc.onUse), m_userData(desc.userData)
    {
        if (desc.maxDamage > 0) setMaxDamage(desc.maxDamage); // protected in Item
    }

    virtual bool TestUse(Level *level, shared_ptr<Player> player)
    {
        return m_onUse != NULL;
    }

    virtual shared_ptr<ItemInstance> use(shared_ptr<ItemInstance> itemInstance, Level *level, shared_ptr<Player> player)
    {
        if (m_onUse != NULL) {
            m_onUse(reinterpret_cast<FLCE_Level *>(level),
                    reinterpret_cast<FLCE_Entity *>(static_cast<Entity *>(player.get())),
                    m_userData);
        }
        return itemInstance;
    }

private:
    FLCE_ItemUseFn m_onUse;
    void          *m_userData;
};

namespace Registry {

namespace {

std::map<std::wstring, int> g_savedIds;    // from forgedlce_ids.txt
std::map<std::wstring, int> g_assignedIds; // registered this run

std::wstring IdMapPath()
{
    std::wstring dir = ModsDir();
    return dir.empty() ? std::wstring() : dir + L"forgedlce_ids.txt";
}

bool IsValidRegistryName(const wchar_t *name)
{
    if (name == NULL || name[0] == 0) return false;
    const wchar_t *colon = wcschr(name, L':');
    // "modid:name": exactly one colon, not at either end, no '=' (file format).
    return colon != NULL && colon != name && colon[1] != 0 &&
           wcschr(colon + 1, L':') == NULL && wcschr(name, L'=') == NULL;
}

bool IdIsUsed(int id)
{
    if (id < 0 || id >= Item::ITEM_NUM_COUNT) return true;
    if (id < Tile::TILE_NUM_COUNT && Tile::tiles[id] != NULL) return true;
    return Item::items[id] != NULL;
}

// Prefer the id saved for this name; otherwise the lowest free id in range.
int AllocateId(const std::wstring &name, int lo, int hi)
{
    std::map<std::wstring, int>::const_iterator saved = g_savedIds.find(name);
    if (saved != g_savedIds.end()) {
        int id = saved->second;
        if (id >= lo && id <= hi && !IdIsUsed(id)) return id;
        Log(FLCE_LOG_WARN, L"ForgedLCE",
            L"Saved id for " + name + L" is unavailable; worlds that used it may show the wrong block or item");
    }
    for (int id = lo; id <= hi; ++id) {
        if (IdIsUsed(id)) continue;
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

Material *MapMaterial(int m)
{
    switch (m) {
    case FLCE_MATERIAL_WOOD:   return Material::wood;
    case FLCE_MATERIAL_DIRT:   return Material::dirt;
    case FLCE_MATERIAL_GRASS:  return Material::grass;
    case FLCE_MATERIAL_SAND:   return Material::sand;
    case FLCE_MATERIAL_METAL:  return Material::metal;
    case FLCE_MATERIAL_GLASS:  return Material::glass;
    case FLCE_MATERIAL_CLOTH:  return Material::cloth;
    case FLCE_MATERIAL_PLANT:  return Material::plant;
    case FLCE_MATERIAL_LEAVES: return Material::leaves;
    case FLCE_MATERIAL_ICE:    return Material::ice;
    case FLCE_MATERIAL_CLAY:   return Material::clay;
    default:                   return Material::stone;
    }
}

const Tile::SoundType *MapSound(int s)
{
    switch (s) {
    case FLCE_SOUND_WOOD:   return Tile::SOUND_WOOD;
    case FLCE_SOUND_GRAVEL: return Tile::SOUND_GRAVEL;
    case FLCE_SOUND_GRASS:  return Tile::SOUND_GRASS;
    case FLCE_SOUND_METAL:  return Tile::SOUND_METAL;
    case FLCE_SOUND_GLASS:  return Tile::SOUND_GLASS;
    case FLCE_SOUND_CLOTH:  return Tile::SOUND_CLOTH;
    case FLCE_SOUND_SAND:   return Tile::SOUND_SAND;
    case FLCE_SOUND_SNOW:   return Tile::SOUND_SNOW;
    default:                return Tile::SOUND_STONE;
    }
}

bool IdExists(int id)
{
    if (id <= 0 || id >= Item::ITEM_NUM_COUNT) return false;
    if (id < 256) return Tile::tiles[id] != NULL;
    return Item::items[id] != NULL;
}

ItemInstance *MakeInstance(int id, int count, int data)
{
    return new ItemInstance(id, count, data);
}

} // namespace

// ---- id map file ----------------------------------------------------

void LoadIdMap()
{
    g_savedIds.clear();
    std::wstring path = IdMapPath();
    if (path.empty()) return;
#ifdef _WINDOWS64
    FILE *f = _wfopen(path.c_str(), L"rt, ccs=UTF-8");
    if (f == NULL) return; // first run
    wchar_t line[512];
    while (fgetws(line, 512, f) != NULL) {
        std::wstring s(line);
        while (!s.empty() && (s[s.size() - 1] == L'\n' || s[s.size() - 1] == L'\r')) s.erase(s.size() - 1);
        if (s.empty() || s[0] == L'#') continue;
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        int id = _wtoi(s.c_str() + eq + 1);
        if (id > 0) g_savedIds[s.substr(0, eq)] = id;
    }
    fclose(f);
#endif
}

void SaveIdMap()
{
    std::wstring path = IdMapPath();
    if (path.empty()) return;
#ifdef _WINDOWS64
    // Keep saved entries for mods that are not installed right now, so
    // re-adding a mod gets its old ids back.
    std::map<std::wstring, int> all = g_savedIds;
    for (std::map<std::wstring, int>::const_iterator it = g_assignedIds.begin(); it != g_assignedIds.end(); ++it) {
        all[it->first] = it->second;
    }
    FILE *f = _wfopen(path.c_str(), L"wt, ccs=UTF-8");
    if (f == NULL) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not write " + path);
        return;
    }
    fputws(L"# ForgedLCE id map. Keeps mod blocks/items on the same ids across runs.\n", f);
    fputws(L"# Do not edit while worlds that use these mods exist.\n", f);
    for (std::map<std::wstring, int>::const_iterator it = all.begin(); it != all.end(); ++it) {
        fwprintf(f, L"%ls=%d\n", it->first.c_str(), it->second);
    }
    fclose(f);
#endif
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

    bool solidRender = desc->material != FLCE_MATERIAL_GLASS &&
                       desc->material != FLCE_MATERIAL_LEAVES &&
                       desc->material != FLCE_MATERIAL_PLANT;

    std::wstring display = desc->displayName ? desc->displayName : name;
    std::wstring texture = desc->textureName ? desc->textureName : L"stone";

    // The Tile constructor stores the block in Tile::tiles[id].
    FLCE_ModTile *tile = new FLCE_ModTile(id, MapMaterial(desc->material), solidRender,
                                          MapSound(desc->sound), *desc, texture,
                                          AddModString(display));

    // Tile::staticCtor's TileItem loop has already run, so do its job here.
    if (Item::items[id] == NULL) {
        new TileItem(id - 256); // the Item constructor stores it in Item::items[id]
        tile->flceInit();
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

    Item *item = new FLCE_ModItem(id, *desc);
    item->setTextureName(texture);
    item->setDescriptionId(AddModString(display));
    if (desc->maxStackSize > 0) item->setMaxStackSize(desc->maxStackSize);
    if (desc->handEquipped)     item->handEquipped();

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
    if (!IdExists(resultId) || rows == NULL || rowCount < 1 || rowCount > 3 || (keyCount > 0 && keys == NULL)) {
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
        if (!IdExists(keys[k].id)) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapedRecipe: key refers to an unknown id");
            return 0;
        }
    }

    ItemInstance **slots = new ItemInstance *[width * rowCount];
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < width; ++c) {
            wchar_t ch = rows[r][c];
            ItemInstance *slot = NULL;
            if (ch != L' ') {
                for (int k = 0; k < keyCount; ++k) {
                    if (keys[k].symbol == ch) {
                        // Not `cond ? Recipes::ANY_AUX_VALUE : ...`: that binds a
                        // reference to the static const, which needs an
                        // out-of-class definition on stricter compilers.
                        int data = keys[k].data;
                        if (data < 0) data = Recipes::ANY_AUX_VALUE;
                        slot = MakeInstance(keys[k].id, 1, data);
                        break;
                    }
                }
                if (slot == NULL) {
                    Log(FLCE_LOG_ERROR, L"ForgedLCE", std::wstring(L"addShapedRecipe: no key for symbol '") + ch + L"'");
                    for (int i = 0; i < r * width + c; ++i) delete slots[i];
                    delete[] slots;
                    return 0;
                }
            }
            slots[r * width + c] = slot;
        }
    }

    ItemInstance *result = MakeInstance(resultId, resultCount > 0 ? resultCount : 1, resultData);
    Recipes::getInstance()->getRecipies()->push_back(new ShapedRecipy(width, rowCount, slots, result));
    return 1;
}

int AddShapelessRecipe(int resultId, int resultCount, int resultData, const int *ids, int count)
{
    if (!CheckPhaseAtLeast(PHASE_INIT, L"addShapelessRecipe")) return 0;
    if (!IdExists(resultId) || ids == NULL || count < 1 || count > 9) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapelessRecipe: bad arguments");
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        if (!IdExists(ids[i])) {
            Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addShapelessRecipe: ingredient refers to an unknown id");
            return 0;
        }
    }
    vector<ItemInstance *> *ingredients = new vector<ItemInstance *>();
    for (int i = 0; i < count; ++i) {
        ingredients->push_back(MakeInstance(ids[i], 1, Recipes::ANY_AUX_VALUE));
    }
    ItemInstance *result = MakeInstance(resultId, resultCount > 0 ? resultCount : 1, resultData);
    Recipes::getInstance()->getRecipies()->push_back(new ShapelessRecipy(result, ingredients));
    return 1;
}

int AddSmelting(int inputId, int resultId, int resultCount, int resultData, float xp)
{
    if (!CheckPhaseAtLeast(PHASE_INIT, L"addSmelting")) return 0;
    if (!IdExists(inputId) || !IdExists(resultId)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"addSmelting: unknown id");
        return 0;
    }
    FurnaceRecipes::getInstance()->addFurnaceRecipy(
        inputId, MakeInstance(resultId, resultCount > 0 ? resultCount : 1, resultData), xp);
    return 1;
}

void FinishRecipes()
{
    // Added by patches/0003: rebuilds the console crafting menu's cache.
    Recipes::getInstance()->flceRebuildIngredientsArray();
}

} // namespace Registry
} // namespace ForgedLCE
