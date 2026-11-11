# ForgedLCE

A port of the Forge mod loader to Minecraft: Legacy Console Edition. It runs on every platform the LCE source builds for: Windows64, Xbox 360, Xbox One, PS3, PS4, and PS Vita.

The mod API follows **Forge 1.7.10**: a preInit/init/postInit lifecycle, an event bus, and a registry that maps names to IDs. The LCE code doesn't match any single Forge version (its core is Java 1.3.2 with some later content back-ported; see [docs/RECON.md](docs/RECON.md)), so ForgedLCE copies Forge's design rather than its exact API.

> **Status: v0.** The loader, ExampleMod, and the tests compile and pass in the standalone build below, with no warnings. That was checked with clang (llvm-mingw) on Windows: both the Windows64 and console code paths, C++11 and C++03 mode, 64-bit and 32-bit, plus a 32-bit ARM build that compiles but wasn't run. MSVC, gcc, Linux clang, and the emulated PowerPC/ARM runs are set up in CI but haven't run yet. All 9 patches apply cleanly to the LCE source (both CRLF and LF checkouts), but haven't been compiled inside the real game yet, which needs each platform's SDK.

This repo contains **no game source**, only the loader, patches, and tools.

## Platforms

| Platform | In this LCE source | Mods | Platform-specific patch |
|---|---|---|---|
| Windows64 | yes | built-in + DLL | none needed |
| Xbox 360 | yes | built-in | **0008**: the 360 looks names up in its own XUI string table, so mod names need a separate hook |
| Xbox One (Durango) | yes | built-in | none needed |
| PS3 | yes | built-in | **0009**: the SPU chunk renderer has a fixed table of block types and drew unknown IDs as stone; mod blocks now use its generic block type |
| PS4 (Orbis) | yes | built-in | none needed |
| PS Vita | yes | built-in | none needed |
| **Wii U** | **no** | — | not possible from this source (see below) |

The other hooks (startup, ticks, block break, entity join, recipes, strings) are in code shared by every platform, and none of those files is excluded from any platform's build. That was checked in the project files.

**Wii U (and Switch, 3DS):** this source has no Wii U game code. The only Wii U mentions are inside the third-party Iggy and Miles middleware headers. Wii U Edition was a separate build of the game, and supporting it would need that edition's source and Nintendo's Wii U SDK. ForgedLCE's loader itself is platform-neutral, so with that source the same hooks would mostly carry over.

The loader and tests also pass in 32-bit builds, which is the word size of the Xbox 360, PS3 and Vita, and the 32-bit ARM build compiles. CI additionally runs the tests on emulated big-endian PowerPC, the Xbox 360/PS3 CPU family. Neither console's real SDK was used, since those aren't available here.

## Build and test without the game source

The loader, ExampleMod, and a test suite build on their own against a **fake game layer** ([standalone/fakegame/](standalone/fakegame/)). It's a set of small stand-ins for the LCE classes ForgedLCE uses (`Tile`, `Item`, `Level`, `Recipes`, ...), written from scratch. They copy the real classes' method signatures and public/protected access, so code that compiles here should also compile against the real game. You need CMake 3.18+ and any C++ compiler: Visual Studio or its Build Tools, gcc, or clang.

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

The tests boot the fake game with ExampleMod and a test mod built in. They check what each mod registered (blocks, items, recipes, smelting, names) and that the API rejects bad input. They fire every event, including cancellation and priority order. On Windows they also load DLL mods from `mods\` and check the ID file. `-DFLCE_EMULATE_CONSOLE=ON` tests the console code path on Windows. The build also produces `ExampleMod.dll`, which is a real Windows64 mod.

[.github/workflows/standalone.yml](.github/workflows/standalone.yml) runs all of this on every push and needs no secrets. It covers:
- MSVC, 64-bit and 32-bit, on both the Windows64 and console paths
- gcc in C++03 mode, standing in for the old console compilers
- clang
- 32-bit big-endian PowerPC (Xbox 360/PS3 stand-in) and 32-bit ARM (Vita stand-in), running under qemu

The fake layer checks ForgedLCE's own code and its API. It can't prove the patches compile inside the real game; that still needs the game source (see below).

## Install into an LCE source tree

Commit or back up your LCE tree first, then:

```powershell
.\scripts\apply.ps1 -LceRoot D:\MCLCESRC -StaticMods .\examples\ExampleMod
```

This copies the loader into `Minecraft.World`, compiles the listed mods into the game, and applies [patches/](patches/). Then build any configuration of `MinecraftConsoles.sln`. Re-run the script with the full mod list whenever mods change. To remove everything:

```powershell
.\scripts\apply.ps1 -LceRoot D:\MCLCESRC -Uninstall
```

## Building the real game on GitHub Actions (optional)

[.github/workflows/build-windows64.yml](.github/workflows/build-windows64.yml) runs only when started by hand. It installs VS2012 on a GitHub runner and builds the real Windows64 game with ForgedLCE, reading the LCE source from a separate **private** repo through two secrets. Setup and limits are in [docs/CI.md](docs/CI.md). For now it can compile but not link, because the prebuilt libraries in the known source copy are missing (they're Git LFS placeholders).

## Two kinds of mods

- **Built-in mods** work on every platform and are the only kind on consoles. Pass each mod folder with `-StaticMods`. Each folder needs a `forgedlce.json`:
  ```json
  { "modId": "examplemod", "source": "ExampleMod.cpp" }
  ```
- **DLL mods** work on Windows64 only. Build the mod as a DLL (see [examples/ExampleMod/CMakeLists.txt](examples/ExampleMod/CMakeLists.txt)) and drop it into `<game exe folder>\mods\`. No game rebuild is needed.

The same source builds both ways. See [examples/ExampleMod/ExampleMod.cpp](examples/ExampleMod/ExampleMod.cpp).

## Writing a mod

```cpp
#include "forgedlce/ForgedLCE.hpp"

namespace mymod {
void FLCE_CALL preInit(const FLCE_Api *api)
{
    FLCE_BlockDesc b = flce::blockDesc(L"mymod:shiny_block", L"Shiny Block");
    b.lightEmission = 1.0f;
    b.textureName   = L"blockGold"; // v0: vanilla textures only
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

If the mod has to run on consoles, keep it to C++03 (no lambdas, no `std::to_wstring`). Console compilers are older.

## Limits in v0

- Mod blocks use IDs 1–255, because chunks store one byte per block. That leaves about 80–90 IDs for all mods combined.
- Only vanilla texture names work.
- On consoles, IDs depend on mod order. Don't reorder `-StaticMods` once worlds exist.
- Every player in a multiplayer game needs the same mods.

The design and roadmap are in [docs/DESIGN.md](docs/DESIGN.md).
