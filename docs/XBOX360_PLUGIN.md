# Building the Xbox 360 plugin (ForgedLCE.xex)

This is done **once**, by whoever distributes the plugin. People installing ForgedLCE just copy the finished `ForgedLCE.xex` ([CONSOLE_INSTALL.md](CONSOLE_INSTALL.md)).

> **Untested.** The plugin's platform file, `inject/xbox360/FLCE_Plugin360.cpp`, was written without access to the XDK or a console. It uses xkelib for the kernel and XAM exports, and inline PowerPC assembly for the trampoline pool. Expect small fixes on the first build. Everything else it compiles is tested on a PC.

## Requirements

- Visual Studio 2010 with the Xbox 360 XDK
- xkelib (community headers and import libraries for kernel/XAM exports: `XamGetCurrentTitleId`, `XexLoadImage`, `XexGetProcedureAddress`, `ObCreateSymbolicLink`, `ExCreateThread`, ...)

## Project

Create an **Xbox 360 DLL** project (output: a DLL-type `.xex`):

1. Add these sources:
   ```
   loader/FLCE_Events.cpp      loader/FLCE_Loader.cpp     loader/FLCE_Registry.cpp
   loader/FLCE_Json.cpp        loader/FLCE_DataMods.cpp   loader/FLCE_StaticMods.cpp
   inject/FLCE_Addresses.cpp   inject/FLCE_GameInjected.cpp
   inject/FLCE_InjectHooks.cpp inject/FLCE_HookPPC.cpp
   inject/xbox360/FLCE_Plugin360.cpp
   ```
2. Include directories, in this order: `inject/xbox360` (its `stdafx.h`), `loader`, `include/forgedlce`, `include`, `inject`, and xkelib's include folder.
3. Link xkelib's import libraries.
4. Don't use precompiled headers (or make `inject/xbox360/stdafx.h` the PCH).
5. Build Release. Rename the output to `ForgedLCE.xex`.

## Built-in mods

`loader/FLCE_StaticMods.cpp` is an empty table. To ship code mods inside the plugin, replace it with a table like the one `scripts/apply.ps1` generates and compile the mods in, the same way as for a source build.

## Code mods for the plugin

A code mod for the Xbox 360 plugin is its own Xbox 360 DLL project that outputs `.xex`. It compiles only the mod's sources against `include/` and uses a `.def` file exporting `flce_mod_entry` as ordinal 1 (see `examples/ExampleMod/ExampleMod360.def`). Put the result in `ForgedLCE\mods\`.
