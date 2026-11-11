// Built-in mod used by the tests. It exercises every API call, including
// the ones that must fail, and records what happened in g_testModState.
// C++03, like a console mod.
#include "forgedlce/ForgedLCE.hpp"
#include "TestModState.h"

TestModState g_testModState;

namespace testmod {

TestModState &S = g_testModState;

FLCE_BlockDesc desc(const wchar_t *name)
{
    return flce::blockDesc(name, L"Test Block");
}

// ---- events ---------------------------------------------------------

void onBreakHighest(FLCE_BlockBreakEvent &, const FLCE_Api *) { S.blockBreakOrder += 'H'; }
void onBreakNormal (FLCE_BlockBreakEvent &, const FLCE_Api *) { S.blockBreakOrder += 'N'; }

// Cancels breaking anything at y == 64.
void onBreakHigh(FLCE_BlockBreakEvent &e, const FLCE_Api *)
{
    S.blockBreakOrder += 'h';
    if (e.y == 64) e.base.canceled = 1;
}

void onBreakLowest(FLCE_BlockBreakEvent &e, const FLCE_Api *)
{
    S.blockBreakOrder += 'L';
    if (e.base.canceled) S.lowestSawCanceled = 1;
}

void onServerTick(FLCE_TickEvent &e, const FLCE_Api *)
{
    if (e.phase == FLCE_PHASE_END) ++S.serverTickEnds;
}

void onClientTick(FLCE_TickEvent &e, const FLCE_Api *)
{
    S.clientTickPhases += e.phase == FLCE_PHASE_START ? 'S' : 'E';
}

// Cancels entities joining below y = 0.
void onJoin(FLCE_EntityJoinWorldEvent &e, const FLCE_Api *)
{
    ++S.entityJoins;
    S.lastJoinIsPlayer = e.isPlayer;
    if (e.y < 0) e.base.canceled = 1;
}

void FLCE_CALL dummyHandler(FLCE_Event *, void *) {}

// ---- lifecycle ------------------------------------------------------

void FLCE_CALL preInit(const FLCE_Api *api)
{
    S.api = api;

    // Registrations that must be rejected.
    const wchar_t *badNames[] = { L"noColon", L"a:b:c", L":x", L"x:", L"bad=name:x", L"" };
    for (int i = 0; i < 6; ++i) {
        FLCE_BlockDesc d = desc(badNames[i]);
        if (api->registerBlock(&d) == -1) ++S.rejectedRegistrations;
    }
    FLCE_BlockDesc small = desc(L"testmod:small");
    small.structSize = sizeof(small) - 1;
    if (api->registerBlock(&small) == -1) ++S.rejectedRegistrations;
    if (api->registerBlock(NULL) == -1) ++S.rejectedRegistrations;

    FLCE_BlockDesc a = desc(L"testmod:block_a");
    S.blockA = api->registerBlock(&a);
    if (api->registerBlock(&a) == -1) ++S.rejectedRegistrations; // duplicate

    FLCE_ItemDesc item = flce::itemDesc(L"testmod:item_a", L"Test Item");
    item.maxDamage = 100;
    S.itemA = api->registerItem(&item);
    if (api->registerItem(&item) == -1) ++S.rejectedRegistrations; // duplicate

    S.findIdOk = api->findId(L"testmod:block_a") == S.blockA && api->findId(L"testmod:missing") == -1 &&
                 api->findId(NULL) == -1;

    const wchar_t *rows[] = { L"S" };
    FLCE_RecipeKey keys[] = { { L'S', 1, -1 } };
    S.recipeInPreInit = api->addShapedRecipe(S.blockA, 1, 0, rows, 1, keys, 1);

    // Registered in this order on purpose; priorities decide call order.
    flce::on(api, FLCE_EVENT_BLOCK_BREAK, onBreakLowest, FLCE_PRIORITY_LOWEST);
    flce::on(api, FLCE_EVENT_BLOCK_BREAK, onBreakNormal, FLCE_PRIORITY_NORMAL);
    flce::on(api, FLCE_EVENT_BLOCK_BREAK, onBreakHighest, FLCE_PRIORITY_HIGHEST);
    flce::on(api, FLCE_EVENT_BLOCK_BREAK, onBreakHigh, FLCE_PRIORITY_HIGH);
    flce::on(api, FLCE_EVENT_SERVER_TICK, onServerTick);
    flce::on(api, FLCE_EVENT_CLIENT_TICK, onClientTick);
    flce::on(api, FLCE_EVENT_ENTITY_JOIN_WORLD, onJoin);

    S.badEventTypeSubscribe = api->subscribe(99, dummyHandler, FLCE_PRIORITY_NORMAL, NULL);
}

void FLCE_CALL init(const FLCE_Api *api)
{
    FLCE_BlockDesc late = desc(L"testmod:late");
    S.registerInInit = api->registerBlock(&late);

    // 2 stone side by side -> block_a
    const wchar_t *rows[] = { L"SS" };
    FLCE_RecipeKey keys[] = { { L'S', 1, -1 } };
    S.shapedOk = api->addShapedRecipe(S.blockA, 2, 0, rows, 1, keys, 1);

    const wchar_t *uneven[] = { L"SS", L"S" };
    S.shapedBadRows = api->addShapedRecipe(S.blockA, 1, 0, uneven, 2, keys, 1);

    const wchar_t *unknownSymbol[] = { L"SX" };
    S.shapedMissingKey = api->addShapedRecipe(S.blockA, 1, 0, unknownSymbol, 1, keys, 1);

    FLCE_RecipeKey badKey[] = { { L'S', 9999, -1 } };
    S.shapedUnknownId = api->addShapedRecipe(S.blockA, 1, 0, rows, 1, badKey, 1);

    int ingredients[] = { 1, 3 }; // stone + dirt
    S.shapelessOk = api->addShapelessRecipe(S.itemA, 1, 0, ingredients, 2);

    S.smeltingOk = api->addSmelting(S.blockA, 264, 1, 0, 0.5f);   // -> diamond
    S.smeltingUnknown = api->addSmelting(9999, 264, 1, 0, 0.5f);
}

void FLCE_CALL postInit(const FLCE_Api *api)
{
    S.exampleBlockSeenInPostInit = api->findId(L"examplemod:glowing_emerald_block");
}

} // namespace testmod

FLCE_MOD_ENTRY(unsigned int apiVersion, FLCE_ModInfo *info)
{
    if (apiVersion < FLCE_API_VERSION) return 0;
    info->modId    = L"testmod";
    info->name     = L"ForgedLCE test mod";
    info->version  = L"0";
    info->preInit  = testmod::preInit;
    info->init     = testmod::init;
    info->postInit = testmod::postInit;
    return 1;
}
