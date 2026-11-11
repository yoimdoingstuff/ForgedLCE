// Built-in mod table for the injected-mode tests. A plugin can carry
// built-in code mods too; here it carries the test mod.
#include "stdafx.h"
#include "FLCE_Internal.h"

extern "C" int FLCE_CALL flce_mod_entry_testmod(unsigned int apiVersion, FLCE_ModInfo *info);

namespace ForgedLCE {

const StaticMod g_staticMods[] = {
    { L"testmod", flce_mod_entry_testmod },
    { NULL, NULL }
};

} // namespace ForgedLCE
