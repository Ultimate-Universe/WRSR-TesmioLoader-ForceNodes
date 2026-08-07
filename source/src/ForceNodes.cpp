/* SPDX-License-Identifier: GPL-3.0-only */
#include "Core.h"
#include "Signatures.h"

namespace
{
static const TsmHost* H;
static NativeAddresses g_native;
static CoreResolved g_resolved;
static int g_initialised;
static int g_started;
static int g_inFrameHook;

typedef void (__fastcall* MainFrameFn)(void* construction);
static MainFrameFn g_nextFrame;

static const unsigned char kFramePrefix[14] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x55,0x41,0x56
};

static void* ResolveC3DExport(const char* name, int required)
{
    void* resolved = 0;
    if (H && H->findIatSlot && H->exeModule)
    {
        void** slot = H->findIatSlot(H->exeModule, "C3DDLL64.dll", name);
        if (slot && H->readablePtr && H->readablePtr(slot, sizeof(void*)))
            resolved = *slot;
    }
    if (!resolved && H && H->engineModule)
        resolved = (void*)GetProcAddress((HMODULE)H->engineModule, name);
    if (!resolved && required && H && H->log)
        H->log("ForceNodes  required C3D export missing: %s", name);
    return resolved;
}

static int ResolveRuntime(void)
{
    memset(&g_native, 0, sizeof(g_native));
    memset(&g_resolved, 0, sizeof(g_resolved));
    if (!ResolveNativeAddresses(H, &g_native))
    {
        H->log("ForceNodes  native signature validation failed; plugin remains inactive");
        return 0;
    }

    g_resolved.frameTarget = g_native.frame;
    g_resolved.splitPath = g_native.split;
    g_resolved.mergePaths = g_native.merge;
    g_resolved.refreshWorld = g_native.refresh;
    g_resolved.insertPoint24 = g_native.insert24;
    g_resolved.reserveByteVector = g_native.reserveByte;
    g_resolved.renderQueue = g_native.renderQueue;

    g_resolved.getKeyDown = ResolveC3DExport("?GetKeyDown@C3D_INPUT@@QEAA_NH@Z", 1);
    g_resolved.getMouseLeftPress = ResolveC3DExport("?GetMouseLeftPress@C3D_INPUT@@QEAA_NXZ", 0);
    g_resolved.getMouseRightPress = ResolveC3DExport("?GetMouseRightPress@C3D_INPUT@@QEAA_NXZ", 0);
    g_resolved.getMouseX1Press = ResolveC3DExport("?GetMouseX1Press@C3D_INPUT@@QEAA_NXZ", 1);
    g_resolved.getMouseX2Press = ResolveC3DExport("?GetMouseX2Press@C3D_INPUT@@QEAA_NXZ", 1);
    g_resolved.nodeCtor = ResolveC3DExport("??0C3D_NODE@@QEAA@XZ", 1);
    g_resolved.nodeTransform = ResolveC3DExport("?CreateFromPositionRotationScale@C3D_NODE@@QEAAXVC3DVECTOR3@@00@Z", 1);
    g_resolved.fontPrint = ResolveC3DExport("?PrintLeftUnicodeNoArg@C3D_FONTMANAGER@@QEAAXPEAVC3D_FONT@@MMKPEB_W@Z", 0);

    unsigned inputRva = (unsigned)H->configInt("plugins\\ForceNodes.ini", "ForceNodes",
                                               "advanced_input_object_rva", 0xA54B90);
    if ((usize)inputRva >= H->exeSize)
    {
        H->log("ForceNodes  input object RVA is outside SOVIET64.exe: 0x%X", inputRva);
        return 0;
    }
    g_resolved.inputObject = H->exeBase + inputRva;
    if (!H->readablePtr(g_resolved.inputObject, 1))
    {
        H->log("ForceNodes  input object is unreadable at exe+0x%X", inputRva);
        return 0;
    }

    if (!g_resolved.getKeyDown || !g_resolved.getMouseX1Press ||
        !g_resolved.getMouseX2Press || !g_resolved.nodeCtor ||
        !g_resolved.nodeTransform)
    {
        H->log("ForceNodes  required C3D input/render imports are missing");
        return 0;
    }
    return 1;
}

static void __fastcall ForceNodesFrameHook(void* construction)
{
    if (!g_nextFrame) return;
    if (g_inFrameHook)
    {
        g_nextFrame(construction);
        return;
    }

    g_inFrameHook = 1;
    Core_BeforeNativeFrame(construction);
    g_nextFrame(construction);
    Core_AfterNativeFrame(construction);
    g_inFrameHook = 0;
}

static int InstallFrameHook(void)
{
    if (!g_native.frame) return 0;

    void* existing = 0;
    if (IsAbsoluteJumpDetour(g_native.frame, &existing))
    {
        if (!existing || existing == (void*)&ForceNodesFrameHook)
        {
            H->log("ForceNodes  frame detour is invalid or already points to ForceNodes");
            return 0;
        }
        DWORD oldProtect = 0;
        unsigned char* destinationSlot = (unsigned char*)g_native.frame + 6;
        if (!VirtualProtect(destinationSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            H->log("ForceNodes  frame detour chaining failed: VirtualProtect refused");
            return 0;
        }
        g_nextFrame = (MainFrameFn)existing;
        *(void**)destinationSlot = (void*)&ForceNodesFrameHook;
        DWORD ignored = 0;
        VirtualProtect(destinationSlot, sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), g_native.frame, 14);
        H->log("ForceNodes  frame hook chained after an existing compatible detour");
        return 1;
    }

    if (!MatchFrameNativePrefix(g_native.frame))
    {
        H->log("ForceNodes  frame hook refused: target is neither native nor a supported detour");
        return 0;
    }

    void* trampoline = 0;
    if (!H->installInlineHook(g_native.frame, (void*)&ForceNodesFrameHook, &trampoline,
                              kFramePrefix, sizeof(kFramePrefix), "ForceNodes frame"))
        return 0;
    g_nextFrame = (MainFrameFn)trampoline;
    return g_nextFrame != 0;
}

static int ApiRegister(ForceNodesFrameClient fn, void* user) { return Core_RegisterFrameClient(fn, user); }
static int ApiEnumerate(ForceNodesSegmentVisitor fn, void* user) { return Core_EnumerateSegments(fn, user); }
static int ApiSplit(const ForceNodesSegmentRef* s, const ForceNodesVec3* p) { return Core_SplitSegment(s, p); }
static int ApiCursor(const ForceNodesVec3* p) { return Core_RequestCursorOverride(p); }
static int ApiReady(void) { return Core_IsReady(); }
static unsigned ApiCaps(void) { return Core_PathPreviewCapabilities(); }
static int ApiConnection(const ForceNodesSegmentRef* s, const ForceNodesVec3* p, int splitNow, int requireNative)
{
    return Core_RequestConnectionTarget(s, p, splitNow, requireNative);
}
static int ApiCancel(void) { return Core_CancelCurrentPathBuild(); }
static int ApiKey(int dik) { return Core_GetKeyDown(dik); }

static ForceNodesApiV1 g_apiV1 = {
    sizeof(ForceNodesApiV1), FORCE_NODES_API_VERSION_V1,
    ApiRegister, ApiEnumerate, ApiSplit, ApiCursor, ApiReady
};
static ForceNodesApiV2 g_apiV2 = {
    sizeof(ForceNodesApiV2), FORCE_NODES_API_VERSION_V2,
    ApiRegister, ApiEnumerate, ApiSplit, ApiCursor, ApiReady, ApiCaps
};
static ForceNodesApiV3 g_apiV3 = {
    sizeof(ForceNodesApiV3), FORCE_NODES_API_VERSION_V3,
    ApiRegister, ApiEnumerate, ApiSplit, ApiCursor, ApiReady, ApiCaps,
    ApiConnection, ApiCancel, ApiKey
};
static ForceNodesApi g_apiV6 = {
    sizeof(ForceNodesApi), FORCE_NODES_API_VERSION,
    ApiRegister, ApiEnumerate, ApiSplit, ApiCursor, ApiReady, ApiCaps,
    ApiConnection, ApiCancel, ApiKey
};

static void ProvideServices(void)
{
    int ok1 = H->provide(FORCE_NODES_SERVICE, FORCE_NODES_API_VERSION_V1, &g_apiV1);
    int ok2 = H->provide(FORCE_NODES_SERVICE, FORCE_NODES_API_VERSION_V2, &g_apiV2);
    int ok3 = H->provide(FORCE_NODES_SERVICE, FORCE_NODES_API_VERSION_V3, &g_apiV3);
    int ok6 = H->provide(FORCE_NODES_SERVICE, FORCE_NODES_API_VERSION, &g_apiV6);
    if (!ok1 || !ok2 || !ok3 || !ok6)
        H->log("ForceNodes  one or more service versions were already provided; continuing with valid registrations");
}
}

extern "C" { int _fltused = 0; }

DLL_EXPORT unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

DLL_EXPORT int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    if (!host || !info || host->apiVersion != TSM_API_VERSION ||
        host->structSize < sizeof(TsmHost) || !host->log || !host->readablePtr ||
        !host->configInt || !host->configString || !host->provide ||
        !host->installInlineHook || !host->exeBase || !host->exeSize)
        return 1;

    H = host;
    info->name = "ForceNodes";
    info->version = "1.7.0";

    if (!H->configInt("plugins\\ForceNodes.ini", "ForceNodes", "enabled", 1))
    {
        H->log("ForceNodes  disabled in ForceNodes.ini");
        return 1;
    }

    if (!ResolveRuntime()) return 1;
    if (!Core_Init(H, &g_resolved))
    {
        H->log("ForceNodes  core initialisation failed; plugin remains inactive");
        return 1;
    }

    ProvideServices();
    g_initialised = 1;
    H->log("ForceNodes  v1.7.0 initialised; source-built integrated core ready for hook phase");
    return 0;
}

DLL_EXPORT int TsmPluginStart(void)
{
    if (!g_initialised || g_started) return g_started ? 0 : 1;
    if (!InstallFrameHook())
    {
        Core_SetReady(0);
        H->log("ForceNodes  frame hook refused; service remains loaded but inactive");
        return 1;
    }
    g_started = 1;
    Core_SetReady(1);
    H->log("ForceNodes  v1.7.0 active; overlay, force placement, grid mode and safe removal ready");
    return 0;
}

extern "C" BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
