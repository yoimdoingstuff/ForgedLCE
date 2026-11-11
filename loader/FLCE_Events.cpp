// ForgedLCE event bus and the game-side event hooks.
#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.entity.h"
#include "net.minecraft.world.entity.player.h"

#include "FLCE_Internal.h"

namespace ForgedLCE {
namespace Events {

namespace {

struct Handler {
    FLCE_EventHandler fn;
    int               priority;
    void             *userData;
};

// One list per event type, kept sorted by priority (stable: equal
// priorities keep registration order). Lists are only written during mod
// loading, before any game thread fires events, so reads need no lock.
std::vector<Handler> g_handlers[FLCE_EVENT_TYPE_COUNT];

} // namespace

bool Subscribe(int type, FLCE_EventHandler handler, int priority, void *userData)
{
    if (type < 0 || type >= FLCE_EVENT_TYPE_COUNT || handler == NULL) return false;
    if (CurrentPhase() == PHASE_DONE || CurrentPhase() == PHASE_NOT_STARTED) {
        // Subscribing after load would race with the server thread.
        Log(FLCE_LOG_WARN, L"ForgedLCE", L"subscribe() is only allowed while mods are loading");
        return false;
    }
    if (priority < FLCE_PRIORITY_HIGHEST) priority = FLCE_PRIORITY_HIGHEST;
    if (priority > FLCE_PRIORITY_LOWEST)  priority = FLCE_PRIORITY_LOWEST;

    Handler h;
    h.fn = handler;
    h.priority = priority;
    h.userData = userData;

    std::vector<Handler> &list = g_handlers[type];
    std::vector<Handler>::iterator it = list.begin();
    while (it != list.end() && it->priority <= priority) ++it;
    list.insert(it, h);
    return true;
}

bool Post(FLCE_Event *event)
{
    const std::vector<Handler> &list = g_handlers[event->type];
    for (size_t i = 0; i < list.size(); ++i) {
        // Forge semantics: handlers still run after cancellation and are
        // expected to check event->canceled themselves.
        list[i].fn(event, list[i].userData);
    }
    if (!event->cancelable) event->canceled = 0;
    return event->canceled != 0;
}

} // namespace Events

// ---------------------------------------------------------------------
// Hooks called from patched game code
// ---------------------------------------------------------------------

static void PostTick(int type, int phase)
{
    if (CurrentPhase() != PHASE_DONE) return;
    FLCE_TickEvent e;
    e.base.type = type;
    e.base.cancelable = 0;
    e.base.canceled = 0;
    e.phase = phase;
    Events::Post(&e.base);
}

void OnClientTick(int phase) { PostTick(FLCE_EVENT_CLIENT_TICK, phase); }
void OnServerTick(int phase) { PostTick(FLCE_EVENT_SERVER_TICK, phase); }

bool OnBlockBreak(Level *level, shared_ptr<Player> player, int x, int y, int z, int tileId, int data)
{
    if (CurrentPhase() != PHASE_DONE) return false;
    FLCE_BlockBreakEvent e;
    e.base.type = FLCE_EVENT_BLOCK_BREAK;
    e.base.cancelable = 1;
    e.base.canceled = 0;
    e.level = reinterpret_cast<FLCE_Level *>(level);
    e.player = reinterpret_cast<FLCE_Entity *>(static_cast<Entity *>(player.get()));
    e.x = x; e.y = y; e.z = z;
    e.blockId = tileId;
    e.blockData = data;
    return Events::Post(&e.base);
}

bool OnEntityJoinWorld(Level *level, shared_ptr<Entity> entity)
{
    // .get(): comparing a shared_ptr with NULL doesn't compile with every
    // platform's shared_ptr (e.g. C++03 tr1).
    if (CurrentPhase() != PHASE_DONE || entity.get() == NULL) return false;
    FLCE_EntityJoinWorldEvent e;
    e.base.type = FLCE_EVENT_ENTITY_JOIN_WORLD;
    e.base.cancelable = 1;
    e.base.canceled = 0;
    e.level = reinterpret_cast<FLCE_Level *>(level);
    e.entity = reinterpret_cast<FLCE_Entity *>(entity.get());
    e.isPlayer = dynamic_pointer_cast<Player>(entity).get() != NULL ? 1 : 0;
    e.x = entity->x;
    e.y = entity->y;
    e.z = entity->z;
    return Events::Post(&e.base);
}

} // namespace ForgedLCE
