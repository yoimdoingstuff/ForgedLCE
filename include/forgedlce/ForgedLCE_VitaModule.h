/*
 * ForgedLCE: module entry points for a PS Vita code mod (.suprx).
 *
 * Include this in exactly ONE source file of the mod (it defines functions).
 * The ForgedLCE Vita plugin loads the mod with sceKernelLoadStartModule and
 * passes a registration function as argp; module_start hands it the mod's
 * flce_mod_entry.
 */
#ifndef FORGEDLCE_VITAMODULE_H
#define FORGEDLCE_VITAMODULE_H

#include "ForgedLCE.h"

FLCE_EXTERN_C int FLCE_CALL flce_mod_entry(unsigned int apiVersion, FLCE_ModInfo *info);

FLCE_EXTERN_C int module_start(unsigned int args, void *argp)
{
    if (argp != NULL && args >= sizeof(FLCE_VitaRegisterFn)) {
        (*(FLCE_VitaRegisterFn *)argp)(flce_mod_entry);
    }
    return 0; /* SCE_KERNEL_START_SUCCESS */
}

FLCE_EXTERN_C int module_stop(unsigned int args, void *argp)
{
    (void)args;
    (void)argp;
    return 0; /* SCE_KERNEL_STOP_SUCCESS */
}

FLCE_EXTERN_C void _start() __attribute__((weak, alias("module_start")));

#endif
