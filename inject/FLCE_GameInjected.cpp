// ForgedLCE game backend for INJECTED mode: a plugin loaded into the
// already-compiled game (Dashlaunch on Xbox 360, taiHEN on PS Vita). Every
// game call goes through addresses from the address map (g_game).
//
// Differences from source builds, all logged when they matter:
//  - block/item callbacks (onActivated, onUse) and custom block drops need
//    overriding game virtual functions, which this mode doesn't do yet;
//    such blocks still register, but drop themselves and ignore the callbacks
//  - EntityJoinWorld events report isPlayer = -1 (unknown)
#include "stdafx.h"
#include "FLCE_Internal.h"
#include "FLCE_InjectGame.h"

#include <string.h>

namespace ForgedLCE {
namespace Inject {

GameBinding g_game;

namespace {

template <typename Fn>
void Bind(Fn &out, Addr address)
{
    out = reinterpret_cast<Fn>(static_cast<size_t>(address));
}

const char *const MATERIAL_KEYS[12] = {
    "data.Material_stone", "data.Material_wood", "data.Material_dirt", "data.Material_grass",
    "data.Material_sand", "data.Material_metal", "data.Material_glass", "data.Material_cloth",
    "data.Material_plant", "data.Material_leaves", "data.Material_ice", "data.Material_clay"
};
const char *const SOUND_KEYS[9] = {
    "data.Tile_SOUND_stone", "data.Tile_SOUND_wood", "data.Tile_SOUND_gravel", "data.Tile_SOUND_grass",
    "data.Tile_SOUND_metal", "data.Tile_SOUND_glass", "data.Tile_SOUND_cloth", "data.Tile_SOUND_sand",
    "data.Tile_SOUND_snow"
};

std::wstring Widen(const std::string &s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) out += (wchar_t)(unsigned char)s[i];
    return out;
}

// ---- reading game memory ----------------------------------------------

void *ReadPtr(Addr address) { return *reinterpret_cast<void *const *>(static_cast<size_t>(address)); }

void *TileAt(int id)
{
    void **tiles = static_cast<void **>(ReadPtr(g_game.tiles));
    return tiles != NULL ? tiles[id] : NULL;
}

void *ItemAt(int id)
{
    void **items = static_cast<void **>(ReadPtr(g_game.items)); // ItemArray: data pointer first
    return items != NULL ? items[id] : NULL;
}

void *Recipes() { return ReadPtr(g_game.recipesInstance); }

void *RecipeVector()
{
    char *recipes = static_cast<char *>(Recipes());
    return *reinterpret_cast<void **>(recipes + g_game.offRecipesRecipies);
}

void *NewItemInstance(int id, int count, int data)
{
    void *mem = g_game.operatorNew(g_game.sizeItemInstance);
    g_game.ItemInstance_ctor(mem, id, count, data < 0 ? -1 : data); // -1 = Recipes::ANY_AUX_VALUE
    return mem;
}

// A std::wstring built and destroyed by the game's own code, so its layout,
// allocator and character width always match the game's.
class GameWString
{
public:
    explicit GameWString(const std::wstring &text)
    {
        std::vector<unsigned char> chars;
        const void *gameText = ToGameText(text, g_game.gameWcharSize, chars);
        g_game.wstring_ctor(m_storage.bytes, static_cast<const wchar_t *>(gameText));
    }
    ~GameWString() { g_game.wstring_dtor(m_storage.bytes); }
    const void *get() const { return m_storage.bytes; }
private:
    union { double align; void *alignPtr; char bytes[64]; } m_storage;
    GameWString(const GameWString &);
    GameWString &operator=(const GameWString &);
};

const int GROUP_DECORATION = 6; // Recipy::eGroupType_Decoration

// Mod names converted to the game's width, indexed by id - STRING_ID_BASE.
// Built once after loading, read-only afterwards (safe from game threads).
std::vector< std::vector<unsigned char> > g_gameStrings;
bool g_convertStrings = false;

} // namespace

const void *ToGameText(const std::wstring &text, size_t wcharSize, std::vector<unsigned char> &out)
{
    out.clear();
    if (wcharSize == sizeof(wchar_t)) {
        const unsigned char *p = reinterpret_cast<const unsigned char *>(text.c_str());
        out.assign(p, p + (text.size() + 1) * sizeof(wchar_t));
        return out.empty() ? NULL : &out[0];
    }
    // Decode to code points, then encode at the game's width.
    std::vector<unsigned int> cps;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned int cp = (unsigned int)text[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned int)text[++i] - 0xDC00);
        }
        cps.push_back(cp);
    }
    for (size_t i = 0; i < cps.size(); ++i) {
        unsigned int cp = cps[i];
        if (wcharSize == 2) {
            unsigned short units[2];
            int n = 1;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                units[0] = (unsigned short)(0xD800 + (cp >> 10));
                units[1] = (unsigned short)(0xDC00 + (cp & 0x3FF));
                n = 2;
            } else {
                units[0] = (unsigned short)cp;
            }
            const unsigned char *b = reinterpret_cast<const unsigned char *>(units);
            out.insert(out.end(), b, b + n * 2);
        } else {
            const unsigned char *b = reinterpret_cast<const unsigned char *>(&cp);
            out.insert(out.end(), b, b + 4);
        }
    }
    out.insert(out.end(), wcharSize, 0); // terminator
    return &out[0];
}

void PrepareGameStrings()
{
    g_convertStrings = g_game.gameWcharSize != sizeof(wchar_t);
    if (!g_convertStrings) return;
    size_t count = ModStringCount();
    g_gameStrings.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const wchar_t *text = GetModString(STRING_ID_BASE + (int)i);
        ToGameText(text != NULL ? text : L"", g_game.gameWcharSize, g_gameStrings[i]);
    }
}

const void *GameModString(int id)
{
    if (!g_convertStrings) return GetModString(id);
    if (id < STRING_ID_BASE) return NULL;
    size_t index = (size_t)(id - STRING_ID_BASE);
    return index < g_gameStrings.size() ? &g_gameStrings[index][0] : NULL;
}

bool BindGame(const AddressMap &map, ResolveFn resolve, std::wstring &error)
{
    std::string missing = map.missingRequired();
    if (!missing.empty()) {
        error = L"address map is missing: " + Widen(missing);
        return false;
    }
    if (map.get("size.wstring") > 64 || map.get("size.wstring") == 0) {
        error = L"size.wstring must be between 1 and 64";
        return false;
    }

    // Every required address must resolve to something (a segment value on a
    // platform without segments resolves to 0).
    for (const KeyInfo *k = KEYS; k->name != NULL; ++k) {
        bool isAddress = k->kind == KIND_HOOK || k->kind == KIND_FUNC || k->kind == KIND_DATA ||
                         std::string(k->name) == "check.address";
        if (k->required && isAddress && resolve(map.segment(k->name), map.get(k->name)) == 0) {
            error = Widen(k->name) + L" is 0 or can't be resolved on this platform";
            return false;
        }
    }

    memset(&g_game, 0, sizeof(g_game));
#define ADDR(key) (map.has(key) ? resolve(map.segment(key), map.get(key)) : (Addr)0)
#define NUM(key)  ((size_t)map.get(key))
    Bind(g_game.operatorNew, ADDR("fn.operator_new"));
    Bind(g_game.Tile_ctor, ADDR("fn.Tile_ctor"));
    Bind(g_game.Tile_setDestroyTime, ADDR("fn.Tile_setDestroyTime"));
    Bind(g_game.Tile_setExplodeable, ADDR("fn.Tile_setExplodeable"));
    Bind(g_game.Tile_setSoundType, ADDR("fn.Tile_setSoundType"));
    Bind(g_game.Tile_setLightEmission, ADDR("fn.Tile_setLightEmission"));
    Bind(g_game.Tile_setTextureName, ADDR("fn.Tile_setTextureName"));
    Bind(g_game.Tile_setDescriptionId, ADDR("fn.Tile_setDescriptionId"));
    Bind(g_game.Tile_init, ADDR("fn.Tile_init"));
    Bind(g_game.TileItem_ctor, ADDR("fn.TileItem_ctor"));
    Bind(g_game.Item_ctor, ADDR("fn.Item_ctor"));
    Bind(g_game.Item_setTextureName, ADDR("fn.Item_setTextureName"));
    Bind(g_game.Item_setMaxStackSize, ADDR("fn.Item_setMaxStackSize"));
    Bind(g_game.Item_setMaxDamage, ADDR("fn.Item_setMaxDamage"));
    Bind(g_game.Item_handEquipped, ADDR("fn.Item_handEquipped"));
    Bind(g_game.Item_setDescriptionId, ADDR("fn.Item_setDescriptionId"));
    Bind(g_game.ItemInstance_ctor, ADDR("fn.ItemInstance_ctor"));
    Bind(g_game.ShapedRecipy_ctor, ADDR("fn.ShapedRecipy_ctor"));
    Bind(g_game.ShapelessRecipy_ctor, ADDR("fn.ShapelessRecipy_ctor"));
    Bind(g_game.RecipyPtrVector_push_back, ADDR("fn.RecipyPtrVector_push_back"));
    Bind(g_game.ItemInstancePtrVector_push_back, ADDR("fn.ItemInstancePtrVector_push_back"));
    Bind(g_game.Recipes_buildRecipeIngredientsArray, ADDR("fn.Recipes_buildRecipeIngredientsArray"));
    Bind(g_game.FurnaceRecipes_addFurnaceRecipy, ADDR("fn.FurnaceRecipes_addFurnaceRecipy"));
    Bind(g_game.wstring_ctor, ADDR("fn.wstring_ctor"));
    Bind(g_game.wstring_dtor, ADDR("fn.wstring_dtor"));
    Bind(g_game.Level_getTile, ADDR("fn.Level_getTile"));
    Bind(g_game.Level_getData, ADDR("fn.Level_getData"));
    Bind(g_game.Level_setTileAndData, ADDR("fn.Level_setTileAndData"));
    Bind(g_game.EntitySharedPtr_dtor, ADDR("fn.EntitySharedPtr_dtor"));

    g_game.tiles           = ADDR("data.Tile_tiles");
    g_game.items           = ADDR("data.Item_items");
    g_game.recipesInstance = ADDR("data.Recipes_instance");
    g_game.furnaceInstance = ADDR("data.FurnaceRecipes_instance");
    g_game.lateCheck       = ADDR("data.LateCheck");
    for (int i = 0; i < 12; ++i) g_game.materials[i] = ADDR(MATERIAL_KEYS[i]);
    for (int i = 0; i < 9; ++i)  g_game.sounds[i] = ADDR(SOUND_KEYS[i]);

    g_game.sizeTile            = NUM("size.Tile");
    g_game.sizeTileItem        = NUM("size.TileItem");
    g_game.sizeItem            = NUM("size.Item");
    g_game.sizeItemInstance    = NUM("size.ItemInstance");
    g_game.sizeShapedRecipy    = NUM("size.ShapedRecipy");
    g_game.sizeShapelessRecipy = NUM("size.ShapelessRecipy");
    g_game.sizeVector          = NUM("size.vector");
    g_game.sizeWstring         = NUM("size.wstring");
    g_game.offRecipesRecipies   = NUM("offset.Recipes_recipies");
    g_game.offLevelIsClientSide = NUM("offset.Level_isClientSide");
    g_game.offEntityX           = NUM("offset.Entity_x");
    g_game.offGameModeLevel     = NUM("offset.ServerPlayerGameMode_level");
    g_game.offGameModePlayer    = NUM("offset.ServerPlayerGameMode_player");
    g_game.offSharedPtrRaw      = NUM("offset.SharedPtr_raw");
    g_game.offPlayerEntity      = NUM("offset.Player_entity");
    g_game.hasGameModeOffsets   = map.has("offset.ServerPlayerGameMode_level") &&
                                  map.has("offset.ServerPlayerGameMode_player");
    g_game.sharedPtrArgByPointer = map.get("abi.sharedPtrArgByPointer") != 0;
    g_game.calleeDestroysArgs    = map.get("abi.calleeDestroysArgs") != 0;
    g_game.gameWcharSize         = map.has("abi.wcharSize") ? NUM("abi.wcharSize") : sizeof(wchar_t);
    if (g_game.gameWcharSize != 2 && g_game.gameWcharSize != 4) {
        error = L"abi.wcharSize must be 2 or 4";
        return false;
    }

    g_game.hookStartup      = ADDR("hook.MobCategory_staticCtor");
    g_game.hookGetString    = ADDR("hook.GetString");
    g_game.hookClientTick   = ADDR("hook.Minecraft_tick");
    g_game.hookServerTick   = ADDR("hook.MinecraftServer_tick");
    g_game.hookDestroyBlock = ADDR("hook.ServerPlayerGameMode_destroyBlock");
    g_game.hookAddEntity    = ADDR("hook.Level_addEntity");

    g_game.checkAddress = ADDR("check.address");
    g_game.checkValue   = (unsigned int)map.get("check.value");
#undef ADDR
#undef NUM
    return true;
}

bool VerifyGameVersion(std::wstring &error)
{
    if (g_game.checkAddress == 0) {
        error = L"check.address is 0";
        return false;
    }
    unsigned int actual = *reinterpret_cast<const unsigned int *>(static_cast<size_t>(g_game.checkAddress));
    if (actual != g_game.checkValue) {
        error = L"this address map is for a different game version (check.value doesn't match the game)";
        return false;
    }
    return true;
}

bool StartupAlreadyPassed()
{
    return g_game.lateCheck != 0 && ReadPtr(g_game.lateCheck) != NULL;
}

} // namespace Inject

// ---------------------------------------------------------------------
// Backend (FLCE_Backend.h)
// ---------------------------------------------------------------------

namespace Backend {

using Inject::g_game;

const wchar_t *Name() { return L"injected"; }

bool IdInUse(int id)
{
    if (id < 0 || id >= 32000) return true;
    if (id < 4096 && Inject::TileAt(id) != NULL) return true;
    return Inject::ItemAt(id) != NULL;
}

bool IdExists(int id)
{
    if (id <= 0 || id >= 32000) return false;
    if (id < 256) return Inject::TileAt(id) != NULL;
    return Inject::ItemAt(id) != NULL;
}

bool CreateBlock(int id, const FLCE_BlockDesc &desc, const std::wstring &texture,
                 unsigned int nameId, std::wstring &)
{
    if (desc.onActivated != NULL) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", std::wstring(desc.registryName) +
            L": block callbacks (onActivated) aren't supported by the injected plugin yet; ignored");
    }
    if (desc.dropId > 0 || desc.dropCount > 1) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", std::wstring(desc.registryName) +
            L": custom drops aren't supported by the injected plugin yet; the block drops itself");
    }
    int material = desc.material >= 0 && desc.material < 12 ? desc.material : FLCE_MATERIAL_STONE;
    int sound = desc.sound >= 0 && desc.sound < 9 ? desc.sound : FLCE_SOUND_STONE;
    bool solidRender = desc.material != FLCE_MATERIAL_GLASS &&
                       desc.material != FLCE_MATERIAL_LEAVES &&
                       desc.material != FLCE_MATERIAL_PLANT;

    // The Tile constructor stores the block in Tile::tiles[id].
    void *tile = g_game.operatorNew(g_game.sizeTile);
    g_game.Tile_ctor(tile, id, Inject::ReadPtr(g_game.materials[material]), solidRender);
    g_game.Tile_setDestroyTime(tile, desc.hardness);
    g_game.Tile_setExplodeable(tile, desc.resistance);
    g_game.Tile_setSoundType(tile, Inject::ReadPtr(g_game.sounds[sound]));
    g_game.Tile_setLightEmission(tile, desc.lightEmission);
    {
        Inject::GameWString name(texture);
        g_game.Tile_setTextureName(tile, name.get());
    }
    g_game.Tile_setDescriptionId(tile, nameId);

    // Tile::staticCtor's TileItem loop has already run, so do its job here.
    if (Inject::ItemAt(id) == NULL) {
        void *item = g_game.operatorNew(g_game.sizeTileItem);
        g_game.TileItem_ctor(item, id - 256); // stores itself in Item::items[id]
        g_game.Tile_init(tile);
    }
    return true;
}

bool CreateItem(int id, const FLCE_ItemDesc &desc, const std::wstring &texture,
                unsigned int nameId, std::wstring &)
{
    if (desc.onUse != NULL) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", std::wstring(desc.registryName) +
            L": item callbacks (onUse) aren't supported by the injected plugin yet; ignored");
    }
    void *item = g_game.operatorNew(g_game.sizeItem);
    g_game.Item_ctor(item, id - 256); // stores itself in Item::items[id]
    {
        Inject::GameWString name(texture);
        g_game.Item_setTextureName(item, name.get());
    }
    g_game.Item_setDescriptionId(item, nameId);
    if (desc.maxStackSize > 0) g_game.Item_setMaxStackSize(item, desc.maxStackSize);
    if (desc.maxDamage > 0)    g_game.Item_setMaxDamage(item, desc.maxDamage);
    if (desc.handEquipped)     g_game.Item_handEquipped(item);
    return true;
}

bool AddShaped(int width, int height, const int *slotIds, const int *slotData,
               int resultId, int resultCount, int resultData)
{
    int n = width * height;
    void **slots = static_cast<void **>(g_game.operatorNew(sizeof(void *) * (size_t)n));
    for (int i = 0; i < n; ++i) {
        slots[i] = slotIds[i] != 0 ? Inject::NewItemInstance(slotIds[i], 1, slotData[i]) : NULL;
    }
    void *recipe = g_game.operatorNew(g_game.sizeShapedRecipy);
    g_game.ShapedRecipy_ctor(recipe, width, height, slots,
                             Inject::NewItemInstance(resultId, resultCount, resultData), Inject::GROUP_DECORATION);
    g_game.RecipyPtrVector_push_back(Inject::RecipeVector(), &recipe);
    return true;
}

bool AddShapeless(const int *ids, int count, int resultId, int resultCount, int resultData)
{
    void *ingredients = g_game.operatorNew(g_game.sizeVector);
    memset(ingredients, 0, g_game.sizeVector); // an empty vector
    for (int i = 0; i < count; ++i) {
        void *inst = Inject::NewItemInstance(ids[i], 1, -1);
        g_game.ItemInstancePtrVector_push_back(ingredients, &inst);
    }
    void *recipe = g_game.operatorNew(g_game.sizeShapelessRecipy);
    g_game.ShapelessRecipy_ctor(recipe, Inject::NewItemInstance(resultId, resultCount, resultData), ingredients,
                                Inject::GROUP_DECORATION);
    g_game.RecipyPtrVector_push_back(Inject::RecipeVector(), &recipe);
    return true;
}

bool AddSmelting(int inputId, int resultId, int resultCount, int resultData, float xp)
{
    g_game.FurnaceRecipes_addFurnaceRecipy(Inject::ReadPtr(g_game.furnaceInstance), inputId,
                                           Inject::NewItemInstance(resultId, resultCount, resultData), xp);
    return true;
}

void FinishRecipes()
{
    // The retail game has no flceRebuildIngredientsArray (patch 0003), so call
    // the builder directly. The previous cache is leaked; it's small and
    // this runs once.
    g_game.Recipes_buildRecipeIngredientsArray(Inject::Recipes());
}

int GetTile(FLCE_Level *level, int x, int y, int z)
{
    return g_game.Level_getTile(level, x, y, z);
}

int GetData(FLCE_Level *level, int x, int y, int z)
{
    return g_game.Level_getData(level, x, y, z);
}

bool SetTile(FLCE_Level *level, int x, int y, int z, int id, int data)
{
    return g_game.Level_setTileAndData(level, x, y, z, id, data);
}

bool IsClientSide(FLCE_Level *level)
{
    return *(reinterpret_cast<const bool *>(level) + g_game.offLevelIsClientSide);
}

void GetEntityPos(FLCE_Entity *entity, double *x, double *y, double *z)
{
    const double *pos = reinterpret_cast<const double *>(reinterpret_cast<const char *>(entity) + g_game.offEntityX);
    *x = pos[0];
    *y = pos[1];
    *z = pos[2];
}

} // namespace Backend
} // namespace ForgedLCE
