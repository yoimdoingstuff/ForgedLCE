# ForgedLCE

A port of the Forge mod loader to Minecraft: Legacy Console Edition, for every platform the LCE source builds for: Windows64, Xbox 360, Xbox One, PS3, PS4, and PS Vita.

The mod API follows **Forge 1.7.10**: a preInit/init/postInit lifecycle, an event bus, and a registry that maps names to IDs. The LCE code doesn't match any single Forge version (its core is Java 1.3.2 with some later content back-ported; see [docs/RECON.md](docs/RECON.md)), so ForgedLCE copies Forge's design rather than its exact API.

This repo contains **no game source**, only the loader, patches, plugins and tools.

## Two ways to run it

| | **Built into the game** (source build) | **Plugin** (no compiling to install) |
|---|---|---|
| How | `scripts/apply.ps1` adds ForgedLCE to the LCE source, and you rebuild the game | A plugin hooks your existing copy of the game while it runs |
| Needs | The LCE source and each platform's SDK | A console that runs homebrew: Dashlaunch on the Xbox 360 (RGH/JTAG or the Bad Update softmod), taiHEN on the PS Vita |
| Platforms | All six | Xbox 360, PS Vita |
| Mods | Built-in code mods; on Windows64 also DLL mods and data mods | Data mods (JSON), code mods (`.xex` / `.suprx`) |
| Guide | [Install into an LCE source tree](#install-into-an-lce-source-tree) | [docs/CONSOLE_INSTALL.md](docs/CONSOLE_INSTALL.md) |

**Data mods** are folders with a `forgedlce.json`: blocks, items, recipes and smelting, no code at all. See [docs/DATA_MODS.md](docs/DATA_MODS.md).

> **Status: v0, not yet run on any console or inside the real game.**
> - **Tested on a PC:** the loader core, data mods, code mods, every event, the injected plugin's logic (against a stand-in for the compiled game with simulated hooks) and the PowerPC hook builder. All pass: 64-bit and 32-bit, C++11 and C++03, built with clang. CI adds MSVC, gcc, and emulated big-endian PowerPC and 32-bit ARM; those haven't run yet.
> - **Written but untested:** the Xbox 360 and PS Vita plugin entry code, and the source patches inside the real game.
> - **Missing:** an address map for any real game version. The plugins can't work until one is made ([docs/FINDING_ADDRESSES.md](docs/FINDING_ADDRESSES.md)).

## Platforms

| Platform | Source build | Plugin | Platform-specific work |
|---|---|---|---|
| Windows64 | yes | — | none needed |
| Xbox 360 | yes | yes (Dashlaunch) | **patch 0008**: the 360 looks up names in its own XUI string table |
| Xbox One (Durango) | yes | — | none needed |
| PS3 | yes | — | **patch 0009**: the SPU chunk renderer drew unknown block IDs as stone |
| PS4 (Orbis) | yes | — | none needed |
| PS Vita | yes | yes (taiHEN) | none needed |
| **Wii U** | **no** | **no** | not in this source |

**Wii U (and Switch, 3DS)** aren't in this source. The only mentions are inside the Iggy and Miles middleware headers. Wii U Edition was a separate build of the game; supporting it would need that edition's source or binaries, plus a Wii U address map and plugin.

## Build and test without the game source

The loader, the plugin code, ExampleMod and the tests build on their own against a **fake game layer** ([standalone/fakegame/](standalone/fakegame/)). It's a set of small stand-ins for the LCE classes ForgedLCE uses, written from scratch, with the real classes' method signatures and public/protected access. You need CMake 3.18+ and any C++ compiler.

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

Two test programs run:
- **`flce_tests`** covers the source build. It boots the fake game with the patches' code in it, plus built-in mods, DLL mods and data mods. It checks registrations, recipes, names, IDs, every event (including cancellation and priority order) and error reporting.
- **`flce_inject_tests`** covers the plugin. It boots the fake game in *retail* mode with no patches, then does what the console plugins do: picks an address map, checks the game version, installs hooks, and lets the game start. The mods load through the hooks. It also unit-tests the PowerPC hook builder and the address-map format.

[.github/workflows/standalone.yml](.github/workflows/standalone.yml) runs both on every push, with no secrets needed. It covers MSVC (64- and 32-bit), gcc in C++03 mode, clang, 32-bit big-endian PowerPC and 32-bit ARM (under qemu), and it also compiles the PS Vita plugin with VitaSDK.

## Install into an LCE source tree

Commit or back up your LCE tree first, then:

```powershell
.\scripts\apply.ps1 -LceRoot D:\MCLCESRC -StaticMods .\examples\ExampleMod
```

This copies the loader into `Minecraft.World`, compiles the listed mods into the game, and applies [patches/](patches/). Then build any configuration of `MinecraftConsoles.sln`. Re-run the script with the full mod list whenever mods change. To remove everything:

```powershell
.\scripts\apply.ps1 -LceRoot D:\MCLCESRC -Uninstall
```

Built-in mod folders need a `forgedlce.json` naming the source file: `{ "modId": "examplemod", "source": "ExampleMod.cpp" }`. On Windows64 you can also drop DLL mods and data-mod folders into `<game exe folder>\mods\` without rebuilding.

[.github/workflows/build-windows64.yml](.github/workflows/build-windows64.yml) can build the real Windows64 game on GitHub (started by hand; see [docs/CI.md](docs/CI.md)). For now it compiles but can't link, because the prebuilt libraries in the known source copy are Git LFS placeholders.

## Writing a code mod

```cpp
#include "forgedlce/ForgedLCE.hpp"

namespace mymod {
void FLCE_CALL preInit(const FLCE_Api *api)
{
    FLCE_BlockDesc b = flce::blockDesc(L"mymod:shiny_block", L"Shiny Block");
    b.lightEmission = 1.0f;
    b.textureName   = L"blockGold"; // vanilla textures only, for now
    api->registerBlock(&b);
}
}

FLCE_MOD_ENTRY(unsigned int apiVersion, FLCE_ModInfo *info)
{
    info->modId   = L"mymod";
    info->name    = L"My Mod";
    info->version = L"1.0";
    info->preInit = mymod::preInit;
    return 1;
}
```

The same source builds as a built-in mod, a Windows64 DLL, an Xbox 360 `.xex` (with [ExampleMod360.def](examples/ExampleMod/ExampleMod360.def)) or a Vita `.suprx` (include `forgedlce/ForgedLCE_VitaModule.h` once). Keep console mods to C++03, because console compilers are older. See [examples/ExampleMod/ExampleMod.cpp](examples/ExampleMod/ExampleMod.cpp).

## Limits

- Mod blocks use IDs 1–255, because chunks store one byte per block. That leaves about 80–90 IDs for all mods combined.
- Only vanilla texture names work.
- Without a mods folder (consoles running a source build), IDs depend on mod order, so don't reorder `-StaticMods` once worlds exist. Everywhere else, `forgedlce_ids.txt` keeps IDs stable.
- The plugins don't support block/item callbacks or custom block drops yet (see [docs/CONSOLE_INSTALL.md](docs/CONSOLE_INSTALL.md)).
- Every player in a multiplayer game needs the same mods.

Design and roadmap: [docs/DESIGN.md](docs/DESIGN.md).
