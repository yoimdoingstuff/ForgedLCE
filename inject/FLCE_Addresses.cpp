// ForgedLCE injected mode: the address map's keys and its text parser.
#include "stdafx.h"
#include "FLCE_Addresses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ForgedLCE {
namespace Inject {

#define REQ true
#define OPT false

const KeyInfo KEYS[] = {
    // ---- version check: refuse to hook a different game version ----------
    { "check.address", KIND_CHECK, REQ, "Address of any instruction word in the game's code" },
    { "check.value",   KIND_CHECK, REQ, "The 32-bit word expected at check.address, as the CPU reads it" },

    // ---- hooks --------------------------------------------------------------
    { "hook.MobCategory_staticCtor", KIND_HOOK, REQ,
      "MobCategory::staticCtor() (MobCategory.cpp). Mods load right after it returns, the same point as patch 0002" },
    { "hook.GetString", KIND_HOOK, OPT,
      "StringTable::getString(int) (StringTable.cpp); on Xbox 360 CConsoleMinecraftApp::GetString(int) (Xbox/Xbox_App.cpp). Needed for mod block/item names" },
    { "hook.Minecraft_tick", KIND_HOOK, OPT, "Minecraft::tick(bool, bool) (Minecraft.cpp). Client tick event" },
    { "hook.MinecraftServer_tick", KIND_HOOK, OPT, "MinecraftServer::tick() (MinecraftServer.cpp). Server tick event" },
    { "hook.ServerPlayerGameMode_destroyBlock", KIND_HOOK, OPT,
      "ServerPlayerGameMode::destroyBlock(int, int, int) (ServerPlayerGameMode.cpp). Block break event" },
    { "hook.Level_addEntity", KIND_HOOK, OPT,
      "Level::addEntity(shared_ptr<Entity>) (Level.cpp). Entity join event; also needs abi.sharedPtrArgByPointer = 1" },

    // ---- functions ForgedLCE calls --------------------------------------------
    { "fn.operator_new", KIND_FUNC, REQ, "The game's operator new(size_t)" },
    { "fn.Tile_ctor", KIND_FUNC, REQ, "Tile::Tile(int id, Material *material, bool isSolidRender)" },
    { "fn.Tile_setDestroyTime", KIND_FUNC, REQ, "Tile::setDestroyTime(float)" },
    { "fn.Tile_setExplodeable", KIND_FUNC, REQ, "Tile::setExplodeable(float)" },
    { "fn.Tile_setSoundType", KIND_FUNC, REQ, "Tile::setSoundType(const SoundType *)" },
    { "fn.Tile_setLightEmission", KIND_FUNC, REQ, "Tile::setLightEmission(float)" },
    { "fn.Tile_setTextureName", KIND_FUNC, REQ, "Tile::setTextureName(const wstring &)" },
    { "fn.Tile_setDescriptionId", KIND_FUNC, REQ, "Tile::setDescriptionId(unsigned int)" },
    { "fn.Tile_init", KIND_FUNC, REQ, "Tile::init() (base class version)" },
    { "fn.TileItem_ctor", KIND_FUNC, REQ, "TileItem::TileItem(int id)" },
    { "fn.Item_ctor", KIND_FUNC, REQ, "Item::Item(int id)" },
    { "fn.Item_setTextureName", KIND_FUNC, REQ, "Item::setTextureName(const wstring &)" },
    { "fn.Item_setMaxStackSize", KIND_FUNC, REQ, "Item::setMaxStackSize(int)" },
    { "fn.Item_setMaxDamage", KIND_FUNC, REQ, "Item::setMaxDamage(int)" },
    { "fn.Item_handEquipped", KIND_FUNC, REQ, "Item::handEquipped()" },
    { "fn.Item_setDescriptionId", KIND_FUNC, REQ, "Item::setDescriptionId(unsigned int)" },
    { "fn.ItemInstance_ctor", KIND_FUNC, REQ, "ItemInstance::ItemInstance(int id, int count, int damage)" },
    { "fn.ShapedRecipy_ctor", KIND_FUNC, REQ, "ShapedRecipy::ShapedRecipy(int, int, ItemInstance **, ItemInstance *, int)" },
    { "fn.ShapelessRecipy_ctor", KIND_FUNC, REQ, "ShapelessRecipy::ShapelessRecipy(ItemInstance *, vector<ItemInstance *> *, _eGroupType)" },
    { "fn.RecipyPtrVector_push_back", KIND_FUNC, REQ, "vector<Recipy *>::push_back(Recipy *const &)" },
    { "fn.ItemInstancePtrVector_push_back", KIND_FUNC, REQ, "vector<ItemInstance *>::push_back(ItemInstance *const &)" },
    { "fn.Recipes_buildRecipeIngredientsArray", KIND_FUNC, REQ, "Recipes::buildRecipeIngredientsArray() (rebuilds the crafting menu cache)" },
    { "fn.FurnaceRecipes_addFurnaceRecipy", KIND_FUNC, REQ, "FurnaceRecipes::addFurnaceRecipy(int, ItemInstance *, float)" },
    { "fn.wstring_ctor", KIND_FUNC, REQ, "std::wstring::wstring(const wchar_t *), as compiled into the game" },
    { "fn.wstring_dtor", KIND_FUNC, REQ, "std::wstring::~wstring(), as compiled into the game" },
    { "fn.Level_getTile", KIND_FUNC, REQ, "Level::getTile(int, int, int)" },
    { "fn.Level_getData", KIND_FUNC, REQ, "Level::getData(int, int, int)" },
    { "fn.Level_setTileAndData", KIND_FUNC, REQ, "Level::setTileAndData(int, int, int, int, int)" },
    { "fn.EntitySharedPtr_dtor", KIND_FUNC, OPT,
      "shared_ptr<Entity>::~shared_ptr(). Only with abi.calleeDestroysArgs = 1, to release a canceled entity" },

    // ---- static variables ----------------------------------------------------
    { "data.Tile_tiles", KIND_DATA, REQ, "Tile::tiles (the Tile ** variable itself)" },
    { "data.Item_items", KIND_DATA, REQ, "Item::items (the ItemArray variable; its first word is the data pointer)" },
    { "data.Recipes_instance", KIND_DATA, REQ, "Recipes::instance" },
    { "data.FurnaceRecipes_instance", KIND_DATA, REQ, "FurnaceRecipes::instance" },
    { "data.Material_stone", KIND_DATA, REQ, "Material::stone" },
    { "data.Material_wood", KIND_DATA, REQ, "Material::wood" },
    { "data.Material_dirt", KIND_DATA, REQ, "Material::dirt" },
    { "data.Material_grass", KIND_DATA, REQ, "Material::grass" },
    { "data.Material_sand", KIND_DATA, REQ, "Material::sand" },
    { "data.Material_metal", KIND_DATA, REQ, "Material::metal" },
    { "data.Material_glass", KIND_DATA, REQ, "Material::glass" },
    { "data.Material_cloth", KIND_DATA, REQ, "Material::cloth" },
    { "data.Material_plant", KIND_DATA, REQ, "Material::plant" },
    { "data.Material_leaves", KIND_DATA, REQ, "Material::leaves" },
    { "data.Material_ice", KIND_DATA, REQ, "Material::ice" },
    { "data.Material_clay", KIND_DATA, REQ, "Material::clay" },
    { "data.Tile_SOUND_stone", KIND_DATA, REQ, "Tile::SOUND_STONE" },
    { "data.Tile_SOUND_wood", KIND_DATA, REQ, "Tile::SOUND_WOOD" },
    { "data.Tile_SOUND_gravel", KIND_DATA, REQ, "Tile::SOUND_GRAVEL" },
    { "data.Tile_SOUND_grass", KIND_DATA, REQ, "Tile::SOUND_GRASS" },
    { "data.Tile_SOUND_metal", KIND_DATA, REQ, "Tile::SOUND_METAL" },
    { "data.Tile_SOUND_glass", KIND_DATA, REQ, "Tile::SOUND_GLASS" },
    { "data.Tile_SOUND_cloth", KIND_DATA, REQ, "Tile::SOUND_CLOTH" },
    { "data.Tile_SOUND_sand", KIND_DATA, REQ, "Tile::SOUND_SAND" },
    { "data.Tile_SOUND_snow", KIND_DATA, REQ, "Tile::SOUND_SNOW" },
    { "data.LateCheck", KIND_DATA, OPT,
      "MobCategory::monster: non-NULL once startup has passed the hook, so a plugin loaded too late can say so" },

    // ---- sizes and offsets ------------------------------------------------------
    { "size.Tile", KIND_SIZE, REQ, "sizeof(Tile)" },
    { "size.TileItem", KIND_SIZE, REQ, "sizeof(TileItem)" },
    { "size.Item", KIND_SIZE, REQ, "sizeof(Item)" },
    { "size.ItemInstance", KIND_SIZE, REQ, "sizeof(ItemInstance)" },
    { "size.ShapedRecipy", KIND_SIZE, REQ, "sizeof(ShapedRecipy)" },
    { "size.ShapelessRecipy", KIND_SIZE, REQ, "sizeof(ShapelessRecipy)" },
    { "size.vector", KIND_SIZE, REQ, "sizeof(vector<ItemInstance *>) (an all-zero vector must be a valid empty one)" },
    { "size.wstring", KIND_SIZE, REQ, "sizeof(std::wstring), at most 64" },
    { "offset.Recipes_recipies", KIND_OFFSET, REQ, "Recipes::recipies (the vector<Recipy *> * member)" },
    { "offset.Level_isClientSide", KIND_OFFSET, REQ, "Level::isClientSide (bool)" },
    { "offset.Entity_x", KIND_OFFSET, REQ, "Entity::x (y and z follow as doubles)" },
    { "offset.ServerPlayerGameMode_level", KIND_OFFSET, OPT, "ServerPlayerGameMode::level. Needed by the destroyBlock hook" },
    { "offset.ServerPlayerGameMode_player", KIND_OFFSET, OPT, "ServerPlayerGameMode::player (a shared_ptr). Needed by the destroyBlock hook" },
    { "offset.SharedPtr_raw", KIND_OFFSET, OPT, "Offset of the raw pointer inside a shared_ptr (default 0)" },
    { "offset.Player_entity", KIND_OFFSET, OPT, "Offset of the Entity base inside Player (default 0)" },

    // ---- calling convention details ------------------------------------------------
    { "abi.sharedPtrArgByPointer", KIND_FLAG, OPT,
      "1 if a shared_ptr passed by value arrives as a pointer to it (default 0: Level_addEntity hook disabled)" },
    { "abi.calleeDestroysArgs", KIND_FLAG, OPT,
      "1 if the called function destroys by-value arguments (MSVC); then fn.EntitySharedPtr_dtor is used" },
    { "abi.wcharSize", KIND_FLAG, OPT,
      "Bytes per wchar_t in the game (2 or 4; default: the plugin's own). Mod names are converted when they differ" },

    { NULL, KIND_CHECK, false, NULL }
};

#undef REQ
#undef OPT

const KeyInfo *FindKey(const std::string &name)
{
    for (const KeyInfo *k = KEYS; k->name != NULL; ++k) {
        if (name == k->name) return k;
    }
    return NULL;
}

namespace {

std::wstring Widen(const std::string &s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) out += (wchar_t)(unsigned char)s[i];
    return out;
}

std::string Trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

// "0x1A", "26", or "<segment>:<number>".
bool ParseValue(const std::string &text, Addr &value, int &segment)
{
    std::string s = text;
    segment = -1;
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        std::string seg = s.substr(0, colon);
        if (seg.empty() || seg.size() > 2) return false;
        for (size_t i = 0; i < seg.size(); ++i) if (seg[i] < '0' || seg[i] > '9') return false;
        segment = atoi(seg.c_str());
        s = Trim(s.substr(colon + 1));
    }
    if (s.empty()) return false;
    int base = 10;
    size_t i = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; i = 2; }
    value = 0;
    for (; i < s.size(); ++i) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c == '_' || c == '\'') continue; // digit separators
        else return false;
        value = value * (Addr)base + (Addr)d;
    }
    return true;
}

} // namespace

bool AddressMap::parse(const std::string &text, std::wstring &error)
{
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;

        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;

        char num[16];
        sprintf(num, "%d", lineNo);
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = L"line " + Widen(num) + L": expected \"key = value\"";
            return false;
        }
        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        if (key == "game") { game = value; continue; }

        if (FindKey(key) == NULL) {
            error = L"line " + Widen(num) + L": unknown key \"" + Widen(key) + L"\"";
            return false;
        }
        Entry e;
        if (!ParseValue(value, e.value, e.segment)) {
            error = L"line " + Widen(num) + L": bad value for " + Widen(key) + L" (use 0x1234, 1234 or 0:0x1234)";
            return false;
        }
        m_values[key] = e;
    }
    return true;
}

bool AddressMap::has(const char *key) const
{
    return m_values.count(key) != 0;
}

Addr AddressMap::get(const char *key) const
{
    std::map<std::string, Entry>::const_iterator it = m_values.find(key);
    return it == m_values.end() ? 0 : it->second.value;
}

int AddressMap::segment(const char *key) const
{
    std::map<std::string, Entry>::const_iterator it = m_values.find(key);
    return it == m_values.end() ? -1 : it->second.segment;
}

void AddressMap::set(const char *key, Addr value, int seg)
{
    Entry e;
    e.value = value;
    e.segment = seg;
    m_values[key] = e;
}

std::string AddressMap::missingRequired() const
{
    std::string missing;
    for (const KeyInfo *k = KEYS; k->name != NULL; ++k) {
        if (k->required && !has(k->name)) {
            if (!missing.empty()) missing += ", ";
            missing += k->name;
        }
    }
    return missing;
}

} // namespace Inject
} // namespace ForgedLCE
