# ForgedLCE design

## Goal

A Forge-style mod loader for Legacy Console Edition, on every platform the LCE source builds for: **Windows64, Xbox 360, Xbox One (Durango), PS3, PS4 (Orbis), and PS Vita**.

Forge works by patching Java bytecode. LCE is native C++ with no JVM, so ForgedLCE reimplements Forge's *concepts* in native code, following the **Forge 1.7.10 API design**:

| Forge 1.7.10 | ForgedLCE |
|---|---|
| `@Mod` class plus `mcmod.info` | `FLCE_MOD_ENTRY(...)` fills in an `FLCE_ModInfo`, plus a `forgedlce.json` manifest |
| `FMLPreInitializationEvent` / `Init` / `PostInit` | `preInit` / `init` / `postInit` callbacks |
| `MinecraftForge.EVENT_BUS` with `@SubscribeEvent` | `api->subscribe(type, handler, priority, userData)` or `flce::on(api, type, fn)` |
| `event.setCanceled(true)` | `event->canceled = 1` |
| `GameRegistry.registerBlock(block, "name")` | `api->registerBlock(&desc)`, which returns the assigned numeric ID |
| `GameRegistry.addRecipe` / `addShapelessRecipe` / `addSmelting` | The same names on `FLCE_Api` |
| FML's ID map that ties names to numeric IDs | `mods/forgedlce_ids.txt` on Windows64. On consoles, IDs come from a fixed registration order. |

## Two ways to ship a mod

| | Built-in ("static") mod | DLL mod |
|---|---|---|
| Platforms | **All six** | Windows64 only |
| How | `scripts/apply.ps1 -StaticMods <folder>` compiles the mod into `Minecraft.World` | Drop `<mod>.dll` into `<exe dir>\mods\` |
| Compiler | Each platform's compiler (VS2010-era for Xbox 360, VS2012 for Windows64 and Durango, SNC for PS3, Clang for PS4 and Vita) | Any 64-bit Windows compiler |
| Needs a game rebuild | Yes | No |

Consoles can't load arbitrary code at runtime, so built-in mods are the only option there. The same mod source works both ways: `FLCE_MOD_ENTRY` expands to an exported `flce_mod_entry` for a DLL. In a built-in build, apply.ps1 defines `FLCE_STATIC_MOD_ID`, and the macro expands to a uniquely named `flce_mod_entry_<modid>`.

### How built-in mods get into the build

`Minecraft.World.vcxproj` is shared by all platforms. Patch 0001 adds a fixed set of files to it:

- `FLCE_StaticMod00.cpp` through `FLCE_StaticMod15.cpp`: 16 slots. apply.ps1 generates each one as either empty or:
  ```cpp
  #include "stdafx.h"
  #define FLCE_STATIC_MOD_ID examplemod
  #include "FLCE_Mods/examplemod/ExampleMod.cpp"
  ```
- `FLCE_StaticMods.cpp`: the generated table `g_staticMods[] = { { L"examplemod", flce_mod_entry_examplemod }, ... }`.

Each mod is its own translation unit, so mods can't clash with each other. The table references every entry point, so the linker keeps them even though `Minecraft.World` is a static library. Each mod folder gets its own copy of `forgedlce/*.h`, because GCC-style console compilers search only the including file's folder for `#include "..."`.

## Repo layout

ForgedLCE never contains LCE source. The game source is licensed, so this repo holds only our own code plus patches, the same way Forge ships patches instead of Minecraft code.

```
include/forgedlce/ForgedLCE.h     C ABI that mods compile against
include/forgedlce/ForgedLCE.hpp   C++03 helpers on top of it (header-only)
loader/                           Loader source, copied into Minecraft.World
patches/                          git patches against the LCE source (hooks and project file)
scripts/apply.ps1                 Install/refresh/uninstall into an LCE tree
examples/ExampleMod/              Sample mod (builds as a DLL or built in)
docs/                             RECON.md (findings) and this file
```

## Rules that keep it portable

- **The loader** is written in C++03 plus the subset the game itself uses (`auto` through `AUTO_VAR`, `shared_ptr` from the game's `stdafx.h`). Platform-specific code is limited to `#ifdef _WINDOWS64` blocks for DLL loading, `mods\` paths, and the log file. Everything else uses what the game already uses on every platform: `CRITICAL_SECTION`, which consoles get from 4J's stubs, and `app.DebugPrintf`.
- **The C ABI.** A DLL mod may come from a different compiler than the game, so only plain C structs, opaque pointers, numbers, and `const wchar_t*` cross the boundary. The loader copies any string it receives. Each struct starts with `structSize`, and the API carries `apiVersion`.
- **Mods never subclass `Tile` or `Item`.** They describe a block in `FLCE_BlockDesc`, and the loader creates an `FLCE_ModTile : Tile` that forwards to the mod's callbacks. This also matters because LCE makes most `Tile` setters `protected`.
- **Mod code meant for consoles** should stick to C++03. Put everything in a namespace named after the mod, because built-in mods compile alongside the game's own headers.

## Lifecycle

There is one hook in `MinecraftWorld_RunStaticCtors()`, after `MobCategory::staticCtor()` and before `Item::staticInit()`. It runs on every platform:

1. Load the ID map (Windows64 only).
2. **Discover.** Built-in mods come first, in their fixed order, then DLL mods (Windows64). Invalid or duplicate mod IDs are rejected.
3. **preInit** for every mod: register blocks, items, and event handlers.
4. **init**: recipes and smelting.
5. **postInit**: work that depends on other mods.
6. Save the ID map (Windows64) and rebuild the console crafting menu cache.

## Events (v0)

| Type | Fired from | Cancelable |
|---|---|---|
| `FLCE_EVENT_CLIENT_TICK` (start and end) | `Minecraft::tick`, first local player only (split-screen runs `tick` once per player) | no |
| `FLCE_EVENT_SERVER_TICK` (start and end) | `MinecraftServer::tick` | no |
| `FLCE_EVENT_BLOCK_BREAK` | `ServerPlayerGameMode::destroyBlock` | yes |
| `FLCE_EVENT_ENTITY_JOIN_WORLD` | `Level::addEntity` (client and server levels) | yes |

Handlers run in priority order (HIGHEST to LOWEST), then in registration order. Following Forge's rule, a handler still runs after an event is canceled, so it should check `event->canceled` itself. Subscribing is allowed only while mods load: the handler lists are then fixed, so the client and server threads can read them without locks.

## IDs

- **Blocks:** 1–255, because chunk storage holds one byte per block (see RECON.md). That leaves roughly 80–90 free IDs in total.
- **Items:** 4096–31999.
- **Windows64:** `mods\forgedlce_ids.txt` keeps each name on the same ID. Entries for mods that aren't installed are kept, so their IDs aren't given away.
- **Consoles:** there's no ID file. IDs follow the built-in mod order and the order each mod registers in, so they stay stable while those don't change. Reordering mods or registrations shifts IDs in existing worlds. The fix, a per-world ID map in the save, is the next milestone.
- **Strings:** mod display names get IDs from `0x40000000` up. The patched `StringTable::getString`, which `CMinecraftApp::GetString` calls, returns them on every platform.

## Not done yet (in order)

1. **Compiling and testing on each platform.** The development machine has no compiler, so everything so far has been checked by reading code. The console SDKs are needed for the console builds.
2. A per-world ID map in the save, plus a mod-list check when joining multiplayer games.
3. Custom textures (the stitched atlas must accept mod images on every platform).
4. Block IDs above 255 (chunk storage, save format, packets).
5. Entities (needs an `eINSTANCEOF` strategy), tile entities, GUIs.
