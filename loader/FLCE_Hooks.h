// ForgedLCE hook entry points.
//
// This is the only ForgedLCE header the patched game files include. Keep it
// free of game includes so it can be pulled into any translation unit.
//
// Include it after "stdafx.h": it uses the game's `shared_ptr`, which comes
// from different places on different platforms.
#pragma once

class Level;
class Entity;
class Player;

namespace ForgedLCE {

// Minecraft.World.cpp, MinecraftWorld_RunStaticCtors(): after every vanilla
// registry exists, before Item::staticInit(). Loads mods and runs
// preInit/init/postInit.
void OnStaticCtors();

// Minecraft::tick (client) and MinecraftServer::tick (server).
// phase: 0 = start, 1 = end.
void OnClientTick(int phase);
void OnServerTick(int phase);

// ServerPlayerGameMode::destroyBlock. Returns true if a handler canceled it.
bool OnBlockBreak(Level *level, shared_ptr<Player> player, int x, int y, int z, int tileId, int data);

// Level::addEntity. Returns true if a handler canceled it.
bool OnEntityJoinWorld(Level *level, shared_ptr<Entity> entity);

// Fires a tick hook's start phase on construction and its end phase on
// destruction, so every return path of the ticked function is covered.
struct TickScope {
    typedef void (*Fn)(int phase);
    Fn   fn;
    bool active;
    TickScope(Fn f, bool isActive) : fn(f), active(isActive) { if (active) fn(0); }
    ~TickScope() { if (active) fn(1); }
private:
    TickScope(const TickScope &);
    TickScope &operator=(const TickScope &);
};

// StringTable::getString (behind CMinecraftApp::GetString). Returns NULL if
// the id is not a ForgedLCE string.
const wchar_t *GetModString(int id);

// String ids at or above this belong to ForgedLCE.
const int STRING_ID_BASE = 0x40000000;

} // namespace ForgedLCE
