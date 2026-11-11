// ForgedLCE injected mode: binding the address map to callable functions,
// and installing the hooks.
//
// Game member functions are called as plain functions with `this` as the
// first argument. That matches the member-function calling convention on
// the platforms the plugin targets (PowerPC on Xbox 360, ARM on PS Vita) and
// on x86-64, where the tests run.
#ifndef FLCE_INJECTGAME_H
#define FLCE_INJECTGAME_H

#include "FLCE_Addresses.h"

#include <stddef.h>
#include <string>
#include <vector>

namespace ForgedLCE {
namespace Inject {

// Turns an address map value into a usable address: segment-relative
// values (PS Vita) need the module's segment base. Supplied by the platform.
typedef Addr (*ResolveFn)(int segment, Addr value);

struct GameBinding {
    // functions
    void *(*operatorNew)(size_t size);
    void *(*Tile_ctor)(void *self, int id, void *material, bool isSolidRender);
    void *(*Tile_setDestroyTime)(void *self, float value);
    void *(*Tile_setExplodeable)(void *self, float value);
    void *(*Tile_setSoundType)(void *self, const void *soundType);
    void *(*Tile_setLightEmission)(void *self, float value);
    void *(*Tile_setTextureName)(void *self, const void *wstring);
    void *(*Tile_setDescriptionId)(void *self, unsigned int id);
    void  (*Tile_init)(void *self);
    void *(*TileItem_ctor)(void *self, int id);
    void *(*Item_ctor)(void *self, int id);
    void *(*Item_setTextureName)(void *self, const void *wstring);
    void *(*Item_setMaxStackSize)(void *self, int max);
    void *(*Item_setMaxDamage)(void *self, int max);
    void *(*Item_handEquipped)(void *self);
    void *(*Item_setDescriptionId)(void *self, unsigned int id);
    void *(*ItemInstance_ctor)(void *self, int id, int count, int damage);
    void *(*ShapedRecipy_ctor)(void *self, int width, int height, void **items, void *result, int group);
    void *(*ShapelessRecipy_ctor)(void *self, void *result, void *ingredients, int group);
    void  (*RecipyPtrVector_push_back)(void *vector, void *const *value);
    void  (*ItemInstancePtrVector_push_back)(void *vector, void *const *value);
    void  (*Recipes_buildRecipeIngredientsArray)(void *self);
    void  (*FurnaceRecipes_addFurnaceRecipy)(void *self, int itemId, void *result, float xp);
    void *(*wstring_ctor)(void *self, const wchar_t *text);
    void  (*wstring_dtor)(void *self);
    int   (*Level_getTile)(void *self, int x, int y, int z);
    int   (*Level_getData)(void *self, int x, int y, int z);
    bool  (*Level_setTileAndData)(void *self, int x, int y, int z, int tile, int data);
    void  (*EntitySharedPtr_dtor)(void *sharedPtr);   // optional

    // addresses of static variables
    Addr tiles, items, recipesInstance, furnaceInstance, lateCheck;
    Addr materials[12];   // in FLCE_Material order
    Addr sounds[9];       // in FLCE_Sound order

    // sizes and offsets
    size_t sizeTile, sizeTileItem, sizeItem, sizeItemInstance, sizeShapedRecipy, sizeShapelessRecipy,
           sizeVector, sizeWstring;
    size_t offRecipesRecipies, offLevelIsClientSide, offEntityX, offGameModeLevel, offGameModePlayer,
           offSharedPtrRaw, offPlayerEntity;
    bool   hasGameModeOffsets, sharedPtrArgByPointer, calleeDestroysArgs;
    size_t gameWcharSize;   // 2 or 4

    // hook targets (0 = not provided)
    Addr hookStartup, hookGetString, hookClientTick, hookServerTick, hookDestroyBlock, hookAddEntity;

    Addr         checkAddress;
    unsigned int checkValue;
};

extern GameBinding g_game;

// Checks the map has every required key and sane sizes, and resolves it
// into g_game. Must succeed before anything else here.
bool BindGame(const AddressMap &map, ResolveFn resolve, std::wstring &error);

// Compares the word at check.address with check.value, so a map made for
// another game version is never used.
bool VerifyGameVersion(std::wstring &error);

// True if data.LateCheck shows the game already passed the startup hook
// (the plugin was loaded after the game started).
bool StartupAlreadyPassed();

// Installs a hook at `target`. On success, *original can be called to run
// the original function. Supplied by the platform (PowerPC detours on Xbox
// 360, taiHEN on PS Vita, a simulation in the tests).
typedef bool (*InstallHookFn)(const char *name, Addr target, void *replacement, void **original,
                              std::wstring &error);

// Installs every hook the map provides and logs which events are
// unavailable. Returns false if the startup hook couldn't be installed.
bool InstallHooks(InstallHookFn install);

// Converts `text` to the game's wide-character format (wcharSize 2 = UTF-16,
// 4 = UTF-32), NUL-terminated, into `out`. Returns a pointer into `out`.
const void *ToGameText(const std::wstring &text, size_t wcharSize, std::vector<unsigned char> &out);

// Called once mods have loaded: prepares mod names in the game's format
// (only needed when abi.wcharSize differs from the plugin's wchar_t).
void PrepareGameStrings();
// A mod name in the game's format, for the GetString hook. NULL if `id`
// isn't a mod string.
const void *GameModString(int id);

} // namespace Inject
} // namespace ForgedLCE

#endif
