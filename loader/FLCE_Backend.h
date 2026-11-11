// ForgedLCE game backend: everything the loader core needs from the game.
//
// Two implementations:
//   loader/FLCE_GameSource.cpp    compiled into the game from source; uses the
//                                 game's C++ classes directly
//   inject/FLCE_GameInjected.cpp  a plugin injected into the compiled game;
//                                 calls the game through an address map
//
// The core validates everything first; backend calls get valid arguments.
#ifndef FLCE_BACKEND_H
#define FLCE_BACKEND_H

#include "ForgedLCE.h"

#include <string>

namespace ForgedLCE {
namespace Backend {

const wchar_t *Name(); // L"source" or L"injected"

// ---- ids ---------------------------------------------------------------
// True if a block or item occupies `id` (or it's out of range).
bool IdInUse(int id);
// True if `id` can be used in recipes: an existing block (< 256) or item.
bool IdExists(int id);

// ---- registration (preInit) ---------------------------------------------
// `texture` and `nameId` are already resolved. On failure return false and
// describe why in `error`; the id is then not recorded.
bool CreateBlock(int id, const FLCE_BlockDesc &desc, const std::wstring &texture,
                 unsigned int nameId, std::wstring &error);
bool CreateItem(int id, const FLCE_ItemDesc &desc, const std::wstring &texture,
                unsigned int nameId, std::wstring &error);

// ---- recipes (init and postInit) ------------------------------------------
// slotIds has width*height entries, 0 = empty slot; slotData -1 = any data.
bool AddShaped(int width, int height, const int *slotIds, const int *slotData,
               int resultId, int resultCount, int resultData);
bool AddShapeless(const int *ids, int count, int resultId, int resultCount, int resultData);
bool AddSmelting(int inputId, int resultId, int resultCount, int resultData, float xp);
// Called once after postInit (rebuilds the console crafting menu's cache).
void FinishRecipes();

// ---- world (after loading, from game threads) -----------------------------
int  GetTile(FLCE_Level *level, int x, int y, int z);
int  GetData(FLCE_Level *level, int x, int y, int z);
bool SetTile(FLCE_Level *level, int x, int y, int z, int id, int data);
bool IsClientSide(FLCE_Level *level);
void GetEntityPos(FLCE_Entity *entity, double *x, double *y, double *z);

} // namespace Backend
} // namespace ForgedLCE

#endif
