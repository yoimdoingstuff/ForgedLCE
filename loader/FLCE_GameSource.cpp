// ForgedLCE game backend for SOURCE builds: compiled into Minecraft.World
// by scripts/apply.ps1, it uses the game's C++ classes directly. Also holds
// the hook functions the patches call (FLCE_Hooks.h).
//
// The injected-plugin equivalent is inject/FLCE_GameInjected.cpp.
#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.level.material.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.crafting.h"
#include "net.minecraft.world.entity.h"
#include "net.minecraft.world.entity.player.h"
#include "Recipes.h"
#include "ShapedRecipy.h"
#include "ShapelessRecipy.h"
#include "FurnaceRecipes.h"

#include "FLCE_Hooks.h"
#include "FLCE_Internal.h"

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
    // each block's TileItem, and so does CreateBlock.
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

namespace {

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

ItemInstance *MakeInstance(int id, int count, int data)
{
    // Not `data < 0 ? Recipes::ANY_AUX_VALUE : data`: that binds a reference
    // to the static const, which needs an out-of-class definition on
    // stricter compilers.
    if (data < 0) data = Recipes::ANY_AUX_VALUE;
    return new ItemInstance(id, count, data);
}

} // namespace

// ---------------------------------------------------------------------
// Backend (FLCE_Backend.h)
// ---------------------------------------------------------------------

namespace Backend {

const wchar_t *Name() { return L"source"; }

bool IdInUse(int id)
{
    if (id < 0 || id >= Item::ITEM_NUM_COUNT) return true;
    if (id < Tile::TILE_NUM_COUNT && Tile::tiles[id] != NULL) return true;
    return Item::items[id] != NULL;
}

bool IdExists(int id)
{
    if (id <= 0 || id >= Item::ITEM_NUM_COUNT) return false;
    if (id < 256) return Tile::tiles[id] != NULL;
    return Item::items[id] != NULL;
}

bool CreateBlock(int id, const FLCE_BlockDesc &desc, const std::wstring &texture,
                 unsigned int nameId, std::wstring &)
{
    bool solidRender = desc.material != FLCE_MATERIAL_GLASS &&
                       desc.material != FLCE_MATERIAL_LEAVES &&
                       desc.material != FLCE_MATERIAL_PLANT;

    // The Tile constructor stores the block in Tile::tiles[id].
    FLCE_ModTile *tile = new FLCE_ModTile(id, MapMaterial(desc.material), solidRender,
                                          MapSound(desc.sound), desc, texture, nameId);

    // Tile::staticCtor's TileItem loop has already run, so do its job here.
    if (Item::items[id] == NULL) {
        new TileItem(id - 256); // the Item constructor stores it in Item::items[id]
        tile->flceInit();
    }
    return true;
}

bool CreateItem(int id, const FLCE_ItemDesc &desc, const std::wstring &texture,
                unsigned int nameId, std::wstring &)
{
    Item *item = new FLCE_ModItem(id, desc);
    item->setTextureName(texture);
    item->setDescriptionId(nameId);
    if (desc.maxStackSize > 0) item->setMaxStackSize(desc.maxStackSize);
    if (desc.handEquipped)     item->handEquipped();
    return true;
}

bool AddShaped(int width, int height, const int *slotIds, const int *slotData,
               int resultId, int resultCount, int resultData)
{
    ItemInstance **slots = new ItemInstance *[width * height];
    for (int i = 0; i < width * height; ++i) {
        slots[i] = slotIds[i] != 0 ? MakeInstance(slotIds[i], 1, slotData[i]) : NULL;
    }
    Recipes::getInstance()->getRecipies()->push_back(
        new ShapedRecipy(width, height, slots, MakeInstance(resultId, resultCount, resultData)));
    return true;
}

bool AddShapeless(const int *ids, int count, int resultId, int resultCount, int resultData)
{
    vector<ItemInstance *> *ingredients = new vector<ItemInstance *>();
    for (int i = 0; i < count; ++i) ingredients->push_back(MakeInstance(ids[i], 1, -1));
    Recipes::getInstance()->getRecipies()->push_back(
        new ShapelessRecipy(MakeInstance(resultId, resultCount, resultData), ingredients));
    return true;
}

bool AddSmelting(int inputId, int resultId, int resultCount, int resultData, float xp)
{
    FurnaceRecipes::getInstance()->addFurnaceRecipy(inputId, MakeInstance(resultId, resultCount, resultData), xp);
    return true;
}

void FinishRecipes()
{
    // Added by patches/0003: rebuilds the console crafting menu's cache.
    Recipes::getInstance()->flceRebuildIngredientsArray();
}

int GetTile(FLCE_Level *level, int x, int y, int z)
{
    return reinterpret_cast<Level *>(level)->getTile(x, y, z);
}

int GetData(FLCE_Level *level, int x, int y, int z)
{
    return reinterpret_cast<Level *>(level)->getData(x, y, z);
}

bool SetTile(FLCE_Level *level, int x, int y, int z, int id, int data)
{
    return reinterpret_cast<Level *>(level)->setTileAndData(x, y, z, id, data);
}

bool IsClientSide(FLCE_Level *level)
{
    return reinterpret_cast<Level *>(level)->isClientSide;
}

void GetEntityPos(FLCE_Entity *entity, double *x, double *y, double *z)
{
    Entity *e = reinterpret_cast<Entity *>(entity);
    *x = e->x;
    *y = e->y;
    *z = e->z;
}

} // namespace Backend

// ---------------------------------------------------------------------
// Hooks called from patched game code (FLCE_Hooks.h)
// ---------------------------------------------------------------------

void OnStaticCtors() { LoadAllMods(); }

void OnClientTick(int phase) { Events::PostTick(FLCE_EVENT_CLIENT_TICK, phase); }
void OnServerTick(int phase) { Events::PostTick(FLCE_EVENT_SERVER_TICK, phase); }

bool OnBlockBreak(Level *level, shared_ptr<Player> player, int x, int y, int z, int tileId, int data)
{
    return Events::PostBlockBreak(reinterpret_cast<FLCE_Level *>(level),
                                  reinterpret_cast<FLCE_Entity *>(static_cast<Entity *>(player.get())),
                                  x, y, z, tileId, data);
}

bool OnEntityJoinWorld(Level *level, shared_ptr<Entity> entity)
{
    // .get(): comparing a shared_ptr with NULL doesn't compile with every
    // platform's shared_ptr (e.g. C++03 tr1).
    Entity *e = entity.get();
    if (e == NULL) return false;
    int isPlayer = dynamic_pointer_cast<Player>(entity).get() != NULL ? 1 : 0;
    return Events::PostEntityJoin(reinterpret_cast<FLCE_Level *>(level), reinterpret_cast<FLCE_Entity *>(e),
                                  isPlayer, e->x, e->y, e->z);
}

} // namespace ForgedLCE
