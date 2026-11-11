/*
 * ForgedLCE mod API (C ABI).
 *
 * This is the only header a mod needs. Everything that crosses the
 * game/mod boundary is a plain C type, so a DLL mod can be built with any
 * compiler, regardless of the one used for the game (VS2012/v110).
 *
 * Mods that should run on consoles are compiled into the game with each
 * platform's compiler (VS2010-era for Xbox 360, SNC for PS3, Clang for
 * PS4/Vita). Keep mod code to C++03 plus `auto` if it targets consoles.
 *
 * Rules:
 *  - Strings passed to the API are copied; the mod keeps ownership.
 *  - FLCE_Level / FLCE_Entity pointers are only valid during the
 *    callback that received them. Do not store them.
 *  - Every struct starts with structSize; set it with sizeof().
 */
#ifndef FORGEDLCE_H
#define FORGEDLCE_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLCE_API_VERSION 1
#define FLCE_MOD_ENTRY_NAME "flce_mod_entry"

#if defined(_WIN32)
#  define FLCE_CALL __cdecl
#  define FLCE_EXPORT __declspec(dllexport)
#else
#  define FLCE_CALL
#  define FLCE_EXPORT
#endif

/* ---------------------------------------------------------------- */
/* Opaque game objects                                              */
/* ---------------------------------------------------------------- */

typedef struct FLCE_Level  FLCE_Level;   /* a Level* in the game          */
typedef struct FLCE_Entity FLCE_Entity;  /* an Entity* (or Player*)       */

/* ---------------------------------------------------------------- */
/* Logging                                                          */
/* ---------------------------------------------------------------- */

typedef enum FLCE_LogLevel {
    FLCE_LOG_DEBUG = 0,
    FLCE_LOG_INFO  = 1,
    FLCE_LOG_WARN  = 2,
    FLCE_LOG_ERROR = 3
} FLCE_LogLevel;

/* ---------------------------------------------------------------- */
/* Events (Forge: MinecraftForge.EVENT_BUS)                          */
/* ---------------------------------------------------------------- */

typedef enum FLCE_EventType {
    FLCE_EVENT_SERVER_TICK       = 0, /* FLCE_TickEvent               */
    FLCE_EVENT_CLIENT_TICK       = 1, /* FLCE_TickEvent               */
    FLCE_EVENT_BLOCK_BREAK       = 2, /* FLCE_BlockBreakEvent, cancelable */
    FLCE_EVENT_ENTITY_JOIN_WORLD = 3, /* FLCE_EntityJoinWorldEvent, cancelable */
    FLCE_EVENT_TYPE_COUNT
} FLCE_EventType;

typedef enum FLCE_EventPriority {
    FLCE_PRIORITY_HIGHEST = 0,
    FLCE_PRIORITY_HIGH    = 1,
    FLCE_PRIORITY_NORMAL  = 2,
    FLCE_PRIORITY_LOW     = 3,
    FLCE_PRIORITY_LOWEST  = 4
} FLCE_EventPriority;

typedef enum FLCE_TickPhase {
    FLCE_PHASE_START = 0,
    FLCE_PHASE_END   = 1
} FLCE_TickPhase;

/* Common header for every event. Check `type` before casting. */
typedef struct FLCE_Event {
    int type;        /* FLCE_EventType */
    int cancelable;  /* 1 if setting `canceled` has an effect */
    int canceled;    /* handlers set this to 1 to cancel */
} FLCE_Event;

typedef struct FLCE_TickEvent {
    FLCE_Event base;
    int phase;       /* FLCE_TickPhase */
} FLCE_TickEvent;

typedef struct FLCE_BlockBreakEvent {
    FLCE_Event   base;
    FLCE_Level  *level;
    FLCE_Entity *player;
    int x, y, z;
    int blockId;
    int blockData;
} FLCE_BlockBreakEvent;

typedef struct FLCE_EntityJoinWorldEvent {
    FLCE_Event   base;
    FLCE_Level  *level;
    FLCE_Entity *entity;
    int    isPlayer;
    double x, y, z;
} FLCE_EntityJoinWorldEvent;

typedef void (FLCE_CALL *FLCE_EventHandler)(FLCE_Event *event, void *userData);

/* ---------------------------------------------------------------- */
/* Blocks and items (Forge: GameRegistry)                           */
/* ---------------------------------------------------------------- */

typedef enum FLCE_Material {
    FLCE_MATERIAL_STONE = 0,
    FLCE_MATERIAL_WOOD,
    FLCE_MATERIAL_DIRT,
    FLCE_MATERIAL_GRASS,
    FLCE_MATERIAL_SAND,
    FLCE_MATERIAL_METAL,
    FLCE_MATERIAL_GLASS,
    FLCE_MATERIAL_CLOTH,
    FLCE_MATERIAL_PLANT,
    FLCE_MATERIAL_LEAVES,
    FLCE_MATERIAL_ICE,
    FLCE_MATERIAL_CLAY,
    FLCE_MATERIAL_COUNT
} FLCE_Material;

typedef enum FLCE_Sound {
    FLCE_SOUND_STONE = 0,
    FLCE_SOUND_WOOD,
    FLCE_SOUND_GRAVEL,
    FLCE_SOUND_GRASS,
    FLCE_SOUND_METAL,
    FLCE_SOUND_GLASS,
    FLCE_SOUND_CLOTH,
    FLCE_SOUND_SAND,
    FLCE_SOUND_SNOW,
    FLCE_SOUND_COUNT
} FLCE_Sound;

/* Return nonzero if the activation was handled (stops item use). */
typedef int (FLCE_CALL *FLCE_BlockActivatedFn)(FLCE_Level *level, int x, int y, int z,
                                               FLCE_Entity *player, int face, void *userData);

typedef struct FLCE_BlockDesc {
    size_t         structSize;
    const wchar_t *registryName;  /* "modid:name", required */
    const wchar_t *displayName;   /* shown in game */
    int            material;      /* FLCE_Material */
    int            sound;         /* FLCE_Sound */
    float          hardness;      /* vanilla stone = 1.5 */
    float          resistance;    /* explosion resistance, stone = 10 */
    float          lightEmission; /* 0.0 - 1.0 (glowstone = 1.0) */
    const wchar_t *textureName;   /* v0: must be a vanilla icon name, e.g. L"stone" */
    int            dropId;        /* 0 = drop this block */
    int            dropCount;     /* 0 = 1 */
    FLCE_BlockActivatedFn onActivated; /* optional */
    void          *userData;      /* passed to callbacks */
} FLCE_BlockDesc;

/* Return nonzero if the item was used. */
typedef int (FLCE_CALL *FLCE_ItemUseFn)(FLCE_Level *level, FLCE_Entity *player, void *userData);

typedef struct FLCE_ItemDesc {
    size_t         structSize;
    const wchar_t *registryName;  /* "modid:name", required */
    const wchar_t *displayName;
    int            maxStackSize;  /* 0 = default (64) */
    int            maxDamage;     /* 0 = not damageable */
    const wchar_t *textureName;   /* v0: must be a vanilla icon name */
    int            handEquipped;  /* 1 = held like a tool */
    FLCE_ItemUseFn onUse;         /* optional, right-click in air */
    void          *userData;
} FLCE_ItemDesc;

/* One key of a shaped recipe: symbol -> item/block id (+ data, -1 = any). */
typedef struct FLCE_RecipeKey {
    wchar_t symbol;
    int     id;
    int     data;
} FLCE_RecipeKey;

/* ---------------------------------------------------------------- */
/* The API table handed to mods                                     */
/* ---------------------------------------------------------------- */

typedef struct FLCE_Api {
    unsigned int apiVersion;
    size_t       structSize;

    void (FLCE_CALL *log)(const wchar_t *modId, int level, const wchar_t *message);

    /* Returns 1 on success. Only while mods are loading (preInit through
       postInit); the handler list is fixed once the game starts ticking. */
    int (FLCE_CALL *subscribe)(int eventType, FLCE_EventHandler handler, int priority, void *userData);

    /* preInit only. Return the assigned numeric id, or -1 on failure. */
    int (FLCE_CALL *registerBlock)(const FLCE_BlockDesc *desc);
    int (FLCE_CALL *registerItem)(const FLCE_ItemDesc *desc);

    /* Id of a mod-registered name, or -1. */
    int (FLCE_CALL *findId)(const wchar_t *registryName);

    /* init or later. `rows` are 1-3 strings of equal length (1-3 chars),
       spaces are empty slots. Return 1 on success. */
    int (FLCE_CALL *addShapedRecipe)(int resultId, int resultCount, int resultData,
                                     const wchar_t *const *rows, int rowCount,
                                     const FLCE_RecipeKey *keys, int keyCount);
    int (FLCE_CALL *addShapelessRecipe)(int resultId, int resultCount, int resultData,
                                        const int *ingredientIds, int ingredientCount);
    int (FLCE_CALL *addSmelting)(int inputId, int resultId, int resultCount, int resultData, float xp);

    /* World access (valid inside event / block callbacks). */
    int  (FLCE_CALL *getBlock)(FLCE_Level *level, int x, int y, int z);
    int  (FLCE_CALL *getBlockData)(FLCE_Level *level, int x, int y, int z);
    int  (FLCE_CALL *setBlock)(FLCE_Level *level, int x, int y, int z, int id, int data);
    int  (FLCE_CALL *isClientSide)(FLCE_Level *level);
    void (FLCE_CALL *getEntityPos)(FLCE_Entity *entity, double *x, double *y, double *z);
} FLCE_Api;

/* ---------------------------------------------------------------- */
/* Mod entry point                                                  */
/* ---------------------------------------------------------------- */

typedef void (FLCE_CALL *FLCE_LifecycleFn)(const FLCE_Api *api);

typedef struct FLCE_ModInfo {
    size_t         structSize;
    const wchar_t *modId;     /* required, lowercase, e.g. L"examplemod" */
    const wchar_t *name;
    const wchar_t *version;
    FLCE_LifecycleFn preInit;  /* register blocks, items, events */
    FLCE_LifecycleFn init;     /* recipes */
    FLCE_LifecycleFn postInit; /* cross-mod work */
} FLCE_ModInfo;

/*
 * Every mod defines its entry point with FLCE_MOD_ENTRY:
 *
 *   FLCE_MOD_ENTRY(unsigned int apiVersion, FLCE_ModInfo *info)
 *   {
 *       info->modId = L"examplemod";
 *       ...
 *       return 1;
 *   }
 *
 * Fill `info` and return 1 to load, 0 to decline (e.g. apiVersion too old).
 * Strings in `info` must stay valid for the life of the process
 * (string literals are fine).
 *
 * The same source builds two ways:
 *  - DLL mod (Windows64 only): exports `flce_mod_entry`, loaded from
 *    <exe dir>\mods\ at startup.
 *  - Static mod (every platform, the only option on consoles): compiled into
 *    the game by scripts/apply.ps1, which defines FLCE_STATIC_MOD_ID to the
 *    mod id so each mod's entry gets a unique name, flce_mod_entry_<modid>.
 */
typedef int (FLCE_CALL *FLCE_ModEntryFn)(unsigned int apiVersion, FLCE_ModInfo *info);

/*
 * Code mods as files, loaded by the injected plugins:
 *  - Xbox 360: a DLL-type XEX exporting flce_mod_entry as ordinal 1
 *    (see examples/ExampleMod/ExampleMod360.def)
 *  - PS Vita: a .suprx; ForgedLCE passes a pointer to an FLCE_VitaRegisterFn
 *    as module_start's argp, and the mod calls it with its entry point.
 *    Include forgedlce/ForgedLCE_VitaModule.h in one file of the mod.
 */
typedef void (FLCE_CALL *FLCE_VitaRegisterFn)(FLCE_ModEntryFn entry);

#ifdef __cplusplus
#  define FLCE_EXTERN_C extern "C"
#else
#  define FLCE_EXTERN_C
#endif

#define FLCE_CAT_(a, b) a##b
#define FLCE_CAT(a, b)  FLCE_CAT_(a, b)

#ifdef FLCE_STATIC_MOD_ID
#  define FLCE_MOD_ENTRY FLCE_EXTERN_C int FLCE_CALL FLCE_CAT(flce_mod_entry_, FLCE_STATIC_MOD_ID)
#else
#  define FLCE_MOD_ENTRY FLCE_EXTERN_C FLCE_EXPORT int FLCE_CALL flce_mod_entry
#endif

#ifdef __cplusplus
}
#endif

#endif /* FORGEDLCE_H */
