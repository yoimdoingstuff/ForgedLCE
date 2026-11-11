// ForgedLCE injected mode: the hook functions that stand in for the source
// patches. Each runs ForgedLCE's code, then calls the original game function.
//
//   startup  MobCategory::staticCtor                  = patch 0002
//   names    StringTable::getString / Xbox GetString  = patches 0007, 0008
//   ticks    Minecraft::tick, MinecraftServer::tick    = patch 0006
//   break    ServerPlayerGameMode::destroyBlock        = patch 0005
//   join     Level::addEntity                          = patch 0004
// (Patch 0003's crafting-cache rebuild is a direct call in FinishRecipes.
//  Patch 0009, the PS3 SPU renderer, has no injected equivalent: PS3 isn't
//  a plugin target.)
#include "stdafx.h"
#include "FLCE_Internal.h"
#include "FLCE_InjectGame.h"

namespace ForgedLCE {
namespace Inject {

namespace {

// Pointers to the original functions, filled by the hook installer.
void         (*g_origStartup)();
const wchar_t *(*g_origGetString)(void *self, int id);
void         (*g_origClientTick)(void *self, bool first, bool updateTextures);
void         (*g_origServerTick)(void *self);
bool         (*g_origDestroyBlock)(void *self, int x, int y, int z);
bool         (*g_origAddEntity)(void *self, void *sharedPtrArg);

void Hook_Startup()
{
    g_origStartup();
    LoadAllMods();
    PrepareGameStrings();
}

// An empty string in either character width.
const unsigned int EMPTY_GAME_STRING[1] = { 0 };

const wchar_t *Hook_GetString(void *self, int id)
{
    if (id >= STRING_ID_BASE) {
        // In the game's character width, which may differ from ours.
        const void *text = GameModString(id);
        return static_cast<const wchar_t *>(text != NULL ? text : EMPTY_GAME_STRING);
    }
    return g_origGetString(self, id);
}

// tick() runs once per local player; like patch 0006, only the first one
// produces events.
void Hook_ClientTick(void *self, bool first, bool updateTextures)
{
    if (first) Events::PostTick(FLCE_EVENT_CLIENT_TICK, FLCE_PHASE_START);
    g_origClientTick(self, first, updateTextures);
    if (first) Events::PostTick(FLCE_EVENT_CLIENT_TICK, FLCE_PHASE_END);
}

void Hook_ServerTick(void *self)
{
    Events::PostTick(FLCE_EVENT_SERVER_TICK, FLCE_PHASE_START);
    g_origServerTick(self);
    Events::PostTick(FLCE_EVENT_SERVER_TICK, FLCE_PHASE_END);
}

bool Hook_DestroyBlock(void *self, int x, int y, int z)
{
    if (CurrentPhase() == PHASE_DONE) {
        char *gameMode = static_cast<char *>(self);
        void *level = *reinterpret_cast<void **>(gameMode + g_game.offGameModeLevel);
        char *player = *reinterpret_cast<char **>(gameMode + g_game.offGameModePlayer + g_game.offSharedPtrRaw);
        if (level != NULL) {
            FLCE_Level *l = static_cast<FLCE_Level *>(level);
            FLCE_Entity *p = player != NULL ? reinterpret_cast<FLCE_Entity *>(player + g_game.offPlayerEntity) : NULL;
            if (Events::PostBlockBreak(l, p, x, y, z, Backend::GetTile(l, x, y, z), Backend::GetData(l, x, y, z))) {
                // Canceled. The game's network code re-sends the block, as
                // with patch 0005.
                return false;
            }
        }
    }
    return g_origDestroyBlock(self, x, y, z);
}

bool Hook_AddEntity(void *self, void *sharedPtrArg)
{
    void *entity = sharedPtrArg != NULL
                 ? *reinterpret_cast<void **>(static_cast<char *>(sharedPtrArg) + g_game.offSharedPtrRaw) : NULL;
    if (entity != NULL) {
        FLCE_Entity *e = static_cast<FLCE_Entity *>(entity);
        double x, y, z;
        Backend::GetEntityPos(e, &x, &y, &z);
        // isPlayer = -1: telling players apart needs RTTI this mode can't use.
        if (Events::PostEntityJoin(static_cast<FLCE_Level *>(self), e, -1, x, y, z)) {
            // The original isn't called, so release the argument ourselves
            // where the callee is responsible for that (MSVC).
            if (g_game.calleeDestroysArgs && g_game.EntitySharedPtr_dtor != NULL) {
                g_game.EntitySharedPtr_dtor(sharedPtrArg);
            }
            return false;
        }
    }
    return g_origAddEntity(self, sharedPtrArg);
}

bool Install(InstallHookFn install, const char *name, Addr target, void *replacement, void **original)
{
    if (target == 0) return false;
    std::wstring error;
    if (!install(name, target, replacement, original, error)) {
        std::wstring wname;
        for (const char *p = name; *p; ++p) wname += (wchar_t)*p;
        Log(FLCE_LOG_ERROR, L"ForgedLCE", L"Could not hook " + wname + L": " + error);
        return false;
    }
    return true;
}

template <typename Fn>
void *AsVoid(Fn fn) { return reinterpret_cast<void *>(fn); }

template <typename Fn>
void **OrigSlot(Fn &slot) { return reinterpret_cast<void **>(&slot); }

} // namespace

bool InstallHooks(InstallHookFn install)
{
    if (!Install(install, "hook.MobCategory_staticCtor", g_game.hookStartup, AsVoid(&Hook_Startup), OrigSlot(g_origStartup))) {
        return false;
    }

    if (!Install(install, "hook.GetString", g_game.hookGetString, AsVoid(&Hook_GetString), OrigSlot(g_origGetString))) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", L"No hook.GetString: mod blocks and items will have blank names");
    }
    if (!Install(install, "hook.Minecraft_tick", g_game.hookClientTick, AsVoid(&Hook_ClientTick), OrigSlot(g_origClientTick))) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", L"Client tick events unavailable (no hook.Minecraft_tick)");
    }
    if (!Install(install, "hook.MinecraftServer_tick", g_game.hookServerTick, AsVoid(&Hook_ServerTick), OrigSlot(g_origServerTick))) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", L"Server tick events unavailable (no hook.MinecraftServer_tick)");
    }
    if (!g_game.hasGameModeOffsets ||
        !Install(install, "hook.ServerPlayerGameMode_destroyBlock", g_game.hookDestroyBlock,
                 AsVoid(&Hook_DestroyBlock), OrigSlot(g_origDestroyBlock))) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", L"Block break events unavailable (needs hook.ServerPlayerGameMode_destroyBlock "
                                         L"and its two offsets)");
    }
    if (!g_game.sharedPtrArgByPointer ||
        !Install(install, "hook.Level_addEntity", g_game.hookAddEntity, AsVoid(&Hook_AddEntity), OrigSlot(g_origAddEntity))) {
        Log(FLCE_LOG_WARN, L"ForgedLCE", L"Entity join events unavailable (needs hook.Level_addEntity and "
                                         L"abi.sharedPtrArgByPointer = 1)");
    }
    return true;
}

} // namespace Inject
} // namespace ForgedLCE
