// ForgedLCE tests: boots the fake game with the loader, ExampleMod and the
// test mod built in (plus DLL mods on Windows64), then checks the results.
//
// Exit code 0 = all checks passed.
#include "stdafx.h"
#include "FakeGame.h"
#include "FLCE_Hooks.h"
#include "FLCE_Internal.h"
#include "TestModState.h"
#include "forgedlce/ForgedLCE.hpp"

#include <iostream>
#include <sstream>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "FAIL " << file << ":" << line << ": " << expr << std::endl;
    }
}

void checkEq(long long actual, long long expected, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::cout << "FAIL " << file << ":" << line << ": " << expr << " is " << actual
                  << ", expected " << expected << std::endl;
    }
}

#define CHECK(e)        check((e), #e, __FILE__, __LINE__)
#define CHECK_EQ(a, b)  checkEq((long long)(a), (long long)(b), #a " == " #b, __FILE__, __LINE__)

void FLCE_CALL noopHandler(FLCE_Event *, void *) {}

bool wideEq(LPCWSTR a, const wchar_t *b)
{
    return a != NULL && wcscmp(a, b) == 0;
}

// Everything the loader logged: the log file on Windows64,
// app.DebugPrintf output elsewhere (like the consoles).
std::string loaderLog()
{
#ifdef _WINDOWS64
    std::string text;
    FILE *f = _wfopen((ForgedLCE::Platform::ModsDir() + L"forgedlce.log").c_str(), L"rb");
    if (f != NULL) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        fclose(f);
    }
    return text;
#else
    return app.log;
#endif
}

bool logHas(const char *text)
{
    bool found = loaderLog().find(text) != std::string::npos;
    if (!found) std::cout << "  (log does not contain \"" << text << "\")" << std::endl;
    return found;
}

#ifdef _WINDOWS64
void writeModFile(const std::wstring &modsDir, const wchar_t *folder, const char *json)
{
    std::wstring dir = modsDir + folder + L"\\";
    CreateDirectoryW(dir.c_str(), NULL);
    FILE *f = _wfopen((dir + L"forgedlce.json").c_str(), L"wb");
    if (f != NULL) {
        fputs(json, f);
        fclose(f);
    }
}

std::string readIdFile()
{
    std::string text;
    FILE *f = _wfopen((ForgedLCE::Platform::ModsDir() + L"forgedlce_ids.txt").c_str(), L"rb");
    if (f != NULL) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        fclose(f);
    }
    return text;
}
#endif

ShapedRecipy *findShaped(int resultId)
{
    vector<Recipy *> *all = Recipes::getInstance()->getRecipies();
    for (size_t i = 0; i < all->size(); ++i) {
        ShapedRecipy *r = dynamic_cast<ShapedRecipy *>((*all)[i]);
        if (r != NULL && r->fake_result->id == resultId) return r;
    }
    return NULL;
}

} // namespace

int main()
{
    TestModState &S = g_testModState;

#ifdef _WINDOWS64
    // Pre-seed the id map: a missing mod holds 146, and the wand has a saved id.
    std::wstring modsDir = ForgedLCE::Platform::ModsDir();
    CreateDirectoryW(modsDir.c_str(), NULL);
    FILE *ids = _wfopen((modsDir + L"forgedlce_ids.txt").c_str(), L"wt");
    CHECK(ids != NULL);
    if (ids != NULL) {
        fputs("# test seed\nghost:block=146\nexamplemod:wand=5000\n", ids);
        fclose(ids);
    }
    const int EXAMPLE_BLOCK = 147, WAND = 5000, BLOCK_A = 148, ITEM_A = 4096, DLL_BLOCK = 149;
    const int DATA_MOD_RECIPES = 2;
    std::cout << "Platform path: Windows64 (DLL mods + data mods + id file)" << std::endl;

    // Data mods (forgedlce.json, no code).
    writeModFile(modsDir, L"rubymod",
        "{\n"
        "  \"modId\": \"rubymod\", \"name\": \"Ruby Mod\", \"version\": \"1.2\",\n"
        "  \"items\": [ { \"name\": \"ruby\", \"displayName\": \"Rub\\u00ed \xC3\xA9\", \"texture\": \"emerald\" } ],\n"
        "  \"blocks\": [\n"
        "    { \"name\": \"ruby_block\", \"displayName\": \"Block of Ruby\", \"material\": \"metal\", \"sound\": \"metal\",\n"
        "      \"hardness\": 5, \"resistance\": 10, \"light\": 15, \"texture\": \"blockEmerald\" },\n"
        "    { \"name\": \"ruby_ore\", \"displayName\": \"Ruby Ore\", \"hardness\": 3, \"drops\": \"ruby\", \"dropCount\": 2,\n"
        "      \"texture\": \"oreEmerald\" }\n"
        "  ],\n"
        "  \"recipes\": [\n"
        "    { \"type\": \"shaped\", \"result\": \"ruby_block\", \"pattern\": [\"RRR\", \"RRR\", \"RRR\"], \"key\": { \"R\": \"ruby\" } },\n"
        "    { \"type\": \"shapeless\", \"result\": \"ruby\", \"count\": 9, \"ingredients\": [\"ruby_block\"] },\n"
        "    { \"type\": \"shapeless\", \"result\": \"ruby\", \"ingredients\": [\"nope\"] }\n"
        "  ],\n"
        "  \"smelting\": [ { \"input\": \"ruby_ore\", \"result\": \"ruby\", \"xp\": 1.0 } ]\n"
        "}\n");
    writeModFile(modsDir, L"brokenmod",
        "{ \"modId\": \"brokenmod\",\n"
        "  \"items\": [ { \"name\": \"x\" } ]\n"
        "  \"blocks\": [] }\n");                           // missing comma on line 2
    writeModFile(modsDir, L"typomod",
        "{ \"modId\": \"typomod\", \"blocks\": [ { \"name\": \"b\", \"hardnes\": 2, \"material\": \"lava\" } ] }\n");
    writeModFile(modsDir, L"codeonly",                     // apply.ps1 manifest: not a data mod
        "{ \"modId\": \"codeonly\", \"source\": \"CodeOnly.cpp\" }\n");
#else
    const int EXAMPLE_BLOCK = 146, WAND = 4096, BLOCK_A = 147, ITEM_A = 4097;
    const int DATA_MOD_RECIPES = 0;
    std::cout << "Platform path: console (built-in mods only)" << std::endl;
#endif

    // ---- startup ----------------------------------------------------
    MinecraftWorld_RunStaticCtors();

    CHECK(ForgedLCE::CurrentPhase() == ForgedLCE::PHASE_DONE);
    CHECK(logHas("Found mod examplemod"));
    CHECK(logHas("Found mod testmod"));

    // ---- ExampleMod block -------------------------------------------
    int exampleBlock = ForgedLCE::Registry::FindId(L"examplemod:glowing_emerald_block");
    CHECK_EQ(exampleBlock, EXAMPLE_BLOCK);
    Tile *tile = exampleBlock > 0 ? Tile::tiles[exampleBlock] : NULL;
    CHECK(tile != NULL);
    if (tile != NULL) {
        CHECK(Item::items[exampleBlock] != NULL);                 // TileItem created
        CHECK(tile->fake_initCalled);
        CHECK(tile->fake_destroySpeed == 5.0f);
        CHECK(tile->fake_explosionResistance == 30.0f);           // setExplodeable(10) * 3
        CHECK(tile->fake_soundType == Tile::SOUND_METAL);
        CHECK(tile->fake_material == Material::metal);
        CHECK(tile->fake_solidRender);
        CHECK(tile->fake_textureName == L"blockEmerald");
        CHECK_EQ(Tile::lightEmission[exampleBlock], 15);
        CHECK(tile->getDescriptionId() >= (unsigned int)ForgedLCE::STRING_ID_BASE);
        CHECK(wideEq(StringTable().getString(tile->getDescriptionId()), L"Glowing Emerald Block"));
        CHECK(tile->TestUse());
        Random random;
        CHECK_EQ(tile->getResource(0, &random, 0), exampleBlock);
        CHECK_EQ(tile->getResourceCount(&random), 1);
    }

    // ---- ExampleMod wand --------------------------------------------
    int wandId = ForgedLCE::Registry::FindId(L"examplemod:wand");
    CHECK_EQ(wandId, WAND);
    Item *wand = wandId > 0 ? Item::items[wandId] : NULL;
    CHECK(wand != NULL);
    if (wand != NULL) {
        CHECK_EQ(wand->id, wandId);
        CHECK_EQ(wand->fake_maxStackSize, 1);
        CHECK(wand->fake_handEquipped);
        CHECK(wand->fake_textureName == L"stick");
        CHECK(wideEq(StringTable().getString(wand->fake_descriptionId), L"Emerald Wand"));
    }

    // ---- test mod: API rules ----------------------------------------
    CHECK(S.api != NULL);
    CHECK_EQ(S.rejectedRegistrations, 10);
    CHECK_EQ(S.blockA, BLOCK_A);
    CHECK_EQ(S.itemA, ITEM_A);
    if (S.itemA > 0 && Item::items[S.itemA] != NULL) CHECK_EQ(Item::items[S.itemA]->fake_maxDamage, 100);
    CHECK(S.findIdOk);
    CHECK_EQ(S.recipeInPreInit, 0);
    CHECK_EQ(S.badEventTypeSubscribe, 0);
    CHECK_EQ(S.registerInInit, -1);
    CHECK_EQ(S.shapedOk, 1);
    CHECK_EQ(S.shapedBadRows, 0);
    CHECK_EQ(S.shapedMissingKey, 0);
    CHECK_EQ(S.shapedUnknownId, 0);
    CHECK_EQ(S.shapelessOk, 1);
    CHECK_EQ(S.smeltingOk, 1);
    CHECK_EQ(S.smeltingUnknown, 0);
    CHECK_EQ(S.exampleBlockSeenInPostInit, exampleBlock);

    // ---- recipes ----------------------------------------------------
    // 2 vanilla + 2 ExampleMod + 2 test mod (+ 2 from the ruby data mod)
    CHECK_EQ(Recipes::getInstance()->getRecipies()->size(), 6 + DATA_MOD_RECIPES);
    CHECK_EQ(Recipes::getInstance()->fake_ingredientsArraySize, 6 + DATA_MOD_RECIPES); // crafting cache rebuilt
    ShapedRecipy *blockRecipe = findShaped(exampleBlock);
    CHECK(blockRecipe != NULL);
    if (blockRecipe != NULL) {
        CHECK_EQ(blockRecipe->fake_width, 3);
        CHECK_EQ(blockRecipe->fake_height, 3);
        bool allEmerald = true;
        for (int i = 0; i < 9; ++i) {
            ItemInstance *slot = blockRecipe->fake_items[i];
            if (slot == NULL || slot->id != 388 || slot->getAuxValue() != Recipes::ANY_AUX_VALUE) allEmerald = false;
        }
        CHECK(allEmerald);
    }
    ShapedRecipy *wandRecipe = findShaped(wandId);
    CHECK(wandRecipe != NULL);
    if (wandRecipe != NULL) {
        CHECK_EQ(wandRecipe->fake_width, 1);
        CHECK_EQ(wandRecipe->fake_height, 2);
        CHECK(wandRecipe->fake_items[0] != NULL && wandRecipe->fake_items[0]->id == 388);
        CHECK(wandRecipe->fake_items[1] != NULL && wandRecipe->fake_items[1]->id == 280);
    }
    CHECK(FurnaceRecipes::getInstance()->fake_results.count(S.blockA) == 1);

    // ---- the API after loading --------------------------------------
    CHECK_EQ(S.api->subscribe(FLCE_EVENT_SERVER_TICK, noopHandler, FLCE_PRIORITY_NORMAL, NULL), 0);
    FLCE_BlockDesc late = flce::blockDesc(L"testmod:too_late", L"x");
    CHECK_EQ(S.api->registerBlock(&late), -1);

    Level server(false);
    Level client(true);
    FLCE_Level *serverL = reinterpret_cast<FLCE_Level *>(&server);
    CHECK_EQ(S.api->setBlock(serverL, 0, 64, 0, S.blockA, 2), 1);
    CHECK_EQ(S.api->getBlock(serverL, 0, 64, 0), S.blockA);
    CHECK_EQ(S.api->getBlockData(serverL, 0, 64, 0), 2);
    CHECK_EQ(S.api->setBlock(serverL, 0, 65, 0, 250, 0), 0);   // unregistered id
    CHECK_EQ(S.api->setBlock(serverL, 0, 65, 0, 300, 0), 0);   // above the 255 limit
    CHECK_EQ(S.api->isClientSide(serverL), 0);
    CHECK_EQ(S.api->isClientSide(reinterpret_cast<FLCE_Level *>(&client)), 1);

    shared_ptr<Player> player(new Player());
    player->x = 3.5; player->y = 70; player->z = -2.5;
    double px = 0, py = 0, pz = 0;
    S.api->getEntityPos(reinterpret_cast<FLCE_Entity *>(static_cast<Entity *>(player.get())), &px, &py, &pz);
    CHECK(px == 3.5 && py == 70 && pz == -2.5);

    // ---- block activation and item use ------------------------------
    if (tile != NULL) {
        CHECK(tile->use(&server, 1, 64, 1, player, 0, 0, 0, 0));
        CHECK(!tile->use(&server, 1, 64, 1, player, 0, 0, 0, 0, true)); // soundOnly
        CHECK(tile->TestUse(&server, 1, 64, 1, player));
        CHECK(logHas("Glowing emerald block used at 1,64,1"));
    }
    if (wand != NULL) {
        shared_ptr<ItemInstance> held(new ItemInstance(wandId, 1, 0));
        CHECK(wand->use(held, &server, player).get() == held.get());
        CHECK(wand->TestUse(&server, player));
        CHECK(logHas("Wand used at 3,70,-2"));
    }

    // ---- events -----------------------------------------------------
    // y == 64 is canceled by the test mod's HIGH handler.
    CHECK(ForgedLCE::OnBlockBreak(&server, player, 0, 64, 0, S.blockA, 0));
    CHECK(S.blockBreakOrder == "HhNL");
    CHECK_EQ(S.lowestSawCanceled, 1);
    CHECK(!ForgedLCE::OnBlockBreak(&server, player, 0, 10, 0, exampleBlock, 0));
    CHECK(logHas("Someone broke a glowing emerald block"));

    shared_ptr<Entity> below(new Entity());
    below->y = -5;
    CHECK(!server.addEntity(below));                  // canceled
    CHECK(server.addEntity(player));
    CHECK_EQ(server.fake_entities.size(), 1);
    CHECK_EQ(S.entityJoins, 2);
    CHECK_EQ(S.lastJoinIsPlayer, 1);

    { ForgedLCE::TickScope t(ForgedLCE::OnClientTick, true); }
    { ForgedLCE::TickScope t(ForgedLCE::OnClientTick, false); } // second local player: no event
    CHECK(S.clientTickPhases == "SE");

    for (int i = 0; i < 20 * 60; ++i) {
        ForgedLCE::TickScope t(ForgedLCE::OnServerTick, true);
    }
    CHECK_EQ(S.serverTickEnds, 20 * 60);
    CHECK(logHas("Another minute of server time"));

    // ---- strings ----------------------------------------------------
    CHECK(wideEq(StringTable().getString(ForgedLCE::STRING_ID_BASE + 100000), L""));
    CHECK(wideEq(StringTable().getString(12), L"vanilla string"));

#ifdef _WINDOWS64
    // ---- Windows64 only: id file and DLL mods -----------------------
    std::string idFile = readIdFile();
    std::ostringstream expectBlock;
    expectBlock << "examplemod:glowing_emerald_block=" << EXAMPLE_BLOCK;
    CHECK(idFile.find(expectBlock.str()) != std::string::npos);
    CHECK(idFile.find("ghost:block=146") != std::string::npos);   // kept for the absent mod
    CHECK(idFile.find("examplemod:wand=5000") != std::string::npos);

    CHECK(logHas("Found mod dllmod"));
    CHECK_EQ(ForgedLCE::Registry::FindId(L"dllmod:dll_block"), DLL_BLOCK);
    CHECK(logHas("duplicate modId examplemod"));                  // ExampleMod.dll vs. built-in

    // ---- Windows64 only: data mods ----------------------------------
    CHECK(logHas("Found mod rubymod 1.2"));
    int ruby = ForgedLCE::Registry::FindId(L"rubymod:ruby");
    int rubyBlock = ForgedLCE::Registry::FindId(L"rubymod:ruby_block");
    int rubyOre = ForgedLCE::Registry::FindId(L"rubymod:ruby_ore");
    CHECK(ruby >= 4096 && Item::items[ruby] != NULL);
    CHECK(rubyBlock > 0 && rubyBlock < 256 && Tile::tiles[rubyBlock] != NULL);
    CHECK(rubyOre > 0 && rubyOre < 256 && Tile::tiles[rubyOre] != NULL);
    if (ruby > 0 && Item::items[ruby] != NULL) {
        // "Rubí \xC3\xA9" in the file: one \u escape and one raw UTF-8 character.
        CHECK(wideEq(StringTable().getString(Item::items[ruby]->fake_descriptionId), L"Rub\x00ED \x00E9"));
        CHECK(Item::items[ruby]->fake_textureName == L"emerald");
    }
    if (rubyBlock > 0 && Tile::tiles[rubyBlock] != NULL) {
        Tile *rb = Tile::tiles[rubyBlock];
        CHECK(rb->fake_destroySpeed == 5.0f);
        CHECK(rb->fake_material == Material::metal);
        CHECK(rb->fake_soundType == Tile::SOUND_METAL);
        CHECK_EQ(Tile::lightEmission[rubyBlock], 15);
        CHECK(wideEq(StringTable().getString(rb->getDescriptionId()), L"Block of Ruby"));
        CHECK(Item::items[rubyBlock] != NULL);                     // TileItem
    }
    if (rubyOre > 0 && Tile::tiles[rubyOre] != NULL) {
        Random random;
        CHECK_EQ(Tile::tiles[rubyOre]->getResource(0, &random, 0), ruby);  // drops the ruby item
        CHECK_EQ(Tile::tiles[rubyOre]->getResourceCount(&random), 2);
        CHECK(Tile::tiles[rubyOre]->fake_material == Material::stone);    // default
    }
    ShapedRecipy *rubyBlockRecipe = findShaped(rubyBlock);
    CHECK(rubyBlockRecipe != NULL && rubyBlockRecipe->fake_width == 3 && rubyBlockRecipe->fake_items[4] != NULL &&
          rubyBlockRecipe->fake_items[4]->id == ruby);
    CHECK(FurnaceRecipes::getInstance()->fake_results.count(rubyOre) == 1 &&
          FurnaceRecipes::getInstance()->fake_results[rubyOre]->id == ruby);
    CHECK(logHas("nothing is registered as \"rubymod:nope\""));     // bad reference: recipe skipped

    CHECK(logHas("brokenmod"));
    CHECK(logHas("line 3: expected ',' or '}'"));                 // syntax error reported with its line
    CHECK(ForgedLCE::Registry::FindId(L"brokenmod:x") == -1);

    CHECK(logHas("unknown key \"hardnes\""));                     // typo warning
    CHECK(logHas("unknown material, using stone"));
    CHECK(ForgedLCE::Registry::FindId(L"typomod:b") > 0);         // still registered

    CHECK(loaderLog().find("Found mod codeonly") == std::string::npos); // apply.ps1 manifest ignored
#endif

    std::cout << std::endl << "Loader log:" << std::endl << loaderLog() << std::endl;
    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
