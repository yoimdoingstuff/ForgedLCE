// ForgedLCE fake game layer: stand-ins for the LCE classes ForgedLCE uses.
//
// Each class mirrors ONLY the members the loader touches, with the same
// names, signatures and public/protected access as the real game headers,
// so code that compiles here should compile against the real game too.
// Behaviour is a simple model of the real thing, not a copy of it.
//
// Members prefixed `fake_` don't exist in the game; tests use them to look
// inside.
//
// Written in C++03 like the code it hosts.
#ifndef FLCE_FAKEGAME_H
#define FLCE_FAKEGAME_H

#include "stdafx.h"

class Level;
class Entity;
class Player;
class Tile;
class Item;
class ItemInstance;

// ---------------------------------------------------------------- basics

class Random
{
public:
    Random() : seed(1) {}
    int nextInt(int n) { seed = seed * 1103515245u + 12345u; return n > 0 ? (int)((seed >> 16) % (unsigned)n) : 0; }
private:
    unsigned int seed;
};

// Minimal version of the game's arrayWithLength<T>: a pointer plus length.
template <class T> class arrayWithLength
{
public:
    T           *data;
    unsigned int length;
    arrayWithLength() : data(NULL), length(0) {}
    explicit arrayWithLength(unsigned int n) : data(new T[n]), length(n) { memset(data, 0, sizeof(T) * n); }
    T &operator[](unsigned int i) { return data[i]; }
    T  operator[](unsigned int i) const { return data[i]; }
};
typedef arrayWithLength<Item *> ItemArray;

class Material
{
public:
    static Material *air, *grass, *dirt, *wood, *stone, *metal, *water, *lava, *leaves,
                    *plant, *sand, *cloth, *glass, *ice, *clay;
    static void staticCtor();

    explicit Material(const char *name) : fake_name(name) {}
    const char *fake_name;
};

// ---------------------------------------------------------------- entities

class Entity
{
public:
    Entity() : x(0), y(0), z(0) {}
    virtual ~Entity() {}
    double x, y, z;
};

class Player : public Entity
{
};

// ---------------------------------------------------------------- level

class Level
{
public:
    static const int MAX_BRIGHTNESS = 15;

    explicit Level(bool clientSide) : isClientSide(clientSide) {}
    virtual ~Level() {}

    bool isClientSide;

    virtual int getTile(int x, int y, int z);
    virtual int getData(int x, int y, int z);
    bool setTile(int x, int y, int z, int tile);
    bool setTileAndData(int x, int y, int z, int tile, int data);

    // Contains ForgedLCE patch 0004 (EntityJoinWorld), like the real one.
    virtual bool addEntity(shared_ptr<Entity> e);

    std::vector< shared_ptr<Entity> > fake_entities;

private:
    std::map<std::string, std::pair<int, int> > m_blocks; // "x,y,z" -> (tile, data)
    static std::string key(int x, int y, int z);
};

// ---------------------------------------------------------------- items

class ItemInstance
{
public:
    ItemInstance(int id, int count, int damage) : id(id), count(count), auxValue(damage) {}
    int getAuxValue() const { return auxValue; }
    int id;
    int count;
private:
    int auxValue;
};

class Item
{
public:
    static const int ITEM_NUM_COUNT = 32000;
    static ItemArray items;
    static void staticCtor();

    const int id;
    virtual ~Item() {}

protected:
    Item(int id);                    // stores itself in items[256 + id]
    Item *setMaxDamage(int maxDamage);

public:
    Item *setTextureName(const wstring &name);
    Item *setMaxStackSize(int max);
    Item *handEquipped();
    Item *setDescriptionId(unsigned int id);

    virtual bool TestUse(Level *level, shared_ptr<Player> player);
    virtual shared_ptr<ItemInstance> use(shared_ptr<ItemInstance> itemInstance, Level *level, shared_ptr<Player> player);

    wstring      fake_textureName;
    int          fake_maxStackSize;
    int          fake_maxDamage;
    bool         fake_handEquipped;
    unsigned int fake_descriptionId;
};

class TileItem : public Item
{
public:
    TileItem(int id);
};

// ---------------------------------------------------------------- tiles

class Tile
{
public:
    class SoundType
    {
    public:
        explicit SoundType(const wchar_t *name) : fake_name(name) {}
        const wchar_t *fake_name;
    };

    static SoundType *SOUND_NORMAL, *SOUND_WOOD, *SOUND_GRAVEL, *SOUND_GRASS, *SOUND_STONE,
                     *SOUND_METAL, *SOUND_GLASS, *SOUND_CLOTH, *SOUND_SAND, *SOUND_SNOW;

    static const int TILE_NUM_COUNT = 4096;
    static const int TILE_NUM_SHIFT = 12;
    static Tile **tiles;
    static int lightEmission[TILE_NUM_COUNT];
    static void staticCtor(); // vanilla tiles, then a TileItem for each

    int id;
    virtual ~Tile() {}

protected:
    Tile(int id, Material *material, bool isSolidRender = true);
    virtual Tile *setSoundType(const SoundType *soundType);
    virtual Tile *setLightBlock(int i);
    virtual Tile *setLightEmission(float f);
    virtual Tile *setExplodeable(float explosionResistance);
    virtual void  init();
    virtual Tile *setDestroyTime(float destroySpeed);

public:
    virtual int  getResourceCount(Random *random);
    virtual int  getResource(int data, Random *random, int playerBonusLevel);
    virtual bool TestUse();
    virtual bool TestUse(Level *level, int x, int y, int z, shared_ptr<Player> player);
    virtual bool use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace,
                     float clickX, float clickY, float clickZ, bool soundOnly = false);
    virtual Tile *setDescriptionId(unsigned int id);
    virtual unsigned int getDescriptionId(int iData = -1);
    Tile *setTextureName(const wstring &name);

    Material        *fake_material;
    bool             fake_solidRender;
    float            fake_destroySpeed;
    float            fake_explosionResistance;
    const SoundType *fake_soundType;
    wstring          fake_textureName;
    unsigned int     fake_descriptionId;
    bool             fake_initCalled;
};

// ---------------------------------------------------------------- recipes

class Recipy
{
public:
    enum _eGroupType {
        eGroupType_First = 0,
        eGroupType_Structure = 0,
        eGroupType_Tool,
        eGroupType_Food,
        eGroupType_Armour,
        eGroupType_Mechanism,
        eGroupType_Transport,
        eGroupType_Decoration,
        eGroupType_Max
    };
    virtual ~Recipy() {}
};

class ShapedRecipy : public Recipy
{
public:
    ShapedRecipy(int width, int height, ItemInstance **recipeItems, ItemInstance *result, int iGroup = Recipy::eGroupType_Decoration);
    int            fake_width, fake_height;
    ItemInstance **fake_items;
    ItemInstance  *fake_result;
};

class ShapelessRecipy : public Recipy
{
public:
    ShapelessRecipy(ItemInstance *result, vector<ItemInstance *> *ingredients, _eGroupType egroup = Recipy::eGroupType_Decoration);
    ItemInstance            *fake_result;
    vector<ItemInstance *>  *fake_ingredients;
};

class Recipes
{
public:
    static const int ANY_AUX_VALUE = -1;
    static void staticCtor();
    static Recipes *getInstance() { return instance; }
    vector<Recipy *> *getRecipies() { return recipies; }

    // Added to the real game by ForgedLCE patch 0003.
    void flceRebuildIngredientsArray();

    size_t fake_ingredientsArraySize; // size of the console crafting-menu cache

    // For the injected-mode tests' address map.
    static Recipes **fake_instanceAddress() { return &instance; }
    static size_t fake_recipiesOffset();

private:
    Recipes();
    static Recipes   *instance;
    vector<Recipy *> *recipies;
};

class FurnaceRecipes
{
public:
    static void staticCtor();
    static FurnaceRecipes *getInstance() { return instance; }
    void addFurnaceRecipy(int itemId, ItemInstance *result, float value);

    std::map<int, ItemInstance *> fake_results;
    std::map<int, float>          fake_xp;

    static FurnaceRecipes **fake_instanceAddress() { return &instance; }

private:
    static FurnaceRecipes *instance;
};

// ---------------------------------------------------------------- game mode

class ServerPlayerGameMode
{
public:
    ServerPlayerGameMode() : level(NULL) {}
    Level             *level;
    shared_ptr<Player> player;

    // Contains ForgedLCE patch 0005 (BlockBreak) in source mode. Breaks the
    // block (sets it to air) unless a handler cancels.
    bool destroyBlock(int x, int y, int z);
};

// ---------------------------------------------------------------- strings

class StringTable
{
public:
    // Contains ForgedLCE patch 0007, like the real one. Vanilla strings
    // come back as L"vanilla string".
    LPCWSTR getString(int id);
};

// ---------------------------------------------------------------- startup

// Stand-in for MobCategory::staticCtor(). Sets MobCategory_monster, like the
// real one sets MobCategory::monster.
void MobCategory_staticCtor();
extern void *MobCategory_monster;

// Mirrors the order of the real MinecraftWorld_RunStaticCtors() up to the
// ForgedLCE hook point.
//  - Source mode: calls MobCategory_staticCtor, then the hook (patch 0002).
//  - Retail mode (FLCE_FAKE_RETAIL, no patches): calls MobCategory_staticCtor
//    through g_fakeSlot_MobCategory_staticCtor, which an injected hook
//    redirects, the way a detour redirects the real call.
void MinecraftWorld_RunStaticCtors();
extern void (*g_fakeSlot_MobCategory_staticCtor)();

#endif // FLCE_FAKEGAME_H
