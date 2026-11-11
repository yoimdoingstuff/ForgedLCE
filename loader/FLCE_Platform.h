// ForgedLCE platform services, used by the loader core.
//
// Implemented once per way ForgedLCE runs:
//   loader/FLCE_PlatformGame.cpp      compiled into the game (all platforms)
//   inject/xbox360/FLCE_Plugin360.cpp  Xbox 360 plugin (Dashlaunch)
//   inject/vita/FLCE_PluginVita.cpp    PS Vita plugin (taiHEN)
//   standalone/FLCE_PlatformHost.cpp   tests on a PC
#ifndef FLCE_PLATFORM_H
#define FLCE_PLATFORM_H

#include "ForgedLCE.h"

#include <string>
#include <vector>

namespace ForgedLCE {
namespace Platform {

// Called once before mods load. Safe to call again.
void Init();

// Writes one finished log line (ending in "\n"). Must be thread-safe.
void WriteLog(const std::wstring &line);

// Folder holding mods and ForgedLCE's own files, ending in a separator.
// Empty if this platform has no mods folder (consoles running a source build).
std::wstring ModsDir();

// Separator for building paths under ModsDir(): L"\\" or L"/".
const wchar_t *PathSeparator();

bool ReadFile(const std::wstring &path, std::string &bytes);
bool WriteFile(const std::wstring &path, const std::string &bytes);

// Names (not paths) of the subfolders or files in `dir`.
void ListDirectory(const std::wstring &dir, bool directories, std::vector<std::wstring> &names);

// File extension of code-mod modules, e.g. L".dll". NULL if this platform
// can't load code mods from files (they must be built in instead).
const wchar_t *ModModuleExtension();

// Loads a code-mod module and returns its entry point, or NULL with `error` set.
FLCE_ModEntryFn LoadModModule(const std::wstring &path, std::wstring &error);

} // namespace Platform
} // namespace ForgedLCE

#endif
