// ForgedLCE fake game layer: simple models of the LCE classes in FakeGame.h.
#include "stdafx.h"
#include "FakeGame.h"
#ifndef FLCE_FAKE_RETAIL
#include "FLCE_Hooks.h"
#endif

FakeApp app;

void FakeApp::DebugPrintf(const char *format, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;
    log += buf;
    fputs(buf, stdout);
}

// ---------------------------------------------------------------- Material

Material *Material::air, *Material::grass, *Material::dirt, *Material::wood, *Material::stone,
         *Material::metal, *Material::water, *Material::lava, *Material::leaves, *Material::plant,
         *Material::sand, *Material::cloth, *Material::glass, *Material::ice, *Material::clay;

void Material::staticCtor()
{
    air = new Material("air");       grass = new Material("grass");   dirt = new Material("dirt");
    wood = new Material("wood");     stone = new Material("stone");   metal = new Material("metal");
    water = new Material("water");   lava = new Material("lava");     leaves = new Material("leaves");
    plant = new Material("plant");   sand = new Material("sand");     cloth = new Material("cloth");
    glass = new Material("glass");   ice = new Material("ice");       clay = new Material("clay");
}

// ---------------------------------------------------------------- Level

std::string Level::key(int x, int y, int z)
{
    char buf[64];
    sprintf(buf, "%d,%d,%d", x, y, z);
    return buf;
}

int Level::getTile(int x, int y, int z)
{
    std::map<std::string, std::pair<int, int> >::const_iterator it = m_blocks.find(key(x, y, z));
    return it == m_blocks.end() ? 0 : it->second.first;
}

int Level::getData(int x, int y, int z)
{
    std::map<std::string, std::pair<int, int> >::const_iterator it = m_blocks.find(key(x, y, z));
    return it == m_blocks.end() ? 0 : it->second.second;
}

bool Level::setTile(int x, int y, int z, int tile)
{
    return setTileAndData(x, y, z, tile, 0);
}

bool Level::setTileAndData(int x, int y, int z, int tile, int data)
{
    if (y < 0 || y >= 256) return false;
    m_blocks[key(x, y, z)] = std::make_pair(tile, data);
    return true;
}

bool Level::addEntity(shared_ptr<Entity> e)
{
    if (e.get() == NULL) return false;

#ifndef FLCE_FAKE_RETAIL
    // ForgedLCE: EntityJoinWorld event, cancelable (same as patch 0004)
    if (ForgedLCE::OnEntityJoinWorld(this, e)) return false;
#endif

    fake_entities.push_back(e);
    return true;
}

// ---------------------------------------------------------------- Item

ItemArray Item::items;

Item::Item(int id)
    : id(256 + id), fake_maxStackSize(64), fake_maxDamage(0), fake_handEquipped(false), fake_descriptionId(0)
{
    if (items[256 + id] != NULL) app.DebugPrintf("CONFLICT @ %d\n", id);
    items[256 + id] = this;
}

Item *Item::setMaxDamage(int maxDamage)        { fake_maxDamage = maxDamage; return this; }
Item *Item::setTextureName(const wstring &n)   { fake_textureName = n; return this; }
Item *Item::setMaxStackSize(int max)           { fake_maxStackSize = max; return this; }
Item *Item::handEquipped()                     { fake_handEquipped = true; return this; }
Item *Item::setDescriptionId(unsigned int id)  { fake_descriptionId = id; return this; }

bool Item::TestUse(Level *, shared_ptr<Player>) { return false; }

shared_ptr<ItemInstance> Item::use(shared_ptr<ItemInstance> itemInstance, Level *, shared_ptr<Player>)
{
    return itemInstance;
}

TileItem::TileItem(int id) : Item(id) {}

namespace {
class VanillaItem : public Item
{
public:
    explicit VanillaItem(int numericId) : Item(numericId - 256) {}
};
}

void Item::staticCtor()
{
    // A few vanilla items with their real ids, enough for recipes in tests.
    new VanillaItem(260); // apple
    new VanillaItem(264); // diamond
    new VanillaItem(265); // iron ingot
    new VanillaItem(280); // stick
    new VanillaItem(388); // emerald
}

// ---------------------------------------------------------------- Tile

Tile::SoundType *Tile::SOUND_NORMAL, *Tile::SOUND_WOOD, *Tile::SOUND_GRAVEL, *Tile::SOUND_GRASS,
                *Tile::SOUND_STONE, *Tile::SOUND_METAL, *Tile::SOUND_GLASS, *Tile::SOUND_CLOTH,
                *Tile::SOUND_SAND, *Tile::SOUND_SNOW;
Tile **Tile::tiles = NULL;
int Tile::lightEmission[Tile::TILE_NUM_COUNT];

Tile::Tile(int id, Material *material, bool isSolidRender)
    : id(id), fake_material(material), fake_solidRender(isSolidRender), fake_destroySpeed(0),
      fake_explosionResistance(0), fake_soundType(SOUND_NORMAL), fake_descriptionId(0), fake_initCalled(false)
{
    if (tiles[id] != NULL) app.DebugPrintf("Tile slot %d already occupied\n", id);
    tiles[id] = this;
    lightEmission[id] = 0;
}

Tile *Tile::setSoundType(const SoundType *s)      { fake_soundType = s; return this; }
Tile *Tile::setLightBlock(int)                    { return this; }
Tile *Tile::setLightEmission(float f)             { lightEmission[id] = (int)(Level::MAX_BRIGHTNESS * f); return this; }
Tile *Tile::setExplodeable(float r)               { fake_explosionResistance = r * 3.0f; return this; }
void  Tile::init()                                { fake_initCalled = true; }
Tile *Tile::setDestroyTime(float s)               { fake_destroySpeed = s; return this; }
int   Tile::getResourceCount(Random *)            { return 1; }
int   Tile::getResource(int, Random *, int)       { return id; }
bool  Tile::TestUse()                             { return false; }
bool  Tile::TestUse(Level *, int, int, int, shared_ptr<Player>) { return false; }
bool  Tile::use(Level *, int, int, int, shared_ptr<Player>, int, float, float, float, bool) { return false; }
Tile *Tile::setDescriptionId(unsigned int i)      { fake_descriptionId = i; return this; }
unsigned int Tile::getDescriptionId(int)          { return fake_descriptionId; }
Tile *Tile::setTextureName(const wstring &n)      { fake_textureName = n; return this; }

namespace {
class VanillaTile : public Tile
{
public:
    explicit VanillaTile(int id) : Tile(id, Material::stone, true) {}
};
}

void Tile::staticCtor()
{
    SOUND_NORMAL = new SoundType(L"stone"); SOUND_WOOD  = new SoundType(L"wood");
    SOUND_GRAVEL = new SoundType(L"gravel"); SOUND_GRASS = new SoundType(L"grass");
    SOUND_STONE  = new SoundType(L"stone"); SOUND_METAL = new SoundType(L"metal");
    SOUND_GLASS  = new SoundType(L"glass"); SOUND_CLOTH = new SoundType(L"cloth");
    SOUND_SAND   = new SoundType(L"sand");  SOUND_SNOW  = new SoundType(L"snow");

    tiles = new Tile *[TILE_NUM_COUNT];
    memset(tiles, 0, sizeof(Tile *) * TILE_NUM_COUNT);
    if (Item::items.data == NULL) Item::items = ItemArray(Item::ITEM_NUM_COUNT);

    // Occupy roughly the same block ids as the real game: 1-145 plus the
    // 1.5/1.6 back-ports (quartz 153/155/156, carpet 171).
    for (int i = 1; i <= 145; ++i) new VanillaTile(i);
    new VanillaTile(153);
    new VanillaTile(155);
    new VanillaTile(156);
    new VanillaTile(171);

    // Like the real staticCtor: give every block a TileItem.
    for (int i = 0; i < 256; ++i) {
        if (tiles[i] != NULL && Item::items[i] == NULL) {
            new TileItem(i - 256);
            tiles[i]->init();
        }
    }
}

// ---------------------------------------------------------------- recipes

ShapedRecipy::ShapedRecipy(int width, int height, ItemInstance **recipeItems, ItemInstance *result, int)
    : fake_width(width), fake_height(height), fake_items(recipeItems), fake_result(result)
{
}

ShapelessRecipy::ShapelessRecipy(ItemInstance *result, vector<ItemInstance *> *ingredients, _eGroupType)
    : fake_result(result), fake_ingredients(ingredients)
{
}

Recipes *Recipes::instance = NULL;

Recipes::Recipes() : fake_ingredientsArraySize(0), recipies(new vector<Recipy *>())
{
    // Two vanilla-like recipes: planks -> sticks, cobblestone -> furnace.
    ItemInstance **sticks = new ItemInstance *[2];
    sticks[0] = new ItemInstance(5, 1, ANY_AUX_VALUE);
    sticks[1] = new ItemInstance(5, 1, ANY_AUX_VALUE);
    recipies->push_back(new ShapedRecipy(1, 2, sticks, new ItemInstance(280, 4, 0)));

    vector<ItemInstance *> *cobble = new vector<ItemInstance *>();
    cobble->push_back(new ItemInstance(4, 1, ANY_AUX_VALUE));
    recipies->push_back(new ShapelessRecipy(new ItemInstance(61, 1, 0), cobble));

    fake_ingredientsArraySize = recipies->size(); // the real constructor builds the cache here
}

void Recipes::staticCtor()
{
    instance = new Recipes();
}

size_t Recipes::fake_recipiesOffset()
{
    // Member offset without constructing a Recipes (its constructor adds recipes).
    union { char bytes[sizeof(Recipes)]; double align; } storage;
    Recipes *r = reinterpret_cast<Recipes *>(storage.bytes);
    return (size_t)(reinterpret_cast<char *>(&r->recipies) - storage.bytes);
}

void Recipes::flceRebuildIngredientsArray()
{
    fake_ingredientsArraySize = recipies->size();
}

FurnaceRecipes *FurnaceRecipes::instance = NULL;

void FurnaceRecipes::staticCtor()
{
    instance = new FurnaceRecipes();
}

void FurnaceRecipes::addFurnaceRecipy(int itemId, ItemInstance *result, float value)
{
    fake_results[itemId] = result;
    fake_xp[itemId] = value;
}

// ---------------------------------------------------------------- strings

LPCWSTR StringTable::getString(int id)
{
#ifndef FLCE_FAKE_RETAIL
    // ForgedLCE: names of mod blocks and items (same as patch 0007)
    if (id >= ForgedLCE::STRING_ID_BASE)
    {
        LPCWSTR modString = ForgedLCE::GetModString(id);
        return modString != NULL ? modString : L"";
    }
#endif
    return id >= 0x40000000 ? L"<missing>" : L"vanilla string";
}

// ---------------------------------------------------------------- game mode

bool ServerPlayerGameMode::destroyBlock(int x, int y, int z)
{
    int t = level->getTile(x, y, z);
    int data = level->getData(x, y, z);
#ifndef FLCE_FAKE_RETAIL
    // ForgedLCE: BlockBreak event, cancelable (same as patch 0005)
    if (ForgedLCE::OnBlockBreak(level, player, x, y, z, t, data)) return false;
#else
    (void)t; (void)data;
#endif
    return level->setTile(x, y, z, 0);
}

// ---------------------------------------------------------------- startup

void *MobCategory_monster = NULL;

void MobCategory_staticCtor()
{
    static int monster;
    MobCategory_monster = &monster;
}

void (*g_fakeSlot_MobCategory_staticCtor)() = &MobCategory_staticCtor;

void MinecraftWorld_RunStaticCtors()
{
    Material::staticCtor();
    Tile::staticCtor();
    Item::staticCtor();
    FurnaceRecipes::staticCtor();
    Recipes::staticCtor();

#ifdef FLCE_FAKE_RETAIL
    // No patches: an injected hook may have redirected this call.
    g_fakeSlot_MobCategory_staticCtor();
#else
    MobCategory_staticCtor();
    // ForgedLCE hook (patch 0002)
    ForgedLCE::OnStaticCtors();
#endif
}
