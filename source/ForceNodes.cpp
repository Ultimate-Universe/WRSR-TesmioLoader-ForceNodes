/* SPDX-License-Identifier: GPL-3.0-only */
#include "ForceNodes_API.h"

#define DLL_EXPORT extern "C" __declspec(dllexport)
#define NOINLINE __declspec(noinline)

typedef unsigned long long usize;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern "C" unsigned __int64 __readgsqword(unsigned long Offset);
#pragma intrinsic(__readgsqword)
extern "C" int _fltused = 0;

extern "C" void* memcpy(void* dst, const void* src, usize n)
{
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    for (usize i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
extern "C" void* memset(void* dst, int value, usize n)
{
    u8* d = (u8*)dst;
    for (usize i = 0; i < n; ++i) d[i] = (u8)value;
    return dst;
}
extern "C" int memcmp(const void* a, const void* b, usize n)
{
    const u8* x=(const u8*)a; const u8* y=(const u8*)b;
    for (usize i=0;i<n;++i) { if (x[i]!=y[i]) return (int)x[i]-(int)y[i]; }
    return 0;
}

#define TSM_API_VERSION 3u

typedef struct TsmPluginInfo { const char* name; const char* version; } TsmPluginInfo;
typedef struct TsmHost
{
    unsigned apiVersion;
    unsigned structSize;
    void* exeModule;
    u8* exeBase;
    usize exeSize;
    void* engineModule;
    const char* baseDir;
    const char* pluginDir;
    void (*log)(const char* fmt, ...);
    void** (*findIatSlot)(void* module, const char* dll, const char* fn);
    int (*patchIat)(void* module, const char* dll, const char* fn, void* detour, void** original, const char* label);
    int (*installInlineHook)(void* target, void* detour, void** trampoline, const u8* expect, usize stolen, const char* label);
    u8* (*allocNear)(u8* anchor, usize size);
    int (*readablePtr)(const void* p, usize n);
    long (*faultFilter)(const char* what, void* exceptionPointers);
    int (*configInt)(const char* iniName, const char* section, const char* key, int fallback);
    int (*configString)(const char* iniName, const char* section, const char* key, char* out, int outSize, const char* fallback);
    int (*provide)(const char* service, unsigned version, const void* iface);
    const void* (*consume)(const char* service, unsigned version);
} TsmHost;

static const TsmHost* H = 0;
static u8* g_core = 0;
static void (*g_coreFrame)(void*) = 0;
static int g_ready = 0;
static unsigned g_pathCaps = 0;
static int g_logActions = 1;
static int g_cursorOffset = 3948;
static int g_rawCursorOffset = 3960;
static int g_toolNameOffset = 0xd428;
static int g_scanBeforeMainClick = 0;
static int g_scanPathClick = 1;
static int g_enablePathPreviewHooks = 0;
static int g_inFrame = 0;
static int g_inPathHook = 0;
static int g_pathScanLatch = 0;
static int g_phase = -1;
static int g_overrideRequested = 0;
static ForceNodesVec3 g_overridePosition;

/* API v3 safe-connection transaction state. The cursor override is held until
   the outer construction frame has completed, because the native final-build
   call occurs after the road/pedestrian preview selector returns. */
static int g_connectionRequested = 0;
static int g_connectionSplitNow = 0;
static int g_connectionRequireNativeNode = 0;
static ForceNodesSegmentRef g_connectionSegment;
static ForceNodesVec3 g_connectionPosition;
static unsigned g_connectionStatus = FORCE_NODES_CONNECTION_NONE;
static float g_nativeNodeTolerance = 0.75f;
static int g_selectedNativeNodeOffset = 0xf650;
static int g_selectedSharedNodeOffset = 0xf690;
static int g_nativeBuilderGuardReady = 0;
static int g_blockNativeBuildThisFrame = 0;
static int g_blockNativeBuildLogged = 0;

static int g_frameOverrideActive = 0;
static void* g_frameOverrideConstruction = 0;
static int g_frameHaveCursor = 0;
static int g_frameHaveRaw = 0;
static ForceNodesVec3 g_frameOriginalCursor;
static ForceNodesVec3 g_frameOriginalRaw;
static ForceNodesVec3 g_frameEffectiveCursor;
static char g_toolName[96];

/* v1.6 configurable input bridge. The original engine remains responsible for
   edge latching and all node-editing actions; ForceNodes only replaces the
   five input queries with independently configurable bindings. */
typedef u8 (*CoreKeyFn)(void*,int);
typedef u8 (*CoreMouseFn)(void*);

typedef struct BindingModifier
{
    int first;
    int second; /* optional alternate, e.g. left/right CTRL */
} BindingModifier;

typedef struct InputBinding
{
    int valid;
    int disabled;
    int primaryType; /* 1 keyboard, 2 mouse */
    int primaryCode;
    BindingModifier modifiers[3];
    int modifierCount;
    char text[64];
} InputBinding;

static InputBinding g_bindOverlay;
static InputBinding g_bindForce;
static InputBinding g_bindGrid;
static InputBinding g_bindAdd;
static InputBinding g_bindRemove;

static CoreKeyFn g_originalKeyFn=0;
static CoreMouseFn g_originalMouseLeftFn=0;
static CoreMouseFn g_originalMouseRightFn=0;
static CoreMouseFn g_originalMouseX1Fn=0;
static CoreMouseFn g_originalMouseX2Fn=0;
static int g_customBindingsReady=0;

#define FN_BIND_SENTINEL_MODIFIER 0xF0
#define FN_BIND_SENTINEL_OVERLAY  0xF1
#define FN_BIND_SENTINEL_FORCE    0xF2
#define FN_BIND_SENTINEL_GRID     0xF3

typedef struct FrameClientEntry { ForceNodesFrameClient fn; void* user; } FrameClientEntry;
static FrameClientEntry g_clients[16];
static int g_clientCount = 0;

/* Retired StraightConnections compatibility state. Stable v1.5 does not
   publish the experimental v4/v5 straight APIs and never writes a straight
   row into the core HUD. The variables remain only so the old source paths
   compile while all associated hooks are disabled by default. */
static int g_straightModeEnabled = 0;
static int g_buildingHookReady = 0;
static int g_enableLegacyBuildingPlacementHook = 0;
static int g_inNativeDetour = 0;
static int g_inBuildingPlacement = 0;
static int g_buildingPreviewMode = 0;
static void* g_buildingConstruction = 0;
static void* g_buildingType = 0;
static ForceNodesVec3 g_buildingCenter = {0,0,0};
static unsigned g_buildingSerial = 0;
static int g_buildingStatusOffset = 0x11a10;
static float g_buildingOriginTolerance = 0.35f;
static float g_buildingCachePositionTolerance = 0.50f;
static float g_buildingSegmentResolveTolerance = 0.60f;
/* Building auto-connections are intentionally much stricter than manual
   cursor selection: a nearby old node must never steal an exact projected
   landing point. */
static float g_buildingNodeTolerance = 0.05f;
static float g_buildingTargetMergeTolerance = 0.05f;
static int g_maxBuildingConnections = 64;
static int g_buildingConnectorStreamLogged = 0;

#define MAX_BUILDING_CONNECTIONS 64
#define MAX_BUILDING_PATH_POINTS 128

typedef struct BuildingProposal
{
    int used;
    int managed;
    int hasTarget;
    int rejected;
    int needsSplit;
    int prepared;
    unsigned pathKind;
    int worldType;
    int pathClass;
    int pathSubtype;
    int originAtStart;
    void* world;
    ForceNodesVec3 buildingCenter;
    ForceNodesVec3 origin;
    ForceNodesVec3 target;
    ForceNodesSegmentRef segment;
    void* preparedNode;
} BuildingProposal;

static BuildingProposal g_buildingWork[MAX_BUILDING_CONNECTIONS];
static int g_buildingWorkCount = 0;
static BuildingProposal g_buildingPreview[MAX_BUILDING_CONNECTIONS];
static int g_buildingPreviewCount = 0;
static void* g_buildingPreviewType = 0;
static ForceNodesVec3 g_buildingPreviewCenter = {0,0,0};
static unsigned g_buildingPreviewSerial = 0;
static const ForceNodesBuildingConnectionRef* g_currentBuildingConnection = 0;
static int g_buildingRequestKind = 0; /* 1 target, 2 strict rejection */
static ForceNodesSegmentRef g_buildingRequestedSegment;
static ForceNodesVec3 g_buildingRequestedTarget;
static int g_buildingRequestedNeedsSplit = 0;

typedef void (*RoadBuilderFn)(void*,u8,u8,u8,u8,u8,u8,float,u8,u8);
typedef void (*PedBuilderFn)(void*,int,u8,u8,u8,u8);
/* Native path builder ABI (SOVIET64.exe +0x4FCA20).
   The function has TEN integer/pointer arguments. Earlier v1.3.1 declared only
   five, so stack arguments 6-10 were not forwarded through the compatibility
   detour. The game then read a stale float bit-pattern (0x3F800000) as arg8,
   treated it as a pointer, and crashed on the first road/path placement.

   Important: arg8 is an optional std::vector-like descriptor containing
   24-byte path-control records. It is NOT a native node pointer. ForceNodes
   must preserve it byte-for-byte and let the native builder resolve the exact
   node from arg2 after the target segment has been split and refreshed. */
typedef void (*NativePathBuilderFn)(void*,void*,u32,u32,usize,usize,usize,usize,usize,usize);
typedef void (*BuildingPlacementFn)(void*,void*,u8,u8);
static RoadBuilderFn g_roadBuilder=0;
static PedBuilderFn g_pedBuilder=0;
static NativePathBuilderFn g_nativePathBuilder=0;
static BuildingPlacementFn g_buildingPlacement=0;

static u8 LowerAscii(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + ('a'-'A')) : c; }
static int WideNameEquals(const u16* wide, u16 bytes, const char* ascii)
{
    if (!wide || !ascii || (bytes & 1)) return 0;
    unsigned count = bytes / 2;
    unsigned n = 0; while (ascii[n]) ++n;
    if (count != n) return 0;
    for (unsigned i=0;i<n;++i)
    {
        u16 wc = wide[i];
        if (wc > 127 || LowerAscii((u8)wc) != LowerAscii((u8)ascii[i])) return 0;
    }
    return 1;
}

static u8* FindLoadedModule(const char* wanted)
{
    u8* peb = (u8*)(usize)__readgsqword(0x60);
    if (!peb) return 0;
    u8* ldr = *(u8**)(peb + 0x18);
    if (!ldr) return 0;
    u8* head = ldr + 0x20;
    u8* link = *(u8**)head;
    int guard = 0;
    while (link && link != head && guard++ < 512)
    {
        u8* entry = link - 0x10;
        u8* base = *(u8**)(entry + 0x30);
        u16 nameBytes = *(u16*)(entry + 0x58);
        const u16* name = *(const u16**)(entry + 0x60);
        if (base && WideNameEquals(name, nameBytes, wanted)) return base;
        link = *(u8**)link;
    }
    return 0;
}

static int Readable(const void* p, usize n)
{
    return H && H->readablePtr && H->readablePtr(p,n);
}

static int ValidateCore(u8* base)
{
    static const u8 frameExpected[15] = {
        0x41,0x57,0x41,0x56,0x56,0x57,0x55,0x53,0x48,0x81,0xEC,0xB8,0x00,0x00,0x00
    };
    static const u8 actionExpected[16] = {
        0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x58
    };
    if (!base) return 0;
    if (!Readable(base, 0x1000) || !Readable(base + 0x1315f8, 8)) return 0;
    if (memcmp(base + 0x2c50, frameExpected, sizeof(frameExpected)) != 0) return 0;
    if (memcmp(base + 0x4680, actionExpected, sizeof(actionExpected)) != 0) return 0;
    if (*(const TsmHost**)(base + 0x90a8) != H) return 0;
    if (!*(void**)(base + 0x90d8)) return 0;
    return 1;
}

static int ServiceReady(void) { return g_ready; }
static unsigned PathPreviewCapabilities(void) { return g_pathCaps; }

static int RegisterFrameClient(ForceNodesFrameClient callback, void* userData)
{
    if (!callback || g_clientCount >= (int)(sizeof(g_clients)/sizeof(g_clients[0]))) return 0;
    for (int i=0;i<g_clientCount;++i)
        if (g_clients[i].fn == callback && g_clients[i].user == userData) return 1;
    g_clients[g_clientCount].fn = callback;
    g_clients[g_clientCount].user = userData;
    ++g_clientCount;
    return 1;
}

static int EnumerateSegments(ForceNodesSegmentVisitor visitor, void* userData)
{
    if (!g_ready || !visitor || !g_core) return 0;
    int delivered = 0;
    int worldCount = *(int*)(g_core + 0x91b8);
    if (worldCount < 0) return 0;
    if (worldCount > 128) worldCount = 128;
    void** worlds = (void**)(g_core + 0x511c0);

    for (int wi=0; wi<worldCount; ++wi)
    {
        u8* world = (u8*)worlds[wi];
        if (!world || !Readable(world + 0x258, 4) || !Readable(world + 0x2b0, 24)) continue;
        int worldType = *(int*)(world + 0x258);
        void** pathsBegin = *(void***)(world + 0x2b0);
        void** pathsEnd   = *(void***)(world + 0x2b8);
        if (!pathsBegin || !pathsEnd || pathsEnd < pathsBegin) continue;
        usize pathCount64 = (usize)(pathsEnd - pathsBegin);
        if (pathCount64 > 200000) continue;
        int pathCount = (int)pathCount64;
        if (!Readable(pathsBegin, (usize)pathCount * sizeof(void*))) continue;

        for (int pi=0; pi<pathCount; ++pi)
        {
            u8* path = (u8*)pathsBegin[pi];
            if (!path || !Readable(path + 0x8, 24) || !Readable(path + 0x140, 1)) continue;
            if (*(u8*)(path + 0x140)) continue;
            int pathClass = 0;
            if (Readable(path + 0x120, 4)) pathClass = *(int*)(path + 0x120);
            u8* pointBegin = *(u8**)(path + 0x8);
            u8* pointEnd   = *(u8**)(path + 0x10);
            if (!pointBegin || !pointEnd || pointEnd < pointBegin) continue;
            usize bytes = (usize)(pointEnd - pointBegin);
            if (bytes % 24u) continue;
            int pointCount = (int)(bytes / 24u);
            if (pointCount < 2 || pointCount > 100000) continue;
            if (!Readable(pointBegin, bytes)) continue;

            for (int si=0; si<pointCount-1; ++si)
            {
                const float* a = (const float*)(pointBegin + (usize)si * 24u);
                const float* b = (const float*)(pointBegin + (usize)(si+1) * 24u);
                ForceNodesSegmentRef ref;
                memset(&ref, 0, sizeof(ref));
                ref.structSize = sizeof(ref);
                ref.world = world;
                ref.path = path;
                ref.worldType = worldType;
                ref.pathClass = pathClass;
                ref.pathIndex = pi;
                ref.segmentIndex = si;
                ref.pointCount = pointCount;
                ref.a.x=a[0]; ref.a.y=a[1]; ref.a.z=a[2];
                ref.b.x=b[0]; ref.b.y=b[1]; ref.b.z=b[2];
                ++delivered;
                if (!visitor(&ref, userData)) return delivered;
            }
        }
    }
    return delivered;
}

static float Dot3(const ForceNodesVec3* a, const ForceNodesVec3* b)
{
    return a->x*b->x + a->y*b->y + a->z*b->z;
}

static int SplitSegment(const ForceNodesSegmentRef* segment, const ForceNodesVec3* position)
{
    if (!g_ready || !segment || !position || !segment->world || !segment->path) return 0;
    if (segment->segmentIndex < 0 || segment->segmentIndex >= segment->pointCount-1) return 0;
    if (!Readable((u8*)segment->world + 0x2b0, 24) || !Readable((u8*)segment->path + 0x8, 24)) return 0;

    void** pathsBegin = *(void***)((u8*)segment->world + 0x2b0);
    void** pathsEnd   = *(void***)((u8*)segment->world + 0x2b8);
    if (!pathsBegin || !pathsEnd || pathsEnd < pathsBegin) return 0;
    int pathCount = (int)(pathsEnd - pathsBegin);
    if (segment->pathIndex < 0 || segment->pathIndex >= pathCount) return 0;
    if (!Readable(pathsBegin + segment->pathIndex, sizeof(void*))) return 0;
    if (pathsBegin[segment->pathIndex] != segment->path) return 0;

    ForceNodesVec3 ab = { segment->b.x-segment->a.x, segment->b.y-segment->a.y, segment->b.z-segment->a.z };
    ForceNodesVec3 ap = { position->x-segment->a.x, position->y-segment->a.y, position->z-segment->a.z };
    float denom = Dot3(&ab,&ab);
    if (denom < 0.000001f) return 0;
    float t = Dot3(&ap,&ab) / denom;
    if (t <= 0.0001f || t >= 0.9999f) return 0;

    u8* record = g_core + 0x1315c0;
    u8* valid  = g_core + 0x91b3;
    u8 oldRecord[56];
    memcpy(oldRecord, record, sizeof(oldRecord));
    u8 oldValid = *valid;
    memset(record, 0, 56);
    *(void**)(record + 0x00) = segment->world;
    *(void**)(record + 0x08) = segment->path;
    *(int*)(record + 0x10) = segment->pathIndex;
    *(int*)(record + 0x14) = segment->segmentIndex + 1;
    *(int*)(record + 0x18) = segment->segmentIndex;
    *(int*)(record + 0x1c) = 2;
    *(int*)(record + 0x20) = 0;
    *(float*)(record + 0x24) = t;
    *(float*)(record + 0x28) = position->x;
    *(float*)(record + 0x2c) = position->y;
    *(float*)(record + 0x30) = position->z;
    *valid = 1;

    typedef u8 (*CreateFn)(void);
    CreateFn create = (CreateFn)(g_core + 0x4680);
    int ok = create() ? 1 : 0;

    memcpy(record, oldRecord, sizeof(oldRecord));
    *valid = oldValid;
    return ok;
}

static int RequestCursorOverride(const ForceNodesVec3* position)
{
    if (!g_ready || !position || !g_inPathHook || g_phase != (int)FORCE_NODES_PHASE_PATH_PREVIEW) return 0;
    g_overridePosition = *position;
    g_overrideRequested = 1;
    return 1;
}

static int RequestConnectionTarget(const ForceNodesSegmentRef* segment,
                                   const ForceNodesVec3* position,
                                   int splitNow,
                                   int requireNativeNode)
{
    if (!g_ready || !position || !g_inPathHook ||
        g_phase != (int)FORCE_NODES_PHASE_PATH_PREVIEW) return 0;
    if (requireNativeNode && !g_nativeBuilderGuardReady) return 0;
    if (splitNow && (!segment || !segment->world || !segment->path)) return 0;
    memset(&g_connectionSegment,0,sizeof(g_connectionSegment));
    if (segment) g_connectionSegment=*segment;
    g_connectionPosition=*position;
    g_connectionSplitNow=splitNow?1:0;
    g_connectionRequireNativeNode=requireNativeNode?1:0;
    g_connectionRequested=1;
    return 1;
}

static int CancelCurrentPathBuild(void)
{
    if (!g_ready || !g_inPathHook ||
        g_phase != (int)FORCE_NODES_PHASE_PATH_PREVIEW ||
        !g_nativeBuilderGuardReady) return 0;
    g_blockNativeBuildThisFrame=1;
    g_connectionStatus=FORCE_NODES_CONNECTION_REJECTED;
    return 1;
}


static char UpperAsciiChar(char c)
{
    return (c>='a' && c<='z') ? (char)(c-('a'-'A')) : c;
}

static int TextEquals(const char* a,const char* b)
{
    if (!a || !b) return 0;
    while (*a && *b)
    {
        if (*a!=*b) return 0;
        ++a; ++b;
    }
    return *a==0 && *b==0;
}

static int TextStarts(const char* text,const char* prefix)
{
    if (!text || !prefix) return 0;
    while (*prefix)
    {
        if (*text++!=*prefix++) return 0;
    }
    return 1;
}

static int TextLength(const char* text)
{
    int n=0; if (!text) return 0; while (text[n]) ++n; return n;
}

static void CopyText(char* dst,int cap,const char* src)
{
    if (!dst || cap<=0) return;
    int i=0;
    if (src) for (; i<cap-1 && src[i]; ++i) dst[i]=src[i];
    dst[i]=0;
}

static int ParseSmallNumber(const char* text)
{
    if (!text || !*text) return -1;
    int value=0;
    for (int i=0;text[i];++i)
    {
        if (text[i]<'0' || text[i]>'9') return -1;
        value=value*10+(text[i]-'0');
    }
    return value;
}

static int ParseModifierToken(const char* token,BindingModifier* out)
{
    if (!token || !out) return 0;
    if (TextEquals(token,"CTRL") || TextEquals(token,"CONTROL"))
        { out->first=0x1D; out->second=0x9D; return 1; }
    if (TextEquals(token,"LCTRL") || TextEquals(token,"LEFTCTRL") || TextEquals(token,"LEFTCONTROL"))
        { out->first=0x1D; out->second=0; return 1; }
    if (TextEquals(token,"RCTRL") || TextEquals(token,"RIGHTCTRL") || TextEquals(token,"RIGHTCONTROL"))
        { out->first=0x9D; out->second=0; return 1; }
    if (TextEquals(token,"SHIFT"))
        { out->first=0x2A; out->second=0x36; return 1; }
    if (TextEquals(token,"LSHIFT") || TextEquals(token,"LEFTSHIFT"))
        { out->first=0x2A; out->second=0; return 1; }
    if (TextEquals(token,"RSHIFT") || TextEquals(token,"RIGHTSHIFT"))
        { out->first=0x36; out->second=0; return 1; }
    if (TextEquals(token,"ALT"))
        { out->first=0x38; out->second=0xB8; return 1; }
    if (TextEquals(token,"LALT") || TextEquals(token,"LEFTALT"))
        { out->first=0x38; out->second=0; return 1; }
    if (TextEquals(token,"RALT") || TextEquals(token,"RIGHTALT"))
        { out->first=0xB8; out->second=0; return 1; }
    return 0;
}

static int LetterScanCode(char c)
{
    switch (c)
    {
        case 'A': return 0x1E; case 'B': return 0x30; case 'C': return 0x2E;
        case 'D': return 0x20; case 'E': return 0x12; case 'F': return 0x21;
        case 'G': return 0x22; case 'H': return 0x23; case 'I': return 0x17;
        case 'J': return 0x24; case 'K': return 0x25; case 'L': return 0x26;
        case 'M': return 0x32; case 'N': return 0x31; case 'O': return 0x18;
        case 'P': return 0x19; case 'Q': return 0x10; case 'R': return 0x13;
        case 'S': return 0x1F; case 'T': return 0x14; case 'U': return 0x16;
        case 'V': return 0x2F; case 'W': return 0x11; case 'X': return 0x2D;
        case 'Y': return 0x15; case 'Z': return 0x2C;
    }
    return 0;
}

static int DigitScanCode(char c)
{
    if (c>='1' && c<='9') return 0x02+(c-'1');
    if (c=='0') return 0x0B;
    return 0;
}

static int ParsePrimaryToken(const char* token,int* type,int* code)
{
    if (!token || !type || !code) return 0;
    *type=0; *code=0;

    if (TextEquals(token,"MOUSE1") || TextEquals(token,"LMB") || TextEquals(token,"LEFTMOUSE"))
        { *type=2; *code=1; return 1; }
    if (TextEquals(token,"MOUSE2") || TextEquals(token,"RMB") || TextEquals(token,"RIGHTMOUSE"))
        { *type=2; *code=2; return 1; }
    if (TextEquals(token,"MOUSE4") || TextEquals(token,"M4") || TextEquals(token,"X1"))
        { *type=2; *code=4; return 1; }
    if (TextEquals(token,"MOUSE5") || TextEquals(token,"M5") || TextEquals(token,"X2"))
        { *type=2; *code=5; return 1; }

    int len=TextLength(token);
    if (len==1)
    {
        int scan=LetterScanCode(token[0]);
        if (!scan) scan=DigitScanCode(token[0]);
        if (scan) { *type=1; *code=scan; return 1; }
    }

    if (TextStarts(token,"NUMPAD"))
    {
        const char* tail=token+6;
        if (tail[0]>='0' && tail[0]<='9' && tail[1]==0)
        {
            static const int scans[10]={0x52,0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49};
            *type=1; *code=scans[tail[0]-'0']; return 1;
        }
        if (TextEquals(tail,"ENTER"))  { *type=1; *code=0x9C; return 1; }
        if (TextEquals(tail,"PLUS"))   { *type=1; *code=0x4E; return 1; }
        if (TextEquals(tail,"MINUS"))  { *type=1; *code=0x4A; return 1; }
        if (TextEquals(tail,"STAR") || TextEquals(tail,"MULTIPLY")) { *type=1; *code=0x37; return 1; }
        if (TextEquals(tail,"SLASH") || TextEquals(tail,"DIVIDE")) { *type=1; *code=0xB5; return 1; }
        if (TextEquals(tail,"PERIOD") || TextEquals(tail,"DECIMAL")) { *type=1; *code=0x53; return 1; }
    }

    if (token[0]=='F')
    {
        int n=ParseSmallNumber(token+1);
        if (n>=1 && n<=10) { *type=1; *code=0x3A+n; return 1; }
        if (n==11) { *type=1; *code=0x57; return 1; }
        if (n==12) { *type=1; *code=0x58; return 1; }
    }

    struct NamedKey { const char* name; int scan; };
    static const NamedKey named[] = {
        {"ESC",0x01},{"ESCAPE",0x01},{"TAB",0x0F},{"ENTER",0x1C},
        {"SPACE",0x39},{"SPACEBAR",0x39},{"CAPS",0x3A},{"CAPSLOCK",0x3A},
        {"BACKSPACE",0x0E},{"MINUS",0x0C},{"EQUALS",0x0D},
        {"LBRACKET",0x1A},{"RBRACKET",0x1B},{"BACKSLASH",0x2B},
        {"SEMICOLON",0x27},{"APOSTROPHE",0x28},{"GRAVE",0x29},
        {"COMMA",0x33},{"PERIOD",0x34},{"SLASH",0x35},
        {"HOME",0xC7},{"UP",0xC8},{"PAGEUP",0xC9},{"PGUP",0xC9},
        {"LEFT",0xCB},{"RIGHT",0xCD},{"END",0xCF},{"DOWN",0xD0},
        {"PAGEDOWN",0xD1},{"PGDOWN",0xD1},{"PGDN",0xD1},
        {"INSERT",0xD2},{"INS",0xD2},{"DELETE",0xD3},{"DEL",0xD3},
        {"NUMLOCK",0x45},{"SCROLLLOCK",0x46},{"LWIN",0xDB},{"RWIN",0xDC}
    };
    for (unsigned i=0;i<sizeof(named)/sizeof(named[0]);++i)
        if (TextEquals(token,named[i].name)) { *type=1; *code=named[i].scan; return 1; }
    return 0;
}

static int ParseBinding(const char* source,InputBinding* out)
{
    if (!source || !out) return 0;
    memset(out,0,sizeof(*out));
    char normalized[64];
    int n=0;
    for (int i=0;source[i] && n<(int)sizeof(normalized)-1;++i)
    {
        char c=source[i];
        if (c==' ' || c=='\t' || c=='\r' || c=='\n') continue;
        normalized[n++]=UpperAsciiChar(c);
    }
    normalized[n]=0;
    CopyText(out->text,(int)sizeof(out->text),normalized);
    if (!normalized[0]) return 0;
    if (TextEquals(normalized,"NONE"))
    {
        out->valid=1; out->disabled=1; return 1;
    }

    char work[64]; CopyText(work,(int)sizeof(work),normalized);
    char* token=work;
    for (int i=0;;++i)
    {
        char c=work[i];
        if (c=='+' || c==0)
        {
            work[i]=0;
            if (!*token) return 0;
            BindingModifier mod;
            int primaryType=0,primaryCode=0;
            if (ParseModifierToken(token,&mod))
            {
                if (out->modifierCount>=3 || out->primaryType) return 0;
                out->modifiers[out->modifierCount++]=mod;
            }
            else if (ParsePrimaryToken(token,&primaryType,&primaryCode))
            {
                if (out->primaryType) return 0;
                out->primaryType=primaryType;
                out->primaryCode=primaryCode;
            }
            else return 0;
            if (c==0) break;
            token=work+i+1;
        }
    }
    if (!out->primaryType) return 0;
    out->valid=1;
    return 1;
}

static void LoadBinding(const char* key,const char* fallback,InputBinding* out)
{
    char value[64]; value[0]=0;
    H->configString("plugins\\ForceNodes.ini","ForceNodes",key,value,(int)sizeof(value),fallback);
    if (ParseBinding(value,out)) return;
    H->log("ForceNodes  invalid binding %s=%s; using %s",key,value,fallback);
    ParseBinding(fallback,out);
}

static int OriginalKeyDown(void* input,int scan)
{
    return g_originalKeyFn && input && scan>0 && scan<=255 && g_originalKeyFn(input,scan) ? 1 : 0;
}

static int BindingDown(void* input,const InputBinding* binding)
{
    if (!input || !binding || !binding->valid || binding->disabled) return 0;
    for (int i=0;i<binding->modifierCount;++i)
    {
        const BindingModifier* mod=&binding->modifiers[i];
        if (!OriginalKeyDown(input,mod->first) &&
            !(mod->second && OriginalKeyDown(input,mod->second))) return 0;
    }
    if (binding->primaryType==1) return OriginalKeyDown(input,binding->primaryCode);
    if (binding->primaryType==2)
    {
        CoreMouseFn fn=0;
        if (binding->primaryCode==1) fn=g_originalMouseLeftFn;
        else if (binding->primaryCode==2) fn=g_originalMouseRightFn;
        else if (binding->primaryCode==4) fn=g_originalMouseX1Fn;
        else if (binding->primaryCode==5) fn=g_originalMouseX2Fn;
        return fn && fn(input) ? 1 : 0;
    }
    return 0;
}

static u8 BoundKeyQuery(void* input,int scan)
{
    if (scan==FN_BIND_SENTINEL_MODIFIER)
        return (u8)(BindingDown(input,&g_bindOverlay) || BindingDown(input,&g_bindForce) || BindingDown(input,&g_bindGrid));
    if (scan==FN_BIND_SENTINEL_OVERLAY) return (u8)BindingDown(input,&g_bindOverlay);
    if (scan==FN_BIND_SENTINEL_FORCE)   return (u8)BindingDown(input,&g_bindForce);
    if (scan==FN_BIND_SENTINEL_GRID)    return (u8)BindingDown(input,&g_bindGrid);
    return g_originalKeyFn ? g_originalKeyFn(input,scan) : 0;
}

static u8 BoundAddQuery(void* input)    { return (u8)BindingDown(input,&g_bindAdd); }
static u8 BoundRemoveQuery(void* input) { return (u8)BindingDown(input,&g_bindRemove); }

static int WriteCompactHudRow(void)
{
    if (!g_core || !Readable(g_core+0x131800,0x30)) return 0;
    static const u16 text[]={'A','D','D',' ','/',' ','R','E','M','O','V','E',' ','N','O','D','E','S',0};
    u8* row=g_core+0x131800;
    memset(row,0,0x28);
    int i=0; while (text[i] && i<19) { ((u16*)row)[i]=text[i]; ++i; }
    ((u16*)row)[i]=0;
    *(u8*)(row+0x28)=0;
    return 1;
}

static int InstallCustomBindings(void)
{
    if (!g_core || !Readable(g_core+0x9008,0x128)) return 0;
    g_originalKeyFn=*(CoreKeyFn*)(g_core+0x9108);
    g_originalMouseLeftFn=*(CoreMouseFn*)(g_core+0x9110);
    g_originalMouseRightFn=*(CoreMouseFn*)(g_core+0x9118);
    g_originalMouseX1Fn=*(CoreMouseFn*)(g_core+0x9120);
    g_originalMouseX2Fn=*(CoreMouseFn*)(g_core+0x9128);
    if (!g_originalKeyFn || !g_originalMouseLeftFn || !g_originalMouseRightFn ||
        !g_originalMouseX1Fn || !g_originalMouseX2Fn) return 0;

    *(int*)(g_core+0x9008)=FN_BIND_SENTINEL_MODIFIER;
    *(int*)(g_core+0x900c)=FN_BIND_SENTINEL_OVERLAY;
    *(int*)(g_core+0x9010)=FN_BIND_SENTINEL_FORCE;
    *(int*)(g_core+0x9014)=FN_BIND_SENTINEL_GRID;
    *(int*)(g_core+0x9018)=0; /* recalibration action retired */
    *(CoreKeyFn*)(g_core+0x9108)=BoundKeyQuery;
    *(CoreMouseFn*)(g_core+0x9120)=BoundAddQuery;
    *(CoreMouseFn*)(g_core+0x9128)=BoundRemoveQuery;
    WriteCompactHudRow();
    g_customBindingsReady=1;
    return 1;
}

static int GetKeyDown(int dikScanCode)
{
    if (!g_core || dikScanCode<0 || dikScanCode>255) return 0;
    typedef u8 (*KeyFn)(void*,int);
    KeyFn fn=*(KeyFn*)(g_core+0x9108);
    void* input=*(void**)(g_core+0x9148);
    if (!fn || !input) return 0;
    return fn(input,dikScanCode)?1:0;
}



static const ForceNodesApiV1 g_apiV1 = {
    sizeof(ForceNodesApiV1), FORCE_NODES_API_VERSION_V1,
    RegisterFrameClient, EnumerateSegments, SplitSegment, RequestCursorOverride,
    ServiceReady
};

static const ForceNodesApiV2 g_apiV2 = {
    sizeof(ForceNodesApiV2), FORCE_NODES_API_VERSION_V2,
    RegisterFrameClient, EnumerateSegments, SplitSegment, RequestCursorOverride,
    ServiceReady, PathPreviewCapabilities
};

static const ForceNodesApiV3 g_apiV3 = {
    sizeof(ForceNodesApiV3), FORCE_NODES_API_VERSION_V3,
    RegisterFrameClient, EnumerateSegments, SplitSegment, RequestCursorOverride,
    ServiceReady, PathPreviewCapabilities, RequestConnectionTarget,
    CancelCurrentPathBuild, GetKeyDown
};


static const ForceNodesApi g_api = {
    sizeof(ForceNodesApi), FORCE_NODES_API_VERSION,
    RegisterFrameClient, EnumerateSegments, SplitSegment, RequestCursorOverride,
    ServiceReady, PathPreviewCapabilities, RequestConnectionTarget,
    CancelCurrentPathBuild, GetKeyDown
};

static int MouseLeftPressed(void)
{
    if (!g_core) return 0;
    typedef u8 (*MouseFn)(void*);
    MouseFn fn = *(MouseFn*)(g_core + 0x9110);
    void* input = *(void**)(g_core + 0x9148);
    if (!fn || !input) return 0;
    return fn(input) ? 1 : 0;
}

static int MouseRightPressed(void)
{
    if (!g_core) return 0;
    typedef u8 (*MouseFn)(void*);
    MouseFn fn = *(MouseFn*)(g_core + 0x9118);
    void* input = *(void**)(g_core + 0x9148);
    if (!fn || !input) return 0;
    return fn(input) ? 1 : 0;
}

static const char* ReadToolName(void* construction)
{
    g_toolName[0] = 0;
    if (!construction || !Readable((u8*)construction + g_toolNameOffset, sizeof(void*))) return g_toolName;
    const char* p = *(const char**)((u8*)construction + g_toolNameOffset);
    if (!p) return g_toolName;
    int i=0;
    for (; i<(int)sizeof(g_toolName)-1; ++i)
    {
        if (!Readable(p+i,1)) break;
        char c=p[i];
        g_toolName[i]=c;
        if (!c) return g_toolName;
    }
    g_toolName[i]=0;
    return g_toolName;
}

static void DispatchClients(ForceNodesFrameContext* ctx)
{
    for (int i=0;i<g_clientCount;++i)
        if (g_clients[i].fn) g_clients[i].fn(ctx, g_clients[i].user);
}

static u8* FindPatternUnique(const u8* pattern, const char* mask, usize length, int* countOut)
{
    if (countOut) *countOut=0;
    if (!H || !H->exeBase || H->exeSize<length) return 0;
    u8* found=0;
    int count=0;
    for (usize i=0; i+length<=H->exeSize; ++i)
    {
        int ok=1;
        for (usize j=0;j<length;++j)
        {
            if (mask[j]=='x' && H->exeBase[i+j]!=pattern[j]) { ok=0; break; }
        }
        if (ok)
        {
            found=H->exeBase+i;
            ++count;
            if (count>1) break;
        }
    }
    if (countOut) *countOut=count;
    return count==1 ? found : 0;
}

static u8* ResolveBuilder(const u8* pattern, const char* mask, usize length,
                          int configuredRva, const char* label)
{
    int count=0;
    u8* p=FindPatternUnique(pattern,mask,length,&count);
    if (p)
    {
        if (g_logActions) H->log("ForceNodes  signature ok      %s exe+0x%llX",label,(usize)(p-H->exeBase));
        return p;
    }
    if (configuredRva>0 && (usize)configuredRva+15u<=H->exeSize)
    {
        p=H->exeBase+(usize)configuredRva;
        if (Readable(p,15))
        {
            H->log("ForceNodes  %s signature count=%d; using configured exe+0x%X",label,count,configuredRva);
            return p;
        }
    }
    H->log("ForceNodes  signature FAILED  %s matches=%d",label,count);
    return 0;
}

/* ForceNodes-DrawStraightConnections deliberately hooks the same native path
   builder before ForceNodes starts. Its 21-byte Tesmio detour hides the normal
   function-entry signature. Recover only when the current-build RVA contains
   the loader's absolute jump and the untouched bytes immediately after the
   stolen prologue still match the native signature. This chains the existing
   detour instead of treating it as a game-build mismatch. */
static u8* RecoverDetouredNativePathBuilder(const u8* originalPattern,usize length)
{
    const usize knownRva=0x4FCA20u;
    const usize stolen=21u;
    if (!originalPattern || length<=stolen || knownRva+length>H->exeSize) return 0;
    u8* p=H->exeBase+knownRva;
    if (!Readable(p,length)) return 0;
    if (p[0]!=0xFF || p[1]!=0x25 || p[2] || p[3] || p[4] || p[5]) return 0;
    if (memcmp(p+stolen,originalPattern+stolen,length-stolen)!=0) return 0;
    H->log("ForceNodes  native path builder already detoured; chaining compatible hook at exe+0x4FCA20");
    return p;
}

static void RefreshCoreWorldCache(void* construction, const ForceNodesVec3* cursor)
{
    if (!g_core || !construction || !cursor) return;
    typedef void (*ScanFn)(void*, const ForceNodesVec3*);
    ScanFn scan=(ScanFn)(g_core+0x3e20);
    scan(construction,cursor);
}

static float Distance2XZ(const ForceNodesVec3* a,const ForceNodesVec3* b)
{
    float dx=a->x-b->x;
    float dz=a->z-b->z;
    return dx*dx+dz*dz;
}

static void RestoreFrameCursorOverride(void)
{
    if (!g_frameOverrideActive || !g_frameOverrideConstruction)
    {
        g_frameOverrideActive=0;
        g_frameOverrideConstruction=0;
        return;
    }
    u8* construction=(u8*)g_frameOverrideConstruction;
    if (g_frameHaveCursor && Readable(construction+g_cursorOffset,sizeof(g_frameOriginalCursor)))
        *(ForceNodesVec3*)(construction+g_cursorOffset)=g_frameOriginalCursor;
    if (g_frameHaveRaw && Readable(construction+g_rawCursorOffset,sizeof(g_frameOriginalRaw)))
        *(ForceNodesVec3*)(construction+g_rawCursorOffset)=g_frameOriginalRaw;
    g_frameOverrideActive=0;
    g_frameOverrideConstruction=0;
    g_frameHaveCursor=0;
    g_frameHaveRaw=0;
}

static int ApplyFrameCursorOverride(void* construction,const ForceNodesVec3* position)
{
    if (!construction || !position) return 0;
    u8* c=(u8*)construction;
    int haveCursor=Readable(c+g_cursorOffset,sizeof(ForceNodesVec3));
    int haveRaw=Readable(c+g_rawCursorOffset,sizeof(ForceNodesVec3));
    if (!haveCursor && !haveRaw) return 0;

    if (g_frameOverrideActive && g_frameOverrideConstruction!=construction)
        RestoreFrameCursorOverride();
    if (!g_frameOverrideActive)
    {
        g_frameOverrideConstruction=construction;
        g_frameHaveCursor=haveCursor;
        g_frameHaveRaw=haveRaw;
        if (haveCursor) g_frameOriginalCursor=*(ForceNodesVec3*)(c+g_cursorOffset);
        else memset(&g_frameOriginalCursor,0,sizeof(g_frameOriginalCursor));
        if (haveRaw) g_frameOriginalRaw=*(ForceNodesVec3*)(c+g_rawCursorOffset);
        else g_frameOriginalRaw=g_frameOriginalCursor;
        g_frameOverrideActive=1;
    }
    if (haveCursor) *(ForceNodesVec3*)(c+g_cursorOffset)=*position;
    if (haveRaw) *(ForceNodesVec3*)(c+g_rawCursorOffset)=*position;
    g_frameEffectiveCursor=*position;
    return 1;
}

static int NativeNodeAtOffset(void* construction,int offset,const ForceNodesVec3* position)
{
    if (!construction || offset<=0 || !Readable((u8*)construction+offset,sizeof(void*))) return 0;
    u8* node=*(u8**)((u8*)construction+offset);
    if (!node || !Readable(node+4,sizeof(ForceNodesVec3))) return 0;
    ForceNodesVec3 p=*(ForceNodesVec3*)(node+4);
    return Distance2XZ(&p,position)<=g_nativeNodeTolerance*g_nativeNodeTolerance;
}

static int NativeSelectedNodeMatches(void* construction,const ForceNodesVec3* position)
{
    /* f650 is the ordinary shared path node selected by both road and
       pedestrian builders. f690 is the pedestrian shared/connection-node
       selector. Deliberately do not accept f648/f680: those can represent a
       plain inline path point, which is precisely the unconnected-stub case. */
    return NativeNodeAtOffset(construction,g_selectedNativeNodeOffset,position) ||
           NativeNodeAtOffset(construction,g_selectedSharedNodeOffset,position);
}


static void* NativeNodePointerAtOffsetWithin(void* construction,int offset,
                                             const ForceNodesVec3* position,
                                             float tolerance)
{
    if (!construction || offset<=0 || !Readable((u8*)construction+offset,sizeof(void*))) return 0;
    u8* node=*(u8**)((u8*)construction+offset);
    if (!node || !Readable(node+4,sizeof(ForceNodesVec3))) return 0;
    ForceNodesVec3 p=*(ForceNodesVec3*)(node+4);
    if (Distance2XZ(&p,position)>tolerance*tolerance) return 0;
    return node;
}

static void* NativeSelectedNodePointerWithin(void* construction,
                                             const ForceNodesVec3* position,
                                             float tolerance)
{
    void* node=NativeNodePointerAtOffsetWithin(construction,g_selectedNativeNodeOffset,position,tolerance);
    if (node) return node;
    return NativeNodePointerAtOffsetWithin(construction,g_selectedSharedNodeOffset,position,tolerance);
}


static float SqrtApprox(float x)
{
    if (x<=0.0f) return 0.0f;
    float r=x>1.0f?x:1.0f;
    for (int i=0;i<8;++i) r=0.5f*(r+x/r);
    return r;
}

static float Distance2PointSegmentXZ(const ForceNodesVec3* p,
                                     const ForceNodesVec3* a,
                                     const ForceNodesVec3* b,
                                     float* tOut)
{
    float dx=b->x-a->x,dz=b->z-a->z;
    float denom=dx*dx+dz*dz;
    float t=0.0f;
    if (denom>0.000001f)
    {
        t=((p->x-a->x)*dx+(p->z-a->z)*dz)/denom;
        if (t<0.0f) t=0.0f;
        if (t>1.0f) t=1.0f;
    }
    if (tOut) *tOut=t;
    float qx=a->x+dx*t,qz=a->z+dz*t;
    float ex=p->x-qx,ez=p->z-qz;
    return ex*ex+ez*ez;
}

static int SameBuildingConnection(const BuildingProposal* p,
                                  const ForceNodesBuildingConnectionRef* c)
{
    if (!p || !c || !p->used) return 0;
    if (p->pathKind!=c->pathKind || p->worldType!=c->worldType ||
        p->pathClass!=c->pathClass || p->pathSubtype!=c->pathSubtype ||
        p->originAtStart!=c->originAtStart || p->world!=c->world) return 0;
    return Distance2XZ(&p->origin,&c->origin)<=
           g_buildingOriginTolerance*g_buildingOriginTolerance;
}

static BuildingProposal* FindBuildingProposal(BuildingProposal* entries,int count,
                                               const ForceNodesBuildingConnectionRef* c)
{
    for (int i=0;i<count;++i)
        if (SameBuildingConnection(&entries[i],c)) return &entries[i];
    return 0;
}

static BuildingProposal* FindOrAddBuildingWork(const ForceNodesBuildingConnectionRef* c)
{
    BuildingProposal* p=FindBuildingProposal(g_buildingWork,g_buildingWorkCount,c);
    if (p) return p;
    if (g_buildingWorkCount>=g_maxBuildingConnections ||
        g_buildingWorkCount>=MAX_BUILDING_CONNECTIONS) return 0;
    p=&g_buildingWork[g_buildingWorkCount++];
    memset(p,0,sizeof(*p));
    p->used=1;
    p->pathKind=c->pathKind;
    p->worldType=c->worldType;
    p->pathClass=c->pathClass;
    p->pathSubtype=c->pathSubtype;
    p->originAtStart=c->originAtStart;
    p->world=c->world;
    p->buildingCenter=c->buildingCenter;
    p->origin=c->origin;
    return p;
}

static int FiniteFloat(float value)
{
    union FloatBits { float f; u32 u; } bits;
    bits.f=value;
    return (bits.u&0x7f800000u)!=0x7f800000u;
}

static int ValidPosition(const ForceNodesVec3* p)
{
    return p && FiniteFloat(p->x) && FiniteFloat(p->y) && FiniteFloat(p->z) &&
           p->x>-200000.0f && p->x<200000.0f &&
           p->y>-200000.0f && p->y<200000.0f &&
           p->z>-200000.0f && p->z<200000.0f;
}

static int ReadEndpointPosition(u8* world,int offset,ForceNodesVec3* position,void** nodeOut)
{
    if (nodeOut) *nodeOut=0;
    if (!world || !position || !Readable(world+offset,sizeof(void*))) return 0;
    u8* node=*(u8**)(world+offset);
    if (!node || !Readable(node+4,sizeof(ForceNodesVec3))) return 0;
    ForceNodesVec3 p=*(ForceNodesVec3*)(node+4);
    if (!ValidPosition(&p)) return 0;
    *position=p;
    if (nodeOut) *nodeOut=node;
    return 1;
}

/* Native path-builder ABI:
     arg1 = path world
     arg2 = pointer to one C3DVECTOR3 target (NOT a path object)
     arg3 = native path class (10 road-building connector, 20 pedestrian)
     arg4 = native subtype
     arg8 = optional vector descriptor of 24-byte path-control records

   V1.3.0-v1.3.2 incorrectly interpreted arg2 as a large path descriptor and
   read class/node/vector fields hundreds of bytes beyond the target vector.
   Those reads generally landed on unrelated stack memory, so every automatic
   building connector was silently ignored even though all hooks were ready.

   The building socket is the native start endpoint at world+0x428. If arg8
   contains control records, the first point away from that socket supplies the
   true local tangent. Otherwise the socket-to-building-centre radial direction
   is used. Preview never changes arg8 and never creates a topology node. */
static int ReadPathControlDirection(usize pathControls,
                                    const ForceNodesVec3* origin,
                                    float* dxOut,float* dzOut)
{
    if (!pathControls || !origin || !dxOut || !dzOut ||
        !Readable((const void*)pathControls,24)) return 0;
    const u8* descriptor=(const u8*)pathControls;
    const u8* begin=*(const u8* const*)(descriptor+0);
    const u8* end=*(const u8* const*)(descriptor+8);
    const u8* capacity=*(const u8* const*)(descriptor+16);
    if (!begin || !end || end<begin || capacity<end) return 0;
    usize bytes=(usize)(end-begin);
    if (!bytes || (bytes%24u)!=0 || bytes>24u*MAX_BUILDING_PATH_POINTS ||
        !Readable(begin,bytes)) return 0;
    unsigned count=(unsigned)(bytes/24u);
    unsigned inspect=count<8u?count:8u;
    for (unsigned i=0;i<inspect;++i)
    {
        const ForceNodesVec3* point=(const ForceNodesVec3*)(begin+(usize)i*24u);
        if (!ValidPosition(point)) continue;
        float dx=point->x-origin->x;
        float dz=point->z-origin->z;
        float len2=dx*dx+dz*dz;
        if (len2>=0.0625f && len2<=250000.0f)
        {
            *dxOut=dx; *dzOut=dz;
            return 1;
        }
    }
    return 0;
}

static int ParseBuildingConnection(void* world,const void* targetData,
                                   u32 nativePathClass,u32 nativePathSubtype,
                                   usize pathControls,
                                   ForceNodesBuildingConnectionRef* out)
{
    if (!world || !targetData || !out || !g_buildingConstruction) return 0;
    if (nativePathClass!=10u && nativePathClass!=20u) return 0;
    u8* w=(u8*)world;
    if (!Readable(targetData,sizeof(ForceNodesVec3)) || !Readable(w+0x258,4)) return 0;

    ForceNodesVec3 target=*(const ForceNodesVec3*)targetData;
    if (!ValidPosition(&target)) return 0;

    ForceNodesVec3 origin={0,0,0};
    void* startNode=0;
    if (!ReadEndpointPosition(w,0x428,&origin,&startNode)) return 0;

    float dx=0.0f,dz=0.0f;
    int haveControlDirection=ReadPathControlDirection(pathControls,&origin,&dx,&dz);
    float rx=origin.x-g_buildingCenter.x;
    float rz=origin.z-g_buildingCenter.z;
    float radialLen2=rx*rx+rz*rz;
    float directionLen2=dx*dx+dz*dz;
    if (!haveControlDirection || directionLen2<0.0025f)
    {
        if (radialLen2>=0.0025f) { dx=rx; dz=rz; }
        else { dx=target.x-origin.x; dz=target.z-origin.z; }
        directionLen2=dx*dx+dz*dz;
    }
    if (directionLen2<0.0025f) return 0;

    float directionLen=SqrtApprox(directionLen2);
    dx/=directionLen; dz/=directionLen;
    if (radialLen2>=0.0025f && dx*rx+dz*rz<0.0f)
    {
        dx=-dx; dz=-dz;
    }

    memset(out,0,sizeof(*out));
    out->structSize=sizeof(*out);
    out->constructionObject=g_buildingConstruction;
    out->world=world;
    out->pathData=(void*)targetData;
    out->worldType=*(int*)(w+0x258);
    out->pathClass=(int)nativePathClass;
    out->pathSubtype=(int)nativePathSubtype;
    out->pointCount=2;
    out->pathKind=nativePathClass==20u?FORCE_NODES_PATH_PEDESTRIAN:
                                          FORCE_NODES_PATH_ROAD;
    out->originAtStart=1;
    out->buildingCenter=g_buildingCenter;
    out->origin=origin;
    out->outwardNeighbour=origin;
    out->outwardNeighbour.x+=dx;
    out->outwardNeighbour.z+=dz;
    out->currentTarget=target;
    if (!g_buildingConnectorStreamLogged && g_logActions)
    {
        H->log("ForceNodes  native building connector stream detected: class=%u worldType=%d socket=%.2f,%.2f target=%.2f,%.2f tangent=%s",
               nativePathClass,out->worldType,origin.x,origin.z,target.x,target.z,
               haveControlDirection?"control-points":"building-radial");
        g_buildingConnectorStreamLogged=1;
    }
    return 1;
}

static void RecordBuildingDecision(const ForceNodesBuildingConnectionRef* c)
{
    BuildingProposal* p=FindOrAddBuildingWork(c);
    if (!p) return;
    if (g_buildingRequestKind==1)
    {
        p->managed=1;
        p->hasTarget=1;
        p->rejected=0;
        p->needsSplit=g_buildingRequestedNeedsSplit;
        p->target=g_buildingRequestedTarget;
        p->segment=g_buildingRequestedSegment;
    }
    else if (g_buildingRequestKind==2 && !p->hasTarget)
    {
        p->managed=1;
        p->rejected=1;
    }
}

static int DispatchBuildingConnection(const ForceNodesBuildingConnectionRef* c)
{
    if (!c) return 0;
    ForceNodesFrameContext ctx;
    memset(&ctx,0,sizeof(ctx));
    ctx.structSize=sizeof(ctx);
    ctx.phase=FORCE_NODES_PHASE_BUILDING_PREVIEW;
    ctx.constructionObject=g_buildingConstruction;
    ctx.cursor=c->currentTarget;
    ctx.rawCursor=c->currentTarget;
    ctx.effectiveCursor=c->currentTarget;
    ctx.pathKind=c->pathKind;
    ctx.toolName=ReadToolName(g_buildingConstruction);
    ctx.contextFlags=FORCE_NODES_CONTEXT_BUILDING_AUTOCONNECTION;
    ctx.buildingPreviewSerial=g_buildingSerial;
    ctx.buildingConnection=c;

    g_buildingRequestKind=0;
    g_buildingRequestedNeedsSplit=0;
    memset(&g_buildingRequestedSegment,0,sizeof(g_buildingRequestedSegment));
    g_currentBuildingConnection=c;
    g_phase=(int)ctx.phase;
    DispatchClients(&ctx);
    g_phase=-1;
    g_currentBuildingConnection=0;
    RecordBuildingDecision(c);
    return g_buildingRequestKind;
}

/* Call the native builder with a stack-local target vector. The optional
   arg8 path-control vector is preserved exactly. It is never substituted with
   a node pointer; the native builder resolves its endpoint from the exact target
   after ForceNodes has split and refreshed the receiving segment. */
static void CallNativeWithStraightConnection(void* world,void* data,u32 p3,u32 p4,
                                             usize p5,usize p6,usize p7,usize p8,usize p9,usize p10,
                                             const ForceNodesVec3* target)
{
    if (!g_nativePathBuilder || !data || !target) return;
    ForceNodesVec3 forcedTarget=*target;
    g_nativePathBuilder(world,&forcedTarget,p3,p4,p5,p6,p7,p8,p9,p10);
}

static int NativeBuildingEndpointMatches(void* world,void* preparedNode,
                                         const ForceNodesVec3* target)
{
    if (!world || !preparedNode || !target ||
        !Readable((u8*)world+0x430,sizeof(void*))) return 0;
    void* endpoint=*(void**)((u8*)world+0x430);
    if (!endpoint || endpoint!=preparedNode ||
        !Readable((u8*)endpoint+4,sizeof(ForceNodesVec3))) return 0;
    return Distance2XZ((ForceNodesVec3*)((u8*)endpoint+4),target)<=
           g_buildingNodeTolerance*g_buildingNodeTolerance;
}

typedef struct SegmentResolveSearch
{
    const BuildingProposal* proposal;
    int found;
    float bestDistance2;
    ForceNodesSegmentRef best;
} SegmentResolveSearch;

static int ResolveSegmentVisitor(const ForceNodesSegmentRef* seg,void* user)
{
    SegmentResolveSearch* s=(SegmentResolveSearch*)user;
    const BuildingProposal* p=s->proposal;
    if (seg->world!=p->segment.world || seg->worldType!=p->worldType ||
        seg->pathClass==10 || seg->pathClass==20) return 1;
    if (seg->pathClass!=p->segment.pathClass) return 1;
    float t=0.0f;
    float d=Distance2PointSegmentXZ(&p->target,&seg->a,&seg->b,&t);
    if (d>g_buildingSegmentResolveTolerance*g_buildingSegmentResolveTolerance) return 1;
    if (!s->found || d<s->bestDistance2)
    {
        s->found=1;
        s->bestDistance2=d;
        s->best=*seg;
    }
    return 1;
}

static int ResolveCurrentSegment(const BuildingProposal* p,ForceNodesSegmentRef* out)
{
    SegmentResolveSearch search;
    memset(&search,0,sizeof(search));
    search.proposal=p;
    search.bestDistance2=3.4e38f;
    EnumerateSegments(ResolveSegmentVisitor,&search);
    if (!search.found) return 0;
    *out=search.best;
    return 1;
}

static void* SelectNativeNodeAt(void* construction,unsigned pathKind,
                                const ForceNodesVec3* target)
{
    if (!construction || !target) return 0;
    u8* c=(u8*)construction;
    int haveCursor=Readable(c+g_cursorOffset,sizeof(ForceNodesVec3));
    int haveRaw=Readable(c+g_rawCursorOffset,sizeof(ForceNodesVec3));
    if (!haveCursor && !haveRaw) return 0;
    ForceNodesVec3 oldCursor={0,0,0},oldRaw={0,0,0};
    if (haveCursor) { oldCursor=*(ForceNodesVec3*)(c+g_cursorOffset); *(ForceNodesVec3*)(c+g_cursorOffset)=*target; }
    if (haveRaw) { oldRaw=*(ForceNodesVec3*)(c+g_rawCursorOffset); *(ForceNodesVec3*)(c+g_rawCursorOffset)=*target; }
    RefreshCoreWorldCache(construction,target);

    if (pathKind==FORCE_NODES_PATH_ROAD && g_roadBuilder)
        g_roadBuilder(construction,0,1,0,0,0,0,0.0f,0,0);
    else if (pathKind==FORCE_NODES_PATH_PEDESTRIAN && g_pedBuilder)
        g_pedBuilder(construction,0,1,0,0,0);

    void* node=NativeSelectedNodePointerWithin(construction,target,g_buildingNodeTolerance);
    if (haveCursor) *(ForceNodesVec3*)(c+g_cursorOffset)=oldCursor;
    if (haveRaw) *(ForceNodesVec3*)(c+g_rawCursorOffset)=oldRaw;
    return node;
}

static int SamePreparedTarget(const BuildingProposal* a,const BuildingProposal* b)
{
    if (!a || !b || a->segment.world!=b->segment.world ||
        a->worldType!=b->worldType) return 0;
    return Distance2XZ(&a->target,&b->target)<=
           g_buildingTargetMergeTolerance*g_buildingTargetMergeTolerance;
}

static int PrepareBuildingCommit(void* construction)
{
    int managed=0;
    for (int i=0;i<g_buildingPreviewCount;++i)
    {
        BuildingProposal* p=&g_buildingPreview[i];
        p->prepared=0;
        p->preparedNode=0;
        if (!p->managed) continue;
        ++managed;
        if (p->rejected || !p->hasTarget)
        {
            if (g_logActions)
                H->log("ForceNodes  building placement BLOCKED: straight connector has no safe receiving segment");
            return 0;
        }
        if ((p->pathKind==FORCE_NODES_PATH_ROAD && !g_roadBuilder) ||
            (p->pathKind==FORCE_NODES_PATH_PEDESTRIAN && !g_pedBuilder)) return 0;
    }
    if (!managed) return 2;

    /* First create every required topology point. Preview never reaches this
       loop, and duplicate connector targets are split only once. */
    for (int i=0;i<g_buildingPreviewCount;++i)
    {
        BuildingProposal* p=&g_buildingPreview[i];
        if (!p->managed || !p->needsSplit) continue;
        /* A prior interrupted/failed commit may already have left the exact
           safe node behind. Reuse only an essentially exact native node; never
           split the same target again and never accept a nearby vanilla node. */
        if (SelectNativeNodeAt(construction,p->pathKind,&p->target)) continue;
        int duplicate=0;
        for (int j=0;j<i;++j)
            if (g_buildingPreview[j].managed && SamePreparedTarget(p,&g_buildingPreview[j]))
            { duplicate=1; break; }
        if (duplicate) continue;

        ForceNodesSegmentRef current;
        if (!ResolveCurrentSegment(p,&current) || !SplitSegment(&current,&p->target))
        {
            if (g_logActions)
                H->log("ForceNodes  building placement BLOCKED: real split node could not be created at %.2f, %.2f",p->target.x,p->target.z);
            return 0;
        }
        RefreshCoreWorldCache(construction,&p->target);
    }

    /* Resolve all endpoint pointers after every split, so a later split cannot
       invalidate a pointer prepared for an earlier connector. */
    for (int i=0;i<g_buildingPreviewCount;++i)
    {
        BuildingProposal* p=&g_buildingPreview[i];
        if (!p->managed) continue;
        p->preparedNode=SelectNativeNodeAt(construction,p->pathKind,&p->target);
        if (!p->preparedNode)
        {
            if (g_logActions)
                H->log("ForceNodes  building placement BLOCKED: target did not resolve to a genuine native shared node");
            return 0;
        }
        p->prepared=1;
    }
    return 1;
}

static BuildingProposal* MatchPreparedBuildingConnection(const ForceNodesBuildingConnectionRef* c)
{
    BuildingProposal* p=FindBuildingProposal(g_buildingPreview,g_buildingPreviewCount,c);
    if (!p || !p->managed) return 0;
    return p;
}

static int BuildingPreviewCacheMatches(void* buildingType,const ForceNodesVec3* center)
{
    if (g_buildingPreviewCount<=0 || g_buildingPreviewType!=buildingType) return 0;
    return Distance2XZ(&g_buildingPreviewCenter,center)<=
           g_buildingCachePositionTolerance*g_buildingCachePositionTolerance;
}

static NOINLINE void BuildingPlacementDetour(void* construction,void* buildingType,u8 p3,u8 previewMode)
{
    if (!g_buildingPlacement) return;
    if (g_inBuildingPlacement || !g_straightModeEnabled || !g_buildingHookReady)
    {
        g_buildingPlacement(construction,buildingType,p3,previewMode);
        return;
    }
    ForceNodesVec3 center={0,0,0};
    if (!construction || !Readable((u8*)construction+g_cursorOffset,sizeof(center)))
    {
        g_buildingPlacement(construction,buildingType,p3,previewMode);
        return;
    }
    center=*(ForceNodesVec3*)((u8*)construction+g_cursorOffset);
    g_inBuildingPlacement=1;
    g_buildingPreviewMode=previewMode?1:0;
    g_buildingConstruction=construction;
    g_buildingType=buildingType;
    g_buildingCenter=center;

    if (g_buildingPreviewMode)
    {
        ++g_buildingSerial;
        g_buildingWorkCount=0;
        memset(g_buildingWork,0,sizeof(g_buildingWork));
        g_buildingPlacement(construction,buildingType,p3,previewMode);
        g_buildingPreviewCount=g_buildingWorkCount;
        if (g_buildingPreviewCount>0)
            memcpy(g_buildingPreview,g_buildingWork,(usize)g_buildingPreviewCount*sizeof(BuildingProposal));
        else memset(g_buildingPreview,0,sizeof(g_buildingPreview));
        g_buildingPreviewType=buildingType;
        g_buildingPreviewCenter=center;
        g_buildingPreviewSerial=g_buildingSerial;
    }
    else
    {
        int preparation=2;
        if (BuildingPreviewCacheMatches(buildingType,&center))
            preparation=PrepareBuildingCommit(construction);
        if (preparation==0)
        {
            if (Readable((u8*)construction+g_buildingStatusOffset,4))
                *(int*)((u8*)construction+g_buildingStatusOffset)=10;
            H->log("ForceNodes  unsafe building placement refused; no disconnected forced connector was created");
        }
        else
        {
            g_buildingPlacement(construction,buildingType,p3,previewMode);
            if (preparation==1 && g_logActions && Readable((u8*)construction+g_buildingStatusOffset,4))
            {
                int status=*(int*)((u8*)construction+g_buildingStatusOffset);
                H->log("ForceNodes  straight building connector transaction finished; native placement status=%d",status);
            }
        }
    }

    g_buildingConstruction=0;
    g_buildingType=0;
    g_buildingPreviewMode=0;
    g_inBuildingPlacement=0;
}

static NOINLINE void CoreFrameDetour(void* construction)
{
    if (!g_coreFrame) return;
    if (g_inFrame)
    {
        g_coreFrame(construction);
        return;
    }
    g_inFrame=1;
    g_blockNativeBuildThisFrame=0;
    g_blockNativeBuildLogged=0;
    RestoreFrameCursorOverride();

    ForceNodesVec3 cursor={0,0,0};
    ForceNodesVec3 raw={0,0,0};
    int haveCursor=construction && Readable((u8*)construction+g_cursorOffset,sizeof(cursor));
    int haveRaw=construction && Readable((u8*)construction+g_rawCursorOffset,sizeof(raw));
    if (haveCursor) cursor=*(ForceNodesVec3*)((u8*)construction+g_cursorOffset);
    if (haveRaw) raw=*(ForceNodesVec3*)((u8*)construction+g_rawCursorOffset); else raw=cursor;

    ForceNodesFrameContext ctx;
    memset(&ctx,0,sizeof(ctx));
    ctx.structSize=sizeof(ctx);
    ctx.phase=FORCE_NODES_PHASE_BEFORE_MAIN;
    ctx.constructionObject=construction;
    ctx.cursor=cursor;
    ctx.rawCursor=raw;
    ctx.effectiveCursor=cursor;
    ctx.mouseLeftPressed=MouseLeftPressed();
    ctx.pathKind=FORCE_NODES_PATH_NONE;
    ctx.toolName=ReadToolName(construction);
    ctx.mouseRightPressed=MouseRightPressed();
    ctx.connectionStatus=FORCE_NODES_CONNECTION_NONE;

    if (ctx.mouseLeftPressed && g_scanBeforeMainClick && g_clientCount>0 && construction)
        RefreshCoreWorldCache(construction,&ctx.rawCursor);

    g_phase=(int)ctx.phase;
    DispatchClients(&ctx);
    g_phase=-1;

    g_coreFrame(construction);

    /* The native final-build call happens inside g_coreFrame after the path
       selector hook. Keep any forced target alive until that call has returned,
       then restore the player's real cursor for the rest of the game frame. */
    RestoreFrameCursorOverride();
    g_blockNativeBuildThisFrame=0;
    g_blockNativeBuildLogged=0;

    ctx.phase=FORCE_NODES_PHASE_AFTER_MAIN;
    g_phase=(int)ctx.phase;
    DispatchClients(&ctx);
    g_phase=-1;
    g_inFrame=0;
}

static NOINLINE void NativePathBuilderDetour(void* world,void* data,u32 p3,u32 p4,
                                              usize p5,usize p6,usize p7,usize p8,usize p9,usize p10)
{
    if (!g_nativePathBuilder) return;
    /* The native routine reads argument 8 as a three-pointer vector descriptor
       whenever it is non-zero. Refuse a corrupted chain value instead of
       letting an invalid pointer take down the game. */
    if (p8 && !Readable((const void*)p8,24))
    {
        H->log("ForceNodes  native path build blocked: invalid arg8 pointer 0x%llX",p8);
        return;
    }
    if (g_inNativeDetour)
    {
        g_nativePathBuilder(world,data,p3,p4,p5,p6,p7,p8,p9,p10);
        return;
    }
    if (g_blockNativeBuildThisFrame)
    {
        if (!g_blockNativeBuildLogged && g_logActions)
        {
            H->log("ForceNodes  unsafe forced path build BLOCKED; no shared native node was verified");
            g_blockNativeBuildLogged=1;
        }
        return;
    }

    g_inNativeDetour=1;
    int handled=0;
    if (g_inBuildingPlacement && g_straightModeEnabled)
    {
        ForceNodesBuildingConnectionRef connection;
        if (ParseBuildingConnection(world,data,p3,p4,p8,&connection))
        {
            if (g_buildingPreviewMode)
            {
                int decision=DispatchBuildingConnection(&connection);
                if (decision==1)
                {
                    CallNativeWithStraightConnection(world,data,p3,p4,p5,p6,p7,p8,p9,p10,
                                                     &g_buildingRequestedTarget);
                    handled=1;
                }
            }
            else
            {
                BuildingProposal* proposal=MatchPreparedBuildingConnection(&connection);
                if (proposal && proposal->managed)
                {
                    if (proposal->prepared && proposal->preparedNode &&
                        Readable((u8*)proposal->preparedNode+4,sizeof(ForceNodesVec3)) &&
                        Distance2XZ((ForceNodesVec3*)((u8*)proposal->preparedNode+4),
                                    &proposal->target)<=g_buildingNodeTolerance*g_buildingNodeTolerance)
                    {
                        CallNativeWithStraightConnection(world,data,p3,p4,p5,p6,p7,p8,p9,p10,
                                                         &proposal->target);
                        if (!NativeBuildingEndpointMatches(world,proposal->preparedNode,&proposal->target))
                        {
                            if (Readable((u8*)g_buildingConstruction+g_buildingStatusOffset,4))
                                *(int*)((u8*)g_buildingConstruction+g_buildingStatusOffset)=10;
                            H->log("ForceNodes  building connector BLOCKED: native builder did not bind the exact prepared node");
                        }
                        handled=1;
                    }
                    else
                    {
                        if (Readable((u8*)g_buildingConstruction+g_buildingStatusOffset,4))
                            *(int*)((u8*)g_buildingConstruction+g_buildingStatusOffset)=10;
                        H->log("ForceNodes  building connector build suppressed: prepared real node was lost");
                        handled=1;
                    }
                }
            }
        }
    }
    if (!handled) g_nativePathBuilder(world,data,p3,p4,p5,p6,p7,p8,p9,p10);
    g_inNativeDetour=0;
}

static void BeginPathPreview(void* construction,unsigned pathKind,ForceNodesFrameContext* ctx)
{
    ForceNodesVec3 cursor={0,0,0};
    ForceNodesVec3 raw={0,0,0};
    int haveCursor=construction && Readable((u8*)construction+g_cursorOffset,sizeof(cursor));
    int haveRaw=construction && Readable((u8*)construction+g_rawCursorOffset,sizeof(raw));
    if (haveCursor) cursor=*(ForceNodesVec3*)((u8*)construction+g_cursorOffset);
    if (haveRaw) raw=*(ForceNodesVec3*)((u8*)construction+g_rawCursorOffset); else raw=cursor;

    memset(ctx,0,sizeof(*ctx));
    ctx->structSize=sizeof(*ctx);
    ctx->phase=FORCE_NODES_PHASE_PATH_PREVIEW;
    ctx->constructionObject=construction;
    ctx->cursor=cursor;
    ctx->rawCursor=raw;
    ctx->effectiveCursor=cursor;
    ctx->mouseLeftPressed=MouseLeftPressed();
    if (!ctx->mouseLeftPressed) g_pathScanLatch=0;
    ctx->pathKind=pathKind;
    ctx->toolName=ReadToolName(construction);
    ctx->mouseRightPressed=MouseRightPressed();
    ctx->connectionStatus=FORCE_NODES_CONNECTION_NONE;

    if (ctx->mouseLeftPressed && !g_pathScanLatch && g_scanPathClick && g_clientCount>0)
    {
        RefreshCoreWorldCache(construction,&ctx->rawCursor);
        g_pathScanLatch=1;
    }

    g_overrideRequested=0;
    g_connectionRequested=0;
    g_connectionSplitNow=0;
    g_connectionRequireNativeNode=0;
    g_connectionStatus=FORCE_NODES_CONNECTION_NONE;
    memset(&g_connectionSegment,0,sizeof(g_connectionSegment));
    g_inPathHook=1;
    g_phase=(int)ctx->phase;
    DispatchClients(ctx);
    g_phase=-1;

    if (g_connectionRequested)
    {
        int ok=1;
        if (g_connectionSplitNow)
        {
            ok=SplitSegment(&g_connectionSegment,&g_connectionPosition);
            if (ok) RefreshCoreWorldCache(construction,&g_connectionPosition);
        }
        if (!ok || !ApplyFrameCursorOverride(construction,&g_connectionPosition))
        {
            g_connectionStatus=FORCE_NODES_CONNECTION_REJECTED;
            RestoreFrameCursorOverride();
        }
        else
        {
            g_connectionStatus=FORCE_NODES_CONNECTION_PREVIEW;
            ctx->cursor=g_connectionPosition;
            ctx->rawCursor=g_connectionPosition;
            ctx->effectiveCursor=g_connectionPosition;
        }
    }
    else if (g_overrideRequested && ApplyFrameCursorOverride(construction,&g_overridePosition))
    {
        ctx->cursor=g_overridePosition;
        ctx->rawCursor=g_overridePosition;
        ctx->effectiveCursor=g_overridePosition;
    }
}

static void FinalizePathPreview(void* construction,ForceNodesFrameContext* ctx)
{
    if (g_connectionRequested)
    {
        if (g_connectionRequireNativeNode)
        {
            if (g_connectionStatus!=FORCE_NODES_CONNECTION_REJECTED &&
                NativeSelectedNodeMatches(construction,&g_connectionPosition))
            {
                g_connectionStatus=FORCE_NODES_CONNECTION_VERIFIED;
                /* Native selector functions may rewrite the cursor. Pin it back
                   to the verified shared node for the later path-build call. */
                ApplyFrameCursorOverride(construction,&g_connectionPosition);
            }
            else
            {
                g_connectionStatus=FORCE_NODES_CONNECTION_REJECTED;
                RestoreFrameCursorOverride();
                g_blockNativeBuildThisFrame=1;
                if (g_logActions)
                    H->log("ForceNodes  forced connection rejected: native shared-node verification failed; build will be blocked");
            }
        }
        else if (g_connectionStatus==FORCE_NODES_CONNECTION_PREVIEW)
        {
            ApplyFrameCursorOverride(construction,&g_connectionPosition);
        }
        ctx->connectionStatus=g_connectionStatus;
        ctx->effectiveCursor=g_connectionPosition;
    }
    else
    {
        ctx->connectionStatus=FORCE_NODES_CONNECTION_NONE;
        if (g_overrideRequested) ctx->effectiveCursor=g_overridePosition;
    }
}

static void EndPathPreview(ForceNodesFrameContext* ctx)
{
    ctx->phase=FORCE_NODES_PHASE_PATH_AFTER;
    g_phase=(int)ctx->phase;
    DispatchClients(ctx);
    g_phase=-1;
    g_inPathHook=0;
    g_overrideRequested=0;
    g_connectionRequested=0;
    g_connectionSplitNow=0;
    g_connectionRequireNativeNode=0;
}

static NOINLINE void RoadBuilderDetour(void* construction,u8 p2,u8 p3,u8 p4,u8 p5,u8 p6,u8 p7,float p8,u8 p9,u8 p10)
{
    if (!g_roadBuilder) return;
    if (g_inPathHook)
    {
        g_roadBuilder(construction,p2,p3,p4,p5,p6,p7,p8,p9,p10);
        return;
    }
    ForceNodesFrameContext ctx;
    BeginPathPreview(construction,FORCE_NODES_PATH_ROAD,&ctx);
    g_roadBuilder(construction,p2,p3,p4,p5,p6,p7,p8,p9,p10);
    FinalizePathPreview(construction,&ctx);
    EndPathPreview(&ctx);
}

static NOINLINE void PedBuilderDetour(void* construction,int p2,u8 p3,u8 p4,u8 p5,u8 p6)
{
    if (!g_pedBuilder) return;
    if (g_inPathHook)
    {
        g_pedBuilder(construction,p2,p3,p4,p5,p6);
        return;
    }
    ForceNodesFrameContext ctx;
    BeginPathPreview(construction,FORCE_NODES_PATH_PEDESTRIAN,&ctx);
    g_pedBuilder(construction,p2,p3,p4,p5,p6);
    FinalizePathPreview(construction,&ctx);
    EndPathPreview(&ctx);
}

DLL_EXPORT unsigned TsmPluginApiVersion(void) { return TSM_API_VERSION; }

DLL_EXPORT int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    H=host;
    if (info) { info->name="ForceNodes"; info->version="1.6.0-stable"; }
    if (!H || H->apiVersion!=TSM_API_VERSION) return 1;
    const char* ini="plugins\\ForceNodes.ini";
    const char* sec="ForceNodes";
    if (!H->configInt(ini,sec,"enabled",1)) return 1;
    LoadBinding("bind_overlay","CTRL+NUMPAD1",&g_bindOverlay);
    LoadBinding("bind_force","CTRL+NUMPAD2",&g_bindForce);
    LoadBinding("bind_grid","CTRL+NUMPAD3",&g_bindGrid);
    LoadBinding("bind_add_node","MOUSE4",&g_bindAdd);
    LoadBinding("bind_remove_node","MOUSE5",&g_bindRemove);
    g_logActions=H->configInt(ini,sec,"service_log",1);
    g_cursorOffset=H->configInt(ini,sec,"advanced_cursor_offset",3948);
    g_rawCursorOffset=H->configInt(ini,sec,"advanced_raw_cursor_offset",3960);
    g_toolNameOffset=H->configInt(ini,sec,"advanced_tool_name_offset",0xd428);
    g_scanBeforeMainClick=H->configInt(ini,sec,"service_scan_before_main_click",0);
    g_scanPathClick=H->configInt(ini,sec,"service_scan_on_path_click",1);
    g_enablePathPreviewHooks=H->configInt(ini,sec,"service_enable_path_preview_hooks",0)?1:0;
    g_enableLegacyBuildingPlacementHook=0;
    g_nativeNodeTolerance=(float)H->configInt(ini,sec,"service_native_node_verify_tolerance_cm",75)/100.0f;
    g_selectedNativeNodeOffset=H->configInt(ini,sec,"advanced_selected_native_node_offset",0xf650);
    g_selectedSharedNodeOffset=H->configInt(ini,sec,"advanced_selected_shared_node_offset",0xf690);
    g_buildingStatusOffset=H->configInt(ini,sec,"advanced_building_status_offset",0x11a10);
    g_buildingOriginTolerance=(float)H->configInt(ini,sec,"service_building_origin_match_cm",15)/100.0f;
    g_buildingCachePositionTolerance=(float)H->configInt(ini,sec,"service_building_preview_cache_cm",75)/100.0f;
    g_buildingSegmentResolveTolerance=(float)H->configInt(ini,sec,"service_building_segment_resolve_cm",75)/100.0f;
    g_buildingNodeTolerance=(float)H->configInt(ini,sec,"service_building_node_verify_tolerance_cm",5)/100.0f;
    g_buildingTargetMergeTolerance=(float)H->configInt(ini,sec,"service_building_target_merge_tolerance_cm",5)/100.0f;
    g_maxBuildingConnections=H->configInt(ini,sec,"service_max_building_connections",64);
    if (g_nativeNodeTolerance<0.05f) g_nativeNodeTolerance=0.05f;
    if (g_nativeNodeTolerance>3.0f) g_nativeNodeTolerance=3.0f;
    if (g_buildingOriginTolerance<0.02f) g_buildingOriginTolerance=0.02f;
    if (g_buildingOriginTolerance>3.0f) g_buildingOriginTolerance=3.0f;
    if (g_buildingCachePositionTolerance<0.10f) g_buildingCachePositionTolerance=0.10f;
    if (g_buildingCachePositionTolerance>3.0f) g_buildingCachePositionTolerance=3.0f;
    if (g_buildingSegmentResolveTolerance<0.10f) g_buildingSegmentResolveTolerance=0.10f;
    if (g_buildingSegmentResolveTolerance>3.0f) g_buildingSegmentResolveTolerance=3.0f;
    if (g_buildingNodeTolerance<0.01f) g_buildingNodeTolerance=0.01f;
    if (g_buildingNodeTolerance>0.25f) g_buildingNodeTolerance=0.25f;
    if (g_buildingTargetMergeTolerance<0.01f) g_buildingTargetMergeTolerance=0.01f;
    if (g_buildingTargetMergeTolerance>0.25f) g_buildingTargetMergeTolerance=0.25f;
    if (g_maxBuildingConnections<1) g_maxBuildingConnections=1;
    if (g_maxBuildingConnections>MAX_BUILDING_CONNECTIONS) g_maxBuildingConnections=MAX_BUILDING_CONNECTIONS;
    if (!H->provide(FORCE_NODES_SERVICE,FORCE_NODES_API_VERSION_V1,&g_apiV1))
    {
        H->log("ForceNodes  service v1 name/version already provided");
        return 1;
    }
    if (!H->provide(FORCE_NODES_SERVICE,FORCE_NODES_API_VERSION_V2,&g_apiV2))
    {
        H->log("ForceNodes  service v2 name/version already provided");
        return 1;
    }
    if (!H->provide(FORCE_NODES_SERVICE,FORCE_NODES_API_VERSION_V3,&g_apiV3))
    {
        H->log("ForceNodes  service v3 name/version already provided");
        return 1;
    }
    /* API v4/v5 were experimental StraightConnections interfaces. They are
       intentionally not provided by stable v1.5, so any leftover old add-on
       fails closed before it can patch DrawStraight or construction input. */
    if (!H->provide(FORCE_NODES_SERVICE,FORCE_NODES_API_VERSION,&g_api))
    {
        H->log("ForceNodes  service v6 name/version already provided");
        return 1;
    }
    return 0;
}

DLL_EXPORT int TsmPluginStart(void)
{
    g_core=FindLoadedModule("ForceNodes.Engine.dll");
    if (!g_core)
    {
        H->log("ForceNodes  internal engine ForceNodes.Engine.dll not loaded; service inactive");
        return 0;
    }
    if (!ValidateCore(g_core))
    {
        H->log("ForceNodes  internal engine validation failed; refusing service hooks");
        return 0;
    }
    if (!InstallCustomBindings())
    {
        H->log("ForceNodes  configurable input bridge failed; refusing service hooks");
        return 0;
    }
    H->log("ForceNodes  bindings overlay=%s force=%s grid=%s add=%s remove=%s",
           g_bindOverlay.text,g_bindForce.text,g_bindGrid.text,g_bindAdd.text,g_bindRemove.text);

    static const u8 engineExpected[15] = {
        0x41,0x57,0x41,0x56,0x56,0x57,0x55,0x53,0x48,0x81,0xEC,0xB8,0x00,0x00,0x00
    };
    if (!H->installInlineHook(g_core+0x2c50,(void*)CoreFrameDetour,
                              (void**)&g_coreFrame,engineExpected,sizeof(engineExpected),
                              "ForceNodes service frame"))
    {
        H->log("ForceNodes  service frame hook refused; service inactive");
        return 0;
    }
    g_ready=1;

    if (!g_enablePathPreviewHooks)
    {
        if (g_logActions)
            H->log("ForceNodes  service v6 ready (v1.6.0 stable); configurable bindings active; recalibration removed; path preview hooks=OFF");
        return 0;
    }

    static const u8 roadPattern[] = {
        0x48,0x8B,0xC4,0x44,0x88,0x48,0x20,0x88,0x50,0x10,0x48,0x89,0x48,0x08,
        0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xA8,0,0,0,0,0x48,0x81,0xEC,0,0,0,0,0x4C,0x8B,0xF9
    };
    static const char roadMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx????xxx";
    static const u8 pedPattern[] = {
        0x48,0x8B,0xC4,0x44,0x88,0x48,0x20,0x44,0x88,0x40,0x18,0x89,0x50,0x10,
        0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xA8,0,0,0,0,0x48,0x81,0xEC,0,0,0,0,
        0xF3,0x0F,0x10,0x81,0x6C,0x0F,0x00,0x00
    };
    static const char pedMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx????xxxxxxxx";
    static const u8 nativePathPattern[] = {
        0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xAC,0x24,0xC8,0xC3,0xFF,0xFF,0xB8,0x58,0x3D,0x00,0x00
    };
    static const char nativePathMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxx";
    static const u8 buildingPlacementPattern[] = {
        0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xAC,0x24,0xD8,0x53,0xFF,0xFF,0xB8,0x48,0xAD,0x00,0x00
    };
    static const char buildingPlacementMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxx";

    int roadRva=H->configInt("plugins\\ForceNodes.ini","ForceNodes","advanced_road_builder_rva",0);
    int pedRva=H->configInt("plugins\\ForceNodes.ini","ForceNodes","advanced_pedestrian_builder_rva",0);
    int nativePathRva=H->configInt("plugins\\ForceNodes.ini","ForceNodes","advanced_native_path_builder_rva",0);
    int buildingPlacementRva=H->configInt("plugins\\ForceNodes.ini","ForceNodes","advanced_building_placement_rva",0);
    u8* road=ResolveBuilder(roadPattern,roadMask,sizeof(roadPattern),roadRva,"road preview builder");
    u8* ped=ResolveBuilder(pedPattern,pedMask,sizeof(pedPattern),pedRva,"pedestrian preview builder");
    u8* nativePath=ResolveBuilder(nativePathPattern,nativePathMask,sizeof(nativePathPattern),nativePathRva,"native path build guard");
    if (!nativePath && nativePathRva<=0)
        nativePath=RecoverDetouredNativePathBuilder(nativePathPattern,sizeof(nativePathPattern));
    u8* buildingPlacement=0;
    if (g_enableLegacyBuildingPlacementHook)
        buildingPlacement=ResolveBuilder(buildingPlacementPattern,buildingPlacementMask,
                                          sizeof(buildingPlacementPattern),buildingPlacementRva,
                                          "building placement auto-connections");
    else
        H->log("ForceNodes  legacy building-placement interception disabled; vanilla ghost preview and rotation preserved");

    if (nativePath)
    {
        u8 expected[21]; memcpy(expected,nativePath,sizeof(expected));
        if (H->installInlineHook(nativePath,(void*)NativePathBuilderDetour,
                                 (void**)&g_nativePathBuilder,expected,sizeof(expected),
                                 "ForceNodes native path build guard"))
        {
            g_nativeBuilderGuardReady=1;
            g_pathCaps|=4u;
        }
        else H->log("ForceNodes  native path build guard hook refused");
    }

    if (buildingPlacement)
    {
        u8 expected[20]; memcpy(expected,buildingPlacement,sizeof(expected));
        if (H->installInlineHook(buildingPlacement,(void*)BuildingPlacementDetour,
                                 (void**)&g_buildingPlacement,expected,sizeof(expected),
                                 "ForceNodes building placement auto-connections"))
        {
            g_buildingHookReady=1;
            g_pathCaps|=8u;
        }
        else H->log("ForceNodes  building placement hook refused");
    }

    if (road)
    {
        u8 expected[15]; memcpy(expected,road,sizeof(expected));
        if (H->installInlineHook(road,(void*)RoadBuilderDetour,(void**)&g_roadBuilder,
                                 expected,sizeof(expected),"ForceNodes road preview"))
            g_pathCaps|=1u;
        else H->log("ForceNodes  road preview hook refused");
    }
    if (ped)
    {
        u8 expected[15]; memcpy(expected,ped,sizeof(expected));
        if (H->installInlineHook(ped,(void*)PedBuilderDetour,(void**)&g_pedBuilder,
                                 expected,sizeof(expected),"ForceNodes pedestrian preview"))
            g_pathCaps|=2u;
        else H->log("ForceNodes  pedestrian preview hook refused");
    }

    if (g_logActions)
        H->log("ForceNodes  service v6 ready (v1.6.0 stable); configurable bindings active; optional developer path hooks; path preview caps=%u",g_pathCaps);
    return 0;
}
