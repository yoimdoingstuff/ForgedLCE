// A DLL mod for the Windows64 tests: loaded from <test exe dir>\mods\.
#include "forgedlce/ForgedLCE.hpp"

namespace dllmod {

void FLCE_CALL preInit(const FLCE_Api *api)
{
    FLCE_BlockDesc b = flce::blockDesc(L"dllmod:dll_block", L"DLL Block");
    api->registerBlock(&b);
}

} // namespace dllmod

FLCE_MOD_ENTRY(unsigned int apiVersion, FLCE_ModInfo *info)
{
    if (apiVersion < FLCE_API_VERSION) return 0;
    info->modId   = L"dllmod";
    info->name    = L"DLL test mod";
    info->version = L"0";
    info->preInit = dllmod::preInit;
    return 1;
}
