# Installing ForgedLCE on a console without compiling

On a console that can run homebrew, ForgedLCE runs as a **plugin that hooks your existing copy of Minecraft** while it runs. You don't rebuild the game or install anything over it. You copy files to the console.

> **Status: never tested on real hardware.** The plugin's logic is tested on a PC against a stand-in for the compiled game (`standalone/tests/run_inject_tests.cpp`). The parts that only run on a console are written but unverified: hooking real code, the console's file system, and loading the plugin. Also, **no address map exists yet for any real game version** (see below), so this can't work for anyone until one has been made.

## What you need

| | Xbox 360 | PS Vita |
|---|---|---|
| Console that runs unsigned code | RGH/JTAG, or the [Bad Update](https://free60.org/Hacks/Bad_Update_Hack/) softmod | HENkaku / Ensō |
| Plugin loader | Dashlaunch (Bad Update loads it via FreeMyXe or XeUnshackle; see the [ConsoleMods wiki](https://consolemods.org/wiki/Xbox_360:Bad_Update)) | taiHEN |
| The ForgedLCE plugin | `ForgedLCE.xex`, built once with the Xbox 360 XDK ([XBOX360_PLUGIN.md](XBOX360_PLUGIN.md)) | `ForgedLCE.suprx`, built with the free VitaSDK; CI builds it ([VITA_PLUGIN.md](VITA_PLUGIN.md)) |
| An address map for **your exact game version** | `addresses/<version>.txt` | `addresses/<version>.txt` |
| Mods | `.xex` code mods and/or data mods | `.suprx` code mods and/or data mods |

This page doesn't cover how to softmod or jailbreak the console; follow the pages linked above. Bad Update in particular is **non-persistent**: it has to be re-run after every reboot, before starting Minecraft.

### The address map

The plugin has to know where about 60 game functions and variables live in your specific version of the game; each title update moves them. Those addresses go in a text file. The plugin reads every map in its `addresses` folder and uses the one that matches the running game, checked by comparing an instruction at a known address. With no matching map it does nothing, so a wrong map can't crash the game.

**No maps exist yet.** One has to be made once per game version by someone with a disassembler, following [FINDING_ADDRESSES.md](FINDING_ADDRESSES.md). After that, anyone with that version can just copy the file.

## Xbox 360

Folder layout on the hard drive (`Hdd1:\ForgedLCE\`) or the root of a USB stick (`Usb:\ForgedLCE\`):

```
ForgedLCE\
  ForgedLCE.xex             the plugin
  addresses\
    xbox360-TU19.txt        address map(s), one per game version
  mods\
    SomeMod.xex             code mods
    rubymod\
      forgedlce.json        data mods (one folder each)
```

1. Copy the folder over.
2. Add the plugin to Dashlaunch's `launch.ini`, in a free plugin slot:
   ```ini
   [Plugins]
   plugin2 = Usb:\ForgedLCE\ForgedLCE.xex
   ```
3. Reboot (or re-run Bad Update), then start Minecraft.
4. Check `ForgedLCE\forgedlce.log`. It should say `Using address map ...` and list every mod it found.

If the log says *"The game had already started when ForgedLCE attached"*, the plugin noticed Minecraft too late. Quit and start Minecraft again.

After quitting Minecraft, **reboot before playing with mods again**. The plugin loads mods once per console session.

## PS Vita

```
ux0:data/ForgedLCE/
  addresses/vita-1.86.txt   address map(s)
  mods/
    SomeMod.suprx            code mods
    rubymod/forgedlce.json   data mods
ux0:tai/ForgedLCE.suprx      the plugin
```

1. Copy the files over (VitaShell or FTP).
2. In `ux0:tai/config.txt`, add the plugin under **your game's title ID**. You can find it as the folder name under `ux0:app/`; it's different for each region's release.
   ```
   *PCSXXXXXX
   ux0:tai/ForgedLCE.suprx
   ```
3. Reboot (or reload taiHEN's config), then start Minecraft.
4. Check `ux0:data/ForgedLCE/forgedlce.log`.

taiHEN loads the plugin before the game starts, so it's always in time.

## What works in the plugin

| Feature | Plugin | Built into the game from source |
|---|---|---|
| Data mods (`forgedlce.json`): blocks, items, recipes, smelting | yes | yes (Windows64; consoles: built-in code mods only) |
| Code mods: blocks, items, recipes, events | yes | yes |
| Block/item callbacks (`onActivated`, `onUse`) | **not yet**: ignored with a warning | yes |
| Custom block drops | **not yet**: the block drops itself | yes |
| Events: ticks, block break, entity join | yes, if the address map has those hooks | yes |
| `isPlayer` in entity join events | always `-1` (unknown) | yes |
| PS3 | no: no plugin for PS3 | yes (built-in mods) |

## Making mods

- **Data mods**: write a `forgedlce.json`. No compiler needed. See [DATA_MODS.md](DATA_MODS.md).
- **Code mods**: C/C++ against `include/forgedlce/ForgedLCE.h`, compiled per console:
  - Xbox 360: an XDK DLL project that outputs `.xex`, exporting `flce_mod_entry` as ordinal 1 (`examples/ExampleMod/ExampleMod360.def`).
  - PS Vita: a VitaSDK `.suprx` that includes `forgedlce/ForgedLCE_VitaModule.h` in one source file.
