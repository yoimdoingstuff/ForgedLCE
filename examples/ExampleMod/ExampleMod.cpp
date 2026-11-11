// ExampleMod: the ForgedLCE equivalent of a first Forge 1.7.10 mod.
//
// Adds a glowing emerald block and a wand, gives each a recipe, and listens
// for block breaks and server ticks.
//
// Builds as a DLL (Windows64) or as a static mod compiled into the game
// (every platform, including consoles). Written in C++03 so the console
// compilers accept it. Keep everything inside a namespace named after the
// mod: static mods are compiled together with the game's own code.
#include "forgedlce/ForgedLCE.hpp"

namespace examplemod {

const wchar_t *const MODID = L"examplemod";

// Vanilla ids used in recipes (Item.h in the LCE source).
const int EMERALD = 388;
const int STICK   = 280;

int g_blockId = -1;
int g_wandId  = -1;
int g_ticks   = 0;

const FLCE_Api *g_api = NULL;

int FLCE_CALL onBlockActivated(FLCE_Level *level, int x, int y, int z, FLCE_Entity *, int, void *)
{
    if (g_api->isClientSide(level)) return 1; // the server does the work
    std::wstring msg = L"Glowing emerald block used at " + flce::str(x) + L"," + flce::str(y) + L"," + flce::str(z);
    g_api->log(MODID, FLCE_LOG_INFO, msg.c_str());
    return 1;
}

int FLCE_CALL onWandUsed(FLCE_Level *level, FLCE_Entity *player, void *)
{
    if (g_api->isClientSide(level)) return 1;
    double x, y, z;
    g_api->getEntityPos(player, &x, &y, &z);
    std::wstring msg = L"Wand used at " + flce::str((int)x) + L"," + flce::str((int)y) + L"," + flce::str((int)z);
    g_api->log(MODID, FLCE_LOG_INFO, msg.c_str());
    return 1;
}

// Like @SubscribeEvent on BlockEvent.BreakEvent.
void onBlockBreak(FLCE_BlockBreakEvent &e, const FLCE_Api *api)
{
    if (e.blockId == g_blockId) {
        api->log(MODID, FLCE_LOG_INFO, L"Someone broke a glowing emerald block");
    }
}

// Like TickEvent.ServerTickEvent.
void onServerTick(FLCE_TickEvent &e, const FLCE_Api *api)
{
    if (e.phase == FLCE_PHASE_END && ++g_ticks % (20 * 60) == 0) {
        api->log(MODID, FLCE_LOG_INFO, L"Another minute of server time");
    }
}

void FLCE_CALL preInit(const FLCE_Api *api)
{
    g_api = api;

    FLCE_BlockDesc block = flce::blockDesc(L"examplemod:glowing_emerald_block", L"Glowing Emerald Block");
    block.material      = FLCE_MATERIAL_METAL;
    block.sound         = FLCE_SOUND_METAL;
    block.hardness      = 5.0f;
    block.resistance    = 10.0f;
    block.lightEmission = 1.0f;
    block.textureName   = L"blockEmerald"; // v0: vanilla textures only
    block.onActivated   = onBlockActivated;
    g_blockId = api->registerBlock(&block);

    FLCE_ItemDesc wand = flce::itemDesc(L"examplemod:wand", L"Emerald Wand");
    wand.maxStackSize = 1;
    wand.handEquipped = 1;
    wand.textureName  = L"stick";
    wand.onUse        = onWandUsed;
    g_wandId = api->registerItem(&wand);

    flce::on(api, FLCE_EVENT_BLOCK_BREAK, onBlockBreak);
    flce::on(api, FLCE_EVENT_SERVER_TICK, onServerTick);
}

void FLCE_CALL init(const FLCE_Api *api)
{
    if (g_blockId > 0) {
        const wchar_t *rows[] = { L"EEE", L"EEE", L"EEE" };
        FLCE_RecipeKey keys[] = { { L'E', EMERALD, -1 } };
        api->addShapedRecipe(g_blockId, 1, 0, rows, 3, keys, 1);
    }
    if (g_wandId > 0) {
        const wchar_t *rows[] = { L"E", L"S" };
        FLCE_RecipeKey keys[] = { { L'E', EMERALD, -1 }, { L'S', STICK, -1 } };
        api->addShapedRecipe(g_wandId, 1, 0, rows, 2, keys, 2);
    }
}

} // namespace examplemod

FLCE_MOD_ENTRY(unsigned int apiVersion, FLCE_ModInfo *info)
{
    if (apiVersion < FLCE_API_VERSION) return 0;
    info->modId    = examplemod::MODID;
    info->name     = L"Example Mod";
    info->version  = L"1.0.0";
    info->preInit  = examplemod::preInit;
    info->init     = examplemod::init;
    info->postInit = NULL;
    return 1;
}
