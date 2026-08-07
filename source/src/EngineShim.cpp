/* SPDX-License-Identifier: GPL-3.0-only */
#include "../include/Platform.h"
#include "../include/tesmio_api.h"

DLL_EXPORT unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

DLL_EXPORT int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    if (info)
    {
        info->name = "ForceNodes Engine Compatibility";
        info->version = "1.7.0";
    }
    if (host && host->log)
        host->log("ForceNodes  compatibility module active; the engine core is integrated into ForceNodes.dll");
    return 0; /* Deliberately hook-free; retained for the established three-file install layout. */
}

extern "C" BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
