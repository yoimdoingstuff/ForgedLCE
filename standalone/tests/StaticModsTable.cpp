// The built-in mod table for the tests, in the same form scripts/apply.ps1
// generates for the game (FLCE_StaticMods.cpp).
#include "stdafx.h"
#include "FLCE_Internal.h"

extern "C" int FLCE_CALL flce_mod_entry_examplemod(unsigned int apiVersion, FLCE_ModInfo *info);
extern "C" int FLCE_CALL flce_mod_entry_testmod(unsigned int apiVersion, FLCE_ModInfo *info);

namespace ForgedLCE {

const StaticMod g_staticMods[] = {
    { L"examplemod", flce_mod_entry_examplemod },
    { L"testmod",    flce_mod_entry_testmod },
    { NULL, NULL }
};

} // namespace ForgedLCE
