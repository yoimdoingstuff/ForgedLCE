// ForgedLCE injected-mode tests.
//
// Boots the fake game in RETAIL mode (no source patches), then does what the
// console plugins do: binds an address map, verifies the game version,
// installs hooks and lets the game start. The hook "engine" here redirects
// the fake game's call slots instead of rewriting machine code; everything
// else (address map, injected backend, hook functions, loader core) is the
// code that runs in the real plugins.
//
// Usage: flce_inject_tests <DllMod module> <ExampleMod module> <work dir> <TEMPLATE.txt>
#include "stdafx.h"
#include "FakeGame.h"
#include "FLCE_Internal.h"
#include "FLCE_InjectGame.h"
#include "FLCE_HookPPC.h"
#include "PlatformHost.h"
#include "TestModState.h"
#include "forgedlce/ForgedLCE.hpp"

#include <iostream>
#include <new>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

using namespace ForgedLCE;
using Inject::Addr;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (!ok) { ++g_failures; std::cout << "FAIL " << file << ":" << line << ": " << expr << std::endl; }
}

void checkEq(long long a, long long b, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (a != b) {
        ++g_failures;
        std::cout << "FAIL " << file << ":" << line << ": " << expr << " is " << a << ", expected " << b << std::endl;
    }
}

#define CHECK(e)       check((e), #e, __FILE__, __LINE__)
#define CHECK_EQ(a, b) checkEq((long long)(a), (long long)(b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_HEX(a, b) checkEq((long long)(unsigned int)(a), (long long)(unsigned int)(b), #a " == " #b, __FILE__, __LINE__)

bool logHas(const char *text)
{
    bool found = Platform::HostLog().find(text) != std::string::npos;
    if (!found) std::cout << "  (log does not contain \"" << text << "\")" << std::endl;
    return found;
}

bool wideEq(const wchar_t *a, const wchar_t *b) { return a != NULL && wcscmp(a, b) == 0; }

template <typename T> Addr A(T *p) { return (Addr)(size_t)p; }
template <typename Fn> Addr F(Fn fn) { return (Addr)(size_t)fn; }

// ---------------------------------------------------------------------
// The "compiled game": adapters with the calling convention the plugin
// uses (member functions as plain functions taking `this` first).
// ---------------------------------------------------------------------

class RetailTile : public Tile
{
public:
    RetailTile(int id, Material *m, bool solid) : Tile(id, m, solid) {}
    using Tile::setDestroyTime;
    using Tile::setExplodeable;
    using Tile::setSoundType;
    using Tile::setLightEmission;
    using Tile::init;
};

class RetailItem : public Item
{
public:
    explicit RetailItem(int id) : Item(id) {}
    using Item::setMaxDamage;
};

typedef std::wstring WString;
typedef shared_ptr<Entity> EntityPtr;

void *G_new(size_t n)                                     { return ::operator new(n); }
void *G_Tile_ctor(void *s, int id, void *m, bool solid)   { return new (s) RetailTile(id, (Material *)m, solid); }
void *G_Tile_setDestroyTime(void *s, float v)             { return static_cast<RetailTile *>(s)->setDestroyTime(v); }
void *G_Tile_setExplodeable(void *s, float v)             { return static_cast<RetailTile *>(s)->setExplodeable(v); }
void *G_Tile_setSoundType(void *s, const void *t)         { return static_cast<RetailTile *>(s)->setSoundType((const Tile::SoundType *)t); }
void *G_Tile_setLightEmission(void *s, float v)           { return static_cast<RetailTile *>(s)->setLightEmission(v); }
void *G_Tile_setTextureName(void *s, const void *w)       { return static_cast<Tile *>(s)->setTextureName(*(const WString *)w); }
void *G_Tile_setDescriptionId(void *s, unsigned int id)   { return static_cast<Tile *>(s)->setDescriptionId(id); }
void  G_Tile_init(void *s)                                { static_cast<RetailTile *>(s)->init(); }
void *G_TileItem_ctor(void *s, int id)                    { return new (s) TileItem(id); }
void *G_Item_ctor(void *s, int id)                        { return new (s) RetailItem(id); }
void *G_Item_setTextureName(void *s, const void *w)       { return static_cast<Item *>(s)->setTextureName(*(const WString *)w); }
void *G_Item_setMaxStackSize(void *s, int v)              { return static_cast<Item *>(s)->setMaxStackSize(v); }
void *G_Item_setMaxDamage(void *s, int v)                 { return static_cast<RetailItem *>(s)->setMaxDamage(v); }
void *G_Item_handEquipped(void *s)                        { return static_cast<Item *>(s)->handEquipped(); }
void *G_Item_setDescriptionId(void *s, unsigned int id)   { return static_cast<Item *>(s)->setDescriptionId(id); }
void *G_ItemInstance_ctor(void *s, int id, int c, int d)  { return new (s) ItemInstance(id, c, d); }
void *G_Shaped_ctor(void *s, int w, int h, void **items, void *r, int g)
{
    return new (s) ShapedRecipy(w, h, (ItemInstance **)items, (ItemInstance *)r, g);
}
void *G_Shapeless_ctor(void *s, void *r, void *ingredients, int g)
{
    return new (s) ShapelessRecipy((ItemInstance *)r, (vector<ItemInstance *> *)ingredients, (Recipy::_eGroupType)g);
}
void  G_RecipyVec_push(void *v, void *const *x)           { ((vector<Recipy *> *)v)->push_back((Recipy *)*x); }
void  G_ItemInstVec_push(void *v, void *const *x)         { ((vector<ItemInstance *> *)v)->push_back((ItemInstance *)*x); }
void  G_buildIngredients(void *s)                         { ((Recipes *)s)->flceRebuildIngredientsArray(); }
void  G_addFurnace(void *s, int id, void *r, float xp)    { ((FurnaceRecipes *)s)->addFurnaceRecipy(id, (ItemInstance *)r, xp); }
void *G_wstring_ctor(void *s, const wchar_t *t)           { return new (s) WString(t); }
void  G_wstring_dtor(void *s)                             { ((WString *)s)->~WString(); }
int   G_Level_getTile(void *s, int x, int y, int z)       { return ((Level *)s)->getTile(x, y, z); }
int   G_Level_getData(void *s, int x, int y, int z)       { return ((Level *)s)->getData(x, y, z); }
bool  G_Level_setTileAndData(void *s, int x, int y, int z, int t, int d) { return ((Level *)s)->setTileAndData(x, y, z, t, d); }

// Hookable game functions, called through slots (as a detour would redirect them).
int g_clientTicks = 0, g_serverTicks = 0;
const wchar_t *G_GetString(void *s, int id)               { return ((StringTable *)s)->getString(id); }
void  G_ClientTick(void *, bool, bool)                    { ++g_clientTicks; }
void  G_ServerTick(void *)                                { ++g_serverTicks; }
bool  G_DestroyBlock(void *s, int x, int y, int z)        { return ((ServerPlayerGameMode *)s)->destroyBlock(x, y, z); }
bool  G_AddEntity(void *s, void *spArg)                   { return ((Level *)s)->addEntity(*(EntityPtr *)spArg); }

const wchar_t *(*g_slotGetString)(void *, int) = &G_GetString;
void (*g_slotClientTick)(void *, bool, bool) = &G_ClientTick;
void (*g_slotServerTick)(void *) = &G_ServerTick;
bool (*g_slotDestroyBlock)(void *, int, int, int) = &G_DestroyBlock;
bool (*g_slotAddEntity)(void *, void *) = &G_AddEntity;

unsigned int g_checkWord = 0x7D8802A6; // "an instruction in the game" for check.address

// ---- simulated hook engine and address resolution --------------------

struct Slot { Addr target; void **slot; };
std::vector<Slot> g_slots;
int g_hooksInstalled = 0;

bool SimInstall(const char *, Addr target, void *replacement, void **original, std::wstring &error)
{
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].target == target) {
            *original = (void *)(size_t)target; // calling the original = calling the game function
            *g_slots[i].slot = replacement;
            ++g_hooksInstalled;
            return true;
        }
    }
    error = L"no such function";
    return false;
}

Addr SimResolve(int, Addr value) { return value; }

template <typename Fn> void AddSlot(Fn &slot) { Slot s; s.target = F(slot); s.slot = (void **)&slot; g_slots.push_back(s); }

// ---- the address map for the fake game ----------------------------------

Inject::AddressMap BuildMap()
{
    Inject::AddressMap m;
    m.game = "ForgedLCE fake game (retail mode)";
    m.set("check.address", A(&g_checkWord));
    m.set("check.value", 0x7D8802A6);

    m.set("hook.MobCategory_staticCtor", F(&MobCategory_staticCtor));
    m.set("hook.GetString", F(&G_GetString));
    m.set("hook.Minecraft_tick", F(&G_ClientTick));
    m.set("hook.MinecraftServer_tick", F(&G_ServerTick));
    m.set("hook.ServerPlayerGameMode_destroyBlock", F(&G_DestroyBlock));
    m.set("hook.Level_addEntity", F(&G_AddEntity));

    m.set("fn.operator_new", F(&G_new));
    m.set("fn.Tile_ctor", F(&G_Tile_ctor));
    m.set("fn.Tile_setDestroyTime", F(&G_Tile_setDestroyTime));
    m.set("fn.Tile_setExplodeable", F(&G_Tile_setExplodeable));
    m.set("fn.Tile_setSoundType", F(&G_Tile_setSoundType));
    m.set("fn.Tile_setLightEmission", F(&G_Tile_setLightEmission));
    m.set("fn.Tile_setTextureName", F(&G_Tile_setTextureName));
    m.set("fn.Tile_setDescriptionId", F(&G_Tile_setDescriptionId));
    m.set("fn.Tile_init", F(&G_Tile_init));
    m.set("fn.TileItem_ctor", F(&G_TileItem_ctor));
    m.set("fn.Item_ctor", F(&G_Item_ctor));
    m.set("fn.Item_setTextureName", F(&G_Item_setTextureName));
    m.set("fn.Item_setMaxStackSize", F(&G_Item_setMaxStackSize));
    m.set("fn.Item_setMaxDamage", F(&G_Item_setMaxDamage));
    m.set("fn.Item_handEquipped", F(&G_Item_handEquipped));
    m.set("fn.Item_setDescriptionId", F(&G_Item_setDescriptionId));
    m.set("fn.ItemInstance_ctor", F(&G_ItemInstance_ctor));
    m.set("fn.ShapedRecipy_ctor", F(&G_Shaped_ctor));
    m.set("fn.ShapelessRecipy_ctor", F(&G_Shapeless_ctor));
    m.set("fn.RecipyPtrVector_push_back", F(&G_RecipyVec_push));
    m.set("fn.ItemInstancePtrVector_push_back", F(&G_ItemInstVec_push));
    m.set("fn.Recipes_buildRecipeIngredientsArray", F(&G_buildIngredients));
    m.set("fn.FurnaceRecipes_addFurnaceRecipy", F(&G_addFurnace));
    m.set("fn.wstring_ctor", F(&G_wstring_ctor));
    m.set("fn.wstring_dtor", F(&G_wstring_dtor));
    m.set("fn.Level_getTile", F(&G_Level_getTile));
    m.set("fn.Level_getData", F(&G_Level_getData));
    m.set("fn.Level_setTileAndData", F(&G_Level_setTileAndData));

    m.set("data.Tile_tiles", A(&Tile::tiles));
    m.set("data.Item_items", A(&Item::items));
    m.set("data.Recipes_instance", A(Recipes::fake_instanceAddress()));
    m.set("data.FurnaceRecipes_instance", A(FurnaceRecipes::fake_instanceAddress()));
    m.set("data.Material_stone", A(&Material::stone));   m.set("data.Material_wood", A(&Material::wood));
    m.set("data.Material_dirt", A(&Material::dirt));     m.set("data.Material_grass", A(&Material::grass));
    m.set("data.Material_sand", A(&Material::sand));     m.set("data.Material_metal", A(&Material::metal));
    m.set("data.Material_glass", A(&Material::glass));   m.set("data.Material_cloth", A(&Material::cloth));
    m.set("data.Material_plant", A(&Material::plant));   m.set("data.Material_leaves", A(&Material::leaves));
    m.set("data.Material_ice", A(&Material::ice));       m.set("data.Material_clay", A(&Material::clay));
    m.set("data.Tile_SOUND_stone", A(&Tile::SOUND_STONE)); m.set("data.Tile_SOUND_wood", A(&Tile::SOUND_WOOD));
    m.set("data.Tile_SOUND_gravel", A(&Tile::SOUND_GRAVEL)); m.set("data.Tile_SOUND_grass", A(&Tile::SOUND_GRASS));
    m.set("data.Tile_SOUND_metal", A(&Tile::SOUND_METAL)); m.set("data.Tile_SOUND_glass", A(&Tile::SOUND_GLASS));
    m.set("data.Tile_SOUND_cloth", A(&Tile::SOUND_CLOTH)); m.set("data.Tile_SOUND_sand", A(&Tile::SOUND_SAND));
    m.set("data.Tile_SOUND_snow", A(&Tile::SOUND_SNOW));
    m.set("data.LateCheck", A(&MobCategory_monster));

    m.set("size.Tile", sizeof(RetailTile));
    m.set("size.TileItem", sizeof(TileItem));
    m.set("size.Item", sizeof(RetailItem));
    m.set("size.ItemInstance", sizeof(ItemInstance));
    m.set("size.ShapedRecipy", sizeof(ShapedRecipy));
    m.set("size.ShapelessRecipy", sizeof(ShapelessRecipy));
    m.set("size.vector", sizeof(vector<ItemInstance *>));
    m.set("size.wstring", sizeof(WString));

    Level level(false);
    Entity entity;
    ServerPlayerGameMode gameMode;
    m.set("offset.Recipes_recipies", Recipes::fake_recipiesOffset());
    m.set("offset.Level_isClientSide", (Addr)((char *)&level.isClientSide - (char *)&level));
    m.set("offset.Entity_x", (Addr)((char *)&entity.x - (char *)&entity));
    m.set("offset.ServerPlayerGameMode_level", (Addr)((char *)&gameMode.level - (char *)&gameMode));
    m.set("offset.ServerPlayerGameMode_player", (Addr)((char *)&gameMode.player - (char *)&gameMode));

    // Where the raw pointer sits inside this compiler's shared_ptr.
    EntityPtr probe(new Entity());
    size_t raw = 0;
    while (raw < sizeof(EntityPtr) && *(Entity **)((char *)&probe + raw) != probe.get()) raw += sizeof(void *);
    m.set("offset.SharedPtr_raw", raw);
    m.set("offset.Player_entity", 0);
    m.set("abi.sharedPtrArgByPointer", 1); // the slots pass a pointer to the shared_ptr
    return m;
}

// ---------------------------------------------------------------------
// Unit tests: PowerPC hook builder
// ---------------------------------------------------------------------

void TestPPC()
{
    using namespace ForgedLCE::PPC;
    Word jump[4];
    EncodeJump(0x91234568, jump);
    CHECK_HEX(jump[0], 0x3D609123);   // lis   r11, 0x9123
    CHECK_HEX(jump[1], 0x616B4568);   // ori   r11, r11, 0x4568
    CHECK_HEX(jump[2], 0x7D6903A6);   // mtctr r11
    CHECK_HEX(jump[3], 0x4E800420);   // bctr

    // A typical MSVC prologue at 0x82100000:
    //   mflr r12 ; bl __savegprlr (at 0x820FF000) ; stwu r1,-128(r1) ; mr r31,r3
    Word prologue[4] = { 0x7D8802A6, 0x4BFFEFFD, 0x9421FF80, 0x7C7F1B78 };
    Word patch[4], tramp[MAX_TRAMPOLINE_WORDS];
    int words = 0;
    const char *error = NULL;
    CHECK(BuildHook(prologue, 0x82100000, 0x91000000, patch, tramp, &words, &error));
    CHECK_EQ(words, 11);
    Word expected[11] = {
        0x7D8802A6,                                         // mflr r12 (copied)
        0x3D60820F, 0x616BF000, 0x7D6903A6, 0x4E800421,     // bl rewritten as absolute call to 0x820FF000
        0x9421FF80, 0x7C7F1B78,                             // copied
        0x3D608210, 0x616B0010, 0x7D6903A6, 0x4E800420      // jump back to 0x82100010
    };
    bool same = true;
    for (int i = 0; i < 11; ++i) if (tramp[i] != expected[i]) same = false;
    CHECK(same);
    CHECK_HEX(patch[0], 0x3D609100);
    CHECK_HEX(patch[1], 0x616B0000);
    CHECK_HEX(patch[3], 0x4E800420);

    // An absolute call (bla 0x100) stays absolute.
    Word withBla[4] = { 0x48000103, 0x60000000, 0x60000000, 0x60000000 };
    CHECK(BuildHook(withBla, 0x82000000, 0x91000000, patch, tramp, &words, &error));
    CHECK_HEX(tramp[0], 0x3D600000);
    CHECK_HEX(tramp[1], 0x616B0100);
    CHECK_HEX(tramp[3], 0x4E800421);

    // A relative conditional branch (beq +8) can't be moved.
    Word withBeq[4] = { 0x41820008, 0x60000000, 0x60000000, 0x60000000 };
    CHECK(!BuildHook(withBeq, 0x82000000, 0x91000000, patch, tramp, &words, &error));

    // An unconditional jump in the first 3 instructions: function too short.
    Word shortFn[4] = { 0x48000010, 0x60000000, 0x60000000, 0x60000000 };
    CHECK(!BuildHook(shortFn, 0x82000000, 0x91000000, patch, tramp, &words, &error));

    // ...but as the 4th instruction it becomes the trampoline's last jump.
    // b -12 at 0x8200000C jumps to 0x82000000.
    Word tail[4] = { 0x60000000, 0x60000000, 0x60000000, 0x4BFFFFF4 };
    CHECK(BuildHook(tail, 0x82000000, 0x91000000, patch, tramp, &words, &error));
    CHECK_EQ(words, 7);
    CHECK_HEX(tramp[3], 0x3D608200);
    CHECK_HEX(tramp[4], 0x616B0000);
    CHECK_HEX(tramp[6], 0x4E800420);
}

// ---------------------------------------------------------------------
// Unit tests: mod names in the game's wchar_t width
// ---------------------------------------------------------------------

void TestGameText()
{
    // "A", e-acute, and U+1F600 (needs a surrogate pair in UTF-16).
    std::wstring s;
    s += L'A';
    s += (wchar_t)0xE9;
    if (sizeof(wchar_t) == 2) { s += (wchar_t)0xD83D; s += (wchar_t)0xDE00; }
    else                      { s += (wchar_t)0x1F600; }

    std::vector<unsigned char> out;
    const unsigned short *u16 = static_cast<const unsigned short *>(Inject::ToGameText(s, 2, out));
    CHECK_EQ(out.size(), 10);
    CHECK(u16[0] == 0x41 && u16[1] == 0xE9 && u16[2] == 0xD83D && u16[3] == 0xDE00 && u16[4] == 0);

    const unsigned int *u32 = static_cast<const unsigned int *>(Inject::ToGameText(s, 4, out));
    CHECK_EQ(out.size(), 16);
    CHECK(u32[0] == 0x41 && u32[1] == 0xE9 && u32[2] == 0x1F600 && u32[3] == 0);
}

// ---------------------------------------------------------------------
// Unit tests: address map text format and template
// ---------------------------------------------------------------------

void TestAddressMapText(const std::string &templatePath)
{
    Inject::AddressMap m;
    std::wstring err;
    CHECK(m.parse("# comment\n\ngame = Test, v1\nfn.Tile_ctor = 0x8215A2C8  # trailing comment\n"
                  "size.Tile = 128\r\nfn.Tile_init = 0:0x0015A2C9\n", err));
    CHECK(m.game == "Test, v1");
    CHECK_HEX(m.get("fn.Tile_ctor"), 0x8215A2C8);
    CHECK_EQ(m.segment("fn.Tile_ctor"), -1);
    CHECK_EQ(m.get("size.Tile"), 128);
    CHECK_HEX(m.get("fn.Tile_init"), 0x0015A2C9);
    CHECK_EQ(m.segment("fn.Tile_init"), 0);
    CHECK(!m.missingRequired().empty());

    Inject::AddressMap bad;
    CHECK(!bad.parse("fn.NotAThing = 0x1\n", err));
    CHECK(err.find(L"unknown key") != std::wstring::npos && err.find(L"line 1") != std::wstring::npos);
    CHECK(!bad.parse("\nfn.Tile_ctor = 0xZZ\n", err));
    CHECK(err.find(L"line 2") != std::wstring::npos);
    CHECK(!bad.parse("fn.Tile_ctor 0x1\n", err));

    // The template mentions every key, and parses (all keys commented out).
    std::string tmpl;
    CHECK(Platform::ReadFile(Platform::HostWiden(templatePath), tmpl));
    int missing = 0;
    for (const Inject::KeyInfo *k = Inject::KEYS; k->name != NULL; ++k) {
        if (tmpl.find(std::string("# ") + k->name + " = ") == std::string::npos) {
            ++missing;
            std::cout << "  TEMPLATE.txt lacks " << k->name << std::endl;
        }
    }
    CHECK_EQ(missing, 0);
    Inject::AddressMap fromTemplate;
    CHECK(fromTemplate.parse(tmpl, err));
}

// ---------------------------------------------------------------------
// Files for the test mods folder
// ---------------------------------------------------------------------

void MakeDir(const std::wstring &dir)
{
#ifdef _WIN32
    CreateDirectoryW(dir.c_str(), NULL);
#else
    std::string s;
    for (size_t i = 0; i < dir.size(); ++i) s += (char)dir[i];
    mkdir(s.c_str(), 0777);
#endif
}

bool CopyInto(const std::string &from, const std::wstring &toDir)
{
    std::string bytes;
    if (!Platform::ReadFile(Platform::HostWiden(from), bytes)) return false;
    size_t slash = from.find_last_of("/\\");
    std::string name = slash == std::string::npos ? from : from.substr(slash + 1);
    return Platform::WriteFile(toDir + Platform::HostWiden(name), bytes);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 5) {
        std::cout << "usage: flce_inject_tests <DllMod module> <ExampleMod module> <work dir> <TEMPLATE.txt>" << std::endl;
        return 2;
    }
    TestModState &S = g_testModState;

    TestPPC();
    TestGameText();
    TestAddressMapText(argv[4]);

    // ---- the mods folder: two code-mod modules and a data mod -----------
    std::wstring sep = Platform::PathSeparator();
    std::wstring work = Platform::HostWiden(argv[3]);
    if (work.empty() || work.substr(work.size() - 1) != sep) work += sep;
    MakeDir(work);
    std::wstring mods = work + L"mods" + sep;
    MakeDir(mods);
    Platform::WriteFile(mods + L"forgedlce_ids.txt", "");  // start from no saved ids
    CHECK(CopyInto(argv[1], mods));
    CHECK(CopyInto(argv[2], mods));
    MakeDir(mods + L"rubymod");
    Platform::WriteFile(mods + L"rubymod" + sep + L"forgedlce.json",
        "{ \"modId\": \"rubymod\", \"version\": \"1.0\",\n"
        "  \"items\": [ { \"name\": \"ruby\", \"displayName\": \"Ruby\", \"texture\": \"emerald\" } ],\n"
        "  \"blocks\": [ { \"name\": \"ruby_block\", \"displayName\": \"Block of Ruby\", \"material\": \"metal\",\n"
        "                \"sound\": \"metal\", \"hardness\": 5, \"light\": 15, \"texture\": \"blockEmerald\" },\n"
        "              { \"name\": \"ruby_ore\", \"drops\": \"ruby\" } ],\n"
        "  \"recipes\": [ { \"type\": \"shaped\", \"result\": \"ruby_block\", \"pattern\": [\"RR\", \"RR\"], \"key\": { \"R\": \"ruby\" } } ],\n"
        "  \"smelting\": [ { \"input\": \"ruby_ore\", \"result\": \"ruby\", \"xp\": 0.5 } ] }\n");
    Platform::HostSetModsDir(mods);

    // ---- what the plugin does at startup ------------------------------------
    AddSlot(g_fakeSlot_MobCategory_staticCtor);
    AddSlot(g_slotGetString);
    AddSlot(g_slotClientTick);
    AddSlot(g_slotServerTick);
    AddSlot(g_slotDestroyBlock);
    AddSlot(g_slotAddEntity);

    Inject::AddressMap map = BuildMap();
    std::wstring err;
    CHECK(map.missingRequired().empty());

    Inject::AddressMap zeroFn = map;                   // a required function at address 0
    zeroFn.set("fn.Tile_ctor", 0);
    CHECK(!Inject::BindGame(zeroFn, SimResolve, err));
    CHECK(err.find(L"fn.Tile_ctor") != std::wstring::npos);
    Inject::AddressMap badWchar = map;
    badWchar.set("abi.wcharSize", 3);
    CHECK(!Inject::BindGame(badWchar, SimResolve, err));

    CHECK(Inject::BindGame(map, SimResolve, err));

    g_checkWord = 0x12345678;                          // a different game version
    CHECK(!Inject::VerifyGameVersion(err));
    CHECK(err.find(L"different game version") != std::wstring::npos);
    g_checkWord = 0x7D8802A6;
    CHECK(Inject::VerifyGameVersion(err));

    CHECK(!Inject::StartupAlreadyPassed());
    CHECK(Inject::InstallHooks(SimInstall));
    CHECK_EQ(g_hooksInstalled, 6);

    // ---- the game starts; the startup hook loads every mod ------------------
    MinecraftWorld_RunStaticCtors();

    CHECK(CurrentPhase() == PHASE_DONE);
    CHECK(Inject::StartupAlreadyPassed());
    CHECK(MobCategory_monster != NULL);                // the original still ran
    CHECK(logHas("injected backend"));
    CHECK(logHas("Found mod testmod"));
    CHECK(logHas("Found mod dllmod"));
    CHECK(logHas("Found mod examplemod"));
    CHECK(logHas("Found mod rubymod 1.0"));

    // ---- registrations through the injected backend ---------------------------
    CHECK_EQ(S.rejectedRegistrations, 10);
    CHECK_EQ(S.blockA, 146);
    CHECK_EQ(S.itemA, 4096);
    CHECK(S.findIdOk);
    CHECK_EQ(S.recipeInPreInit, 0);
    CHECK_EQ(S.registerInInit, -1);
    CHECK_EQ(S.shapedOk, 1);
    CHECK_EQ(S.shapedBadRows, 0);
    CHECK_EQ(S.shapelessOk, 1);
    CHECK_EQ(S.smeltingOk, 1);
    CHECK_EQ(S.smeltingUnknown, 0);

    Tile *blockA = S.blockA > 0 ? Tile::tiles[S.blockA] : NULL;
    CHECK(blockA != NULL);
    if (blockA != NULL) {
        CHECK(Item::items[S.blockA] != NULL);          // TileItem created
        CHECK(blockA->fake_initCalled);
        CHECK(blockA->fake_destroySpeed == 1.5f);
        CHECK(blockA->fake_explosionResistance == 30.0f);
        CHECK(blockA->fake_textureName == L"stone");   // built through the game's wstring
    }
    if (S.itemA > 0 && Item::items[S.itemA] != NULL) CHECK_EQ(Item::items[S.itemA]->fake_maxDamage, 100);

    int ruby = Registry::FindId(L"rubymod:ruby");
    int rubyBlock = Registry::FindId(L"rubymod:ruby_block");
    int rubyOre = Registry::FindId(L"rubymod:ruby_ore");
    CHECK(ruby >= 4096 && Item::items[ruby] != NULL);
    CHECK(rubyBlock > 0 && Tile::tiles[rubyBlock] != NULL);
    if (rubyBlock > 0 && Tile::tiles[rubyBlock] != NULL) {
        Tile *rb = Tile::tiles[rubyBlock];
        CHECK(rb->fake_material == Material::metal);
        CHECK(rb->fake_soundType == Tile::SOUND_METAL);
        CHECK(rb->fake_destroySpeed == 5.0f);
        CHECK_EQ(Tile::lightEmission[rubyBlock], 15);
        CHECK(rb->fake_textureName == L"blockEmerald");
        StringTable table;
        CHECK(wideEq(g_slotGetString(&table, (int)rb->getDescriptionId()), L"Block of Ruby"));
    }
    if (rubyOre > 0 && Tile::tiles[rubyOre] != NULL) {
        Random random;
        CHECK_EQ(Tile::tiles[rubyOre]->getResource(0, &random, 0), rubyOre); // drops itself here
        CHECK(logHas("custom drops aren't supported by the injected plugin yet"));
    }
    CHECK(logHas("block callbacks (onActivated) aren't supported"));   // ExampleMod's block
    CHECK(logHas("item callbacks (onUse) aren't supported"));          // ExampleMod's wand

    // 2 vanilla + 2 test mod + 2 ExampleMod + 1 ruby
    CHECK_EQ(Recipes::getInstance()->getRecipies()->size(), 7);
    CHECK_EQ(Recipes::getInstance()->fake_ingredientsArraySize, 7);  // crafting cache rebuilt
    CHECK(FurnaceRecipes::getInstance()->fake_results.count(rubyOre) == 1 &&
          FurnaceRecipes::getInstance()->fake_results[rubyOre]->id == ruby);
    CHECK(FurnaceRecipes::getInstance()->fake_results.count(S.blockA) == 1);

    // ---- names --------------------------------------------------------------
    StringTable table;
    CHECK(wideEq(g_slotGetString(&table, 12), L"vanilla string"));
    CHECK(wideEq(g_slotGetString(&table, STRING_ID_BASE + 99999), L""));

    // ---- the API after loading -----------------------------------------------
    Level server(false), client(true);
    FLCE_Level *serverL = reinterpret_cast<FLCE_Level *>(&server);
    CHECK_EQ(S.api->setBlock(serverL, 0, 64, 0, S.blockA, 0), 1);
    CHECK_EQ(S.api->getBlock(serverL, 0, 64, 0), S.blockA);
    CHECK_EQ(S.api->isClientSide(serverL), 0);
    CHECK_EQ(S.api->isClientSide(reinterpret_cast<FLCE_Level *>(&client)), 1);

    // ---- events through the hooks ---------------------------------------------
    int dummy = 0;
    g_slotClientTick(&dummy, true, false);
    g_slotClientTick(&dummy, false, false);           // second local player: no events
    CHECK(S.clientTickPhases == "SE");
    CHECK_EQ(g_clientTicks, 2);                        // the original ran both times

    for (int i = 0; i < 20 * 60; ++i) g_slotServerTick(&dummy);
    CHECK_EQ(S.serverTickEnds, 20 * 60);
    CHECK_EQ(g_serverTicks, 20 * 60);
    CHECK(logHas("Another minute of server time"));    // ExampleMod's handler

    ServerPlayerGameMode gameMode;
    gameMode.level = &server;
    gameMode.player = shared_ptr<Player>(new Player());
    CHECK(!g_slotDestroyBlock(&gameMode, 0, 64, 0));   // y == 64: canceled by the test mod
    CHECK_EQ(server.getTile(0, 64, 0), S.blockA);      // still there
    CHECK(S.blockBreakOrder == "HhNL");
    CHECK_EQ(S.lowestSawCanceled, 1);
    server.setTile(0, 10, 0, S.blockA);
    CHECK(g_slotDestroyBlock(&gameMode, 0, 10, 0));    // not canceled: the original breaks it
    CHECK_EQ(server.getTile(0, 10, 0), 0);

    EntityPtr below(new Entity());
    below->y = -5;
    CHECK(!g_slotAddEntity(&server, &below));          // canceled (y < 0)
    EntityPtr player(new Player());
    player->y = 70;
    CHECK(g_slotAddEntity(&server, &player));
    CHECK_EQ(server.fake_entities.size(), 1);
    CHECK_EQ(S.entityJoins, 2);
    CHECK_EQ(S.lastJoinIsPlayer, -1);                  // unknown in injected mode

    // ---- ids were saved --------------------------------------------------------
    std::string ids;
    CHECK(Platform::ReadFile(mods + L"forgedlce_ids.txt", ids));
    CHECK(ids.find("rubymod:ruby=") != std::string::npos);
    CHECK(ids.find("testmod:block_a=146") != std::string::npos);

    std::cout << std::endl << (g_checks - g_failures) << "/" << g_checks << " checks passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
