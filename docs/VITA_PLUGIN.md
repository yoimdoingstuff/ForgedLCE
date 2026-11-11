# Building the PS Vita plugin (ForgedLCE.suprx)

The Vita plugin is built with the free, open-source [VitaSDK](https://vitasdk.org/), so anyone can build it. The `vita-plugin` job in [.github/workflows/standalone.yml](../.github/workflows/standalone.yml) also builds it on every push and uploads `ForgedLCE.suprx` as an artifact.

> **Untested.** It has never run on a Vita, and that CI job is marked experimental until it does. The plugin's platform file, `inject/vita/FLCE_PluginVita.cpp`, is the untested part; the code it shares with the other builds is tested on a PC.

## Build

```bash
export VITASDK=/usr/local/vitasdk
```

```bash
cmake -S inject/vita -B build-vita
```

```bash
cmake --build build-vita
```

The output is `build-vita/ForgedLCE.suprx`. Installing it is covered in [CONSOLE_INSTALL.md](CONSOLE_INSTALL.md).

## Character width

The plugin is built with VitaSDK's GCC, where `wchar_t` is 4 bytes. If the game uses 2-byte wide strings, set `abi.wcharSize = 2` in the address map; mod names are then converted before they're handed to the game. [FINDING_ADDRESSES.md](FINDING_ADDRESSES.md) explains how to check.

## Code mods for the plugin

A Vita code mod is a separate `.suprx` built with VitaSDK. It compiles the mod's sources against `include/`, and exactly one source file includes `forgedlce/ForgedLCE_VitaModule.h`, which provides `module_start`. Build it like any taiHEN user plugin (`-nostartfiles`, `vita_create_self(... UNSAFE)`), and put it in `ux0:data/ForgedLCE/mods/`.
