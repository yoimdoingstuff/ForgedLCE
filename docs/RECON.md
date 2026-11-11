# Recon: Legacy Console Edition source (`D:\MCLCESRC`)

Notes from the first pass over the LCE source. File references are relative to the LCE source root.

## Layout and build

- `MinecraftConsoles.sln` contains two projects:
  - `Minecraft.World`, a static library with the game logic (about 1,565 files, all in one folder).
  - `Minecraft.Client`, the application with rendering, UI, the server, and per-platform code.
- Platforms: Windows64 (the solution maps it to MSBuild platform `x64`), Durango, ORBIS, PS3, PSVita, Xbox 360.
- The Windows build uses toolset **v110 (Visual Studio 2012)**. Console builds use SNC or Clang.
- Windows entry point: `Minecraft.Client/Windows64/Windows64_Minecraft.cpp:602` (`_tWinMain`).
- Every `.cpp` starts with `#include "stdafx.h"` (precompiled header). On every platform it brings in `using namespace std`, `shared_ptr`, `CRITICAL_SECTION` (on consoles through 4J's `*Stubs.h`), and the global `app` (`CMinecraftApp`, which has `DebugPrintf`).
- Platform macros: `_WINDOWS64`, `_XBOX` (Xbox 360), `_DURANGO`, `__PS3__`, `__ORBIS__`, `__PSVITA__`.
- Many `Tile` setters are **protected** in LCE (`setDestroyTime`, `setExplodeable`, `setSoundType`, `setLightEmission`, `init`), and so is `Item::setMaxDamage`. Only subclasses can call them. `SoundType` is nested: `Tile::SoundType`.
- `D:\MCLCESRC` is a git repo with one commit. All files are staged but not committed.

## Which Java version this matches

The code is **not** a clean 1.6.4 or 1.7.x base:

- `SharedConstants::NETWORK_PROTOCOL_VERSION = 39`, which is Java **1.3.1/1.3.2**.
- The comments that say "1.7.3" and "1.8.2" refer to **Beta** 1.7.3 and Beta 1.8.2 (shears and melons are tagged with them in `Item.h:499`). The Xbox 360 port started from those versions.
- Some newer content was back-ported: anvil, carrots, potatoes, skulls, and flower pots (1.4), nether quartz (1.5), and carpet (1.6, `Tile.h:315`).
- Missing: wither, witch, bat, hoppers, comparators, horses, and hay bales.
- The texture atlas uses `IconRegister` and `TextureMap`/`Stitcher`, an API style from 1.5.

**Effect on the Forge target:** no Forge version matches the game code. The mod API copies the *design* of Forge 1.7.10: string registry names with automatically assigned numeric IDs, a preInit/init/postInit lifecycle, and an event bus. LCE's numeric-ID world fits that model.

## Registries

| What | Where | Notes |
|---|---|---|
| Blocks ("Tiles") | `Tile::tiles` (`Tile.h:152`), `TILE_NUM_COUNT = 4096` | Registered in the `Tile(int id, Material*, bool)` constructor. Per-ID property arrays `solid[]`, `lightBlock[]`, etc. sit at `Tile.h:154`. |
| Block items | `Tile.cpp:454` | A loop creates a `TileItem` for every tile that doesn't have one. It runs **inside** `Tile::staticCtor`, so tiles added later must create their own `TileItem`. |
| Items | `Item::items` (`ItemArray`), `ITEM_NUM_COUNT = 32000` | The `Item(int id)` constructor stores the item at `items[256 + id]` and logs `CONFLICT @` if the slot is taken. |
| Entities | `EntityIO::setId(createFn, eINSTANCEOF, name, id, ...)` | Uses an `eINSTANCEOF` enum, so new entity types need an enum strategy. Deferred. |
| Recipes | `Recipes::getInstance()->getRecipies()` | `addShapedRecipy` takes varargs with a type string, so a fixed C ABI can't forward to it. Recipes are built directly with `ShapedRecipy(w, h, ItemInstance**, result, group)`. |
| Console crafting UI cache | `Recipes::buildRecipeIngredientsArray` (`Recipes.cpp:1197`) | Private, and built only once in the constructor. Recipes added later need a public rebuild method (patched). |
| Smelting | `FurnaceRecipes::getInstance()->addFurnaceRecipy(id, ItemInstance*, xp)` | Public. |
| Strings | `CMinecraftApp::GetString(int)` (`Common/Consoles_App.cpp:302`) calls `StringTable::getString(int)` (`StringTable.cpp:150`) | Names are integer IDs into a string table, not text. Mod names need a reserved ID range. The patch goes in `StringTable.cpp`, which is pure ASCII; `Consoles_App.cpp` has a BOM and non-ASCII comments that break whitespace-tolerant patching. |

### Platform-specific paths

- **Startup:** every platform's entry point (`Windows64/`, `Xbox/`, `Durango/`, `Orbis/`, `PS3/`, `PSVita/` `*_Minecraft.cpp`) calls `Minecraft::main()`, which calls `MinecraftWorld_RunStaticCtors()`. So the loader hook runs everywhere.
- **Strings:** most platforms use `CMinecraftApp::GetString` and then `StringTable::getString`. The **Xbox 360 overrides it** (`Xbox/Xbox_App.cpp:3025`, `CConsoleMinecraftApp::GetString` returning `StringTable.Lookup(iID)` from its XUI string table). Patch 0008 hooks it.
- **PS3 chunk rendering** runs on the SPUs (`PS3/SPU_Tasks/ChunkUpdate`). `Tile_SPU::initTilePointers()` builds a fixed table of 148 SPU block classes, and `Tile_SPU::createFromID()` draws any other ID as stone (`m_tiles[1]`). The main CPU already fills `TileData_SPU` (icons, shapes, materials, light) for **every** existing block ID (`ChunkRebuildData.cpp:~230`), so patch 0009 gives unlisted IDs the generic `Tile_SPU`, the same class vanilla uses for plain blocks such as nether quartz ore. The other SPU tasks (`CompressedTile*`, `RecalcHeightmapOnly`, `LevelRenderer_*`) handle raw block bytes and don't care about block types.
- **Project files:** none of the patched files is excluded from any platform's build. (`Xbox/Xbox_App.cpp` is built for the Xbox 360 only, which is intended.)
- **Wii U / Switch / 3DS:** not in this source. The only mentions are in the Iggy and Miles middleware headers.

### The prebuilt libraries are missing

Every `.lib` under `Minecraft.Client/Windows64/` (4J_Input, 4J_Profile, 4J_Render, 4J_Storage, Iggy, Miles) is a **Git LFS pointer file** of 134 bytes, not the real library. For example, `4J_Render_PC.lib` should be 5,561,880 bytes. Whoever made this copy never downloaded the LFS objects, so the game **compiles but cannot link** until the real libraries are restored from wherever the copy came from. `iggy_w64.dll` and `mss64.dll` under `x64/` are real.

### The block-ID ceiling (important)

`TILE_NUM_COUNT` is 4096, but chunks store **one byte per block** (`LevelChunk::setBlockData` takes 32768 bytes; `CompressedTileStorage`). Block IDs that can be **placed in the world** are therefore limited to 0–255. Vanilla uses up to 171, which leaves roughly 80–90 free IDs for mods. Raising the limit would mean changing chunk storage, the save format, and the network packets. That is a later milestone.

## Init order

`Minecraft::main` (`Minecraft.cpp:4367`) calls `MinecraftWorld_RunStaticCtors()` (`Minecraft.World.cpp:48`), which runs:

`Packet` → `Material` → `Tile` (including the TileItem loop) → tool items → `Biome` → `Item` → `FurnaceRecipes` → `Recipes` → `Stats` → `TileEntity` → `EntityIO` → `MobCategory` → **[ForgedLCE hook goes here]** → `Item::staticInit` (builds item stats) → `LevelChunk` → world-gen features, ...

The comment there says the order matters. The hook runs after every vanilla registry exists and before item stats are built.

## Hook points

| Event | Function | Side |
|---|---|---|
| Client tick | `Minecraft::tick` (`Minecraft.cpp:2152`) | client |
| Server tick | `MinecraftServer::tick` (`MinecraftServer.cpp:1428`) | server (also runs on the host in single player) |
| Block break | `ServerPlayerGameMode::destroyBlock` (`ServerPlayerGameMode.cpp:229`) | server |
| Entity joins world | `Level::addEntity` (`Level.cpp:1659`). `ServerLevel::addEntity` overrides it. | both |
| Main loop | `Minecraft::run_middle` (`Minecraft.cpp:1226`) | client |

## Textures

Blocks get their icons from `registerIcons(IconRegister*)` using a texture name. The terrain atlas is a `PreStitchedTextureMap`, so in v0 mod blocks and items **reuse vanilla texture names**. Custom textures will need the stitcher to accept extra images. That is a later milestone.

## Things that can't be checked on this machine

- No Visual Studio is installed (no `vswhere`, no MSBuild), so nothing can be compiled here. The Windows build needs **VS2012 (v110)**, or a newer VS with the project retargeted.
- `git` is available only through GitHub Desktop's bundled copy.
