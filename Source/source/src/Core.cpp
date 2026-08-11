/* SPDX-License-Identifier: GPL-3.0-only */
#include "Core.h"

namespace
{
static const char kIni[] = "plugins\\ForceNodes.ini";
static const char kSection[] = "ForceNodes";
static const int MAX_CANDIDATES = 8192;
static const int MAX_NODES = 8192;
static const int HASH_SIZE = 16384;
static const int CELL_HASH_SIZE = 16384;
static const int MAX_WORLDS = 128;
static const int MAX_CLIENTS = 32;
static const int MAX_GRID_POINTS_PER_SEGMENT = 512;

static const TsmHost* H;
static CoreResolved R;
static InputApi g_inputApi;
static int g_ready;
static void* g_controller;
static unsigned long long g_frameNumber;
static int g_inFrame;

/* Current-build object layout, all overridable through ForceNodes.ini. */
static u32 g_cursorOffset = 0xF6Cu;
static u32 g_rawCursorOffset = 0xF78u;
static u32 g_toolNameOffset = 0xD428u;
static u32 g_systemCountOffset = 0x14034u;
static u32 g_systemEntriesOffset = 0x14038u;
static u32 g_systemEntryStride = 0x50u;
static u32 g_gameRootSlotRva = 0x9941F0u;
static u32 g_terrainOffset = 0xED8u;
static u32 g_terrainResolutionOffset = 0x220u;
static u32 g_inputObjectRva = 0xA54B90u;
static u32 g_renderContextRva = 0x9D4F10u;
static u32 g_sphereMeshSlotRva = 0x9963C0u;
static u32 g_fontManagerRva = 0x996FB0u;
static u32 g_fontSlotRva = 0x994200u;
static u32 g_uiScaleRva = 0x992088u;
static u32 g_screenWidthRva = 0x99528Cu;
static u32 g_screenHeightRva = 0x995274u;

static float g_gridStep = 19.53125f;
static float g_gridOriginX;
static float g_gridOriginZ;
static float g_gridTolerance = 0.65f;
static float g_selectRadius = 8.0f;
static float g_renderRadius = 850.0f;
static float g_minEndpointDistance = 1.5f;
static float g_existingPointSnap = 0.10f;
static float g_sharedNodeGuard = 0.75f;
static float g_nodeScale = 1.10f;
static float g_candidateScale = 0.62f;
static float g_selectedScale = 0.95f;
static float g_markerHeight = 0.20f;
static int g_candidateLimit = 6000;
static int g_nodeLimit = 6000;
static int g_includeConnectionPaths;
static int g_includeDisabledPaths;
static int g_hudEnabled = 1;
static int g_hudRightMargin = 238;
static int g_hudVerticalOffset = -58;
static int g_logActions = 1;

static int g_overlay;
static int g_force;
static int g_squareGrid;
static BindingEvents g_bindingEvents;

struct Candidate
{
    void* world;
    void* path;
    i32 worldType;
    i32 pathClass;
    i32 pathIndex;
    i32 pointIndex;
    i32 segmentIndex;
    i32 pointCount;
    i32 kind; /* 1 existing, 2 insert */
    float t;
    Vec3 position;
};

struct NodeMarker
{
    void* node;
    void* world;
    Vec3 position;
    i32 path[4];
    i32 degree;
    u8 protectedNode;
    u8 sharedNode;
    u8 connectorNode;
    u8 reserved;
};

struct CandidateHashEntry
{
    void* path;
    i32 x;
    i32 z;
    i32 indexPlusOne;
};

struct NodeHashEntry
{
    void* node;
    i32 indexPlusOne;
};

struct CellEntry
{
    i32 x;
    i32 z;
    i32 head;
    u8 used;
};

static Candidate g_candidates[MAX_CANDIDATES];
static CandidateHashEntry g_candidateHash[HASH_SIZE];
static NodeMarker g_nodes[MAX_NODES];
static NodeHashEntry g_nodeHash[HASH_SIZE];
static CellEntry g_cells[CELL_HASH_SIZE];
static i32 g_cellNext[MAX_NODES];
static void* g_seenWorld[MAX_WORLDS];
static i32 g_candidateCount;
static i32 g_nodeCount;
static i32 g_seenWorldCount;

static Candidate g_overlaySelected;
static Candidate g_forceSelected;
static NodeMarker* g_removeSelected;
static int g_overlaySelectedValid;
static int g_forceSelectedValid;
static float g_overlaySelectedD2;
static float g_forceSelectedD2;
static float g_removeSelectedD2;

struct FrameClient
{
    ForceNodesFrameClient fn;
    void* user;
};
static FrameClient g_clients[MAX_CLIENTS];
static int g_clientCount;
static ForceNodesVec3 g_pendingCursor;
static int g_pendingCursorValid;
static ForceNodesVec3 g_originalCursor;
static ForceNodesVec3 g_originalRaw;
static int g_restoreCursor;
static int g_restoreRaw;
static char g_toolName[96];

typedef int  (__fastcall* SplitPathFn)(void*, int, int, void*);
typedef unsigned long long (__fastcall* MergePathsFn)(void*, int, int, unsigned char);
typedef void (__fastcall* RefreshWorldFn)(void*);
typedef void (__fastcall* InsertPointFn)(void*, void**, u8*, Point24*, Point24*);
typedef void (__fastcall* ReserveByteFn)(void*, unsigned long long);
typedef void (__fastcall* RenderMeshFn)(void*, void*, void*, Vec4*);
typedef void (__fastcall* NodeCtorFn)(void*);
typedef void (__fastcall* NodeCreateFn)(void*, Vec3, Vec3, Vec3);
typedef void (__fastcall* FontPrintFn)(void*, void*, float, float, unsigned long, const wchar_t*);

static SplitPathFn SplitPath() { return (SplitPathFn)R.splitPath; }
static MergePathsFn MergePaths() { return (MergePathsFn)R.mergePaths; }
static RefreshWorldFn RefreshWorld() { return (RefreshWorldFn)R.refreshWorld; }
static InsertPointFn InsertPoint() { return (InsertPointFn)R.insertPoint24; }
static ReserveByteFn ReserveByte() { return (ReserveByteFn)R.reserveByteVector; }
static RenderMeshFn RenderMesh() { return (RenderMeshFn)R.renderQueue; }
static NodeCtorFn NodeCtor() { return (NodeCtorFn)R.nodeCtor; }
static NodeCreateFn NodeCreate() { return (NodeCreateFn)R.nodeTransform; }
static FontPrintFn FontPrint() { return (FontPrintFn)R.fontPrint; }

static int Readable(const void* p, usize n)
{
    return H && H->readablePtr && p && H->readablePtr(p, n);
}

static int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float AbsF(float v) { return v < 0.0f ? -v : v; }
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static int IsFiniteFloat(float value)
{
    union { float f; u32 u; } b;
    b.f = value;
    return (b.u & 0x7F800000u) != 0x7F800000u;
}

static int ValidPosition(const Vec3& p)
{
    if (!IsFiniteFloat(p.x) || !IsFiniteFloat(p.y) || !IsFiniteFloat(p.z)) return 0;
    return AbsF(p.x) < 1000000.0f && AbsF(p.y) < 1000000.0f && AbsF(p.z) < 1000000.0f;
}

static float Distance2XZ(const Vec3& a, const Vec3& b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

static i32 FloorToInt(double value)
{
    i32 i = (i32)value;
    if ((double)i > value) --i;
    return i;
}

static i32 RoundToInt(double value)
{
    return value >= 0.0 ? (i32)(value + 0.5) : (i32)(value - 0.5);
}

static u32 HashPointer(void* p)
{
    u64 k = (u64)p;
    return Mix32((u32)(k >> 4) ^ (u32)(k >> 33));
}

static u32 HashPair(i32 x, i32 z)
{
    return Mix32((u32)x * 0x9E3779B1u ^ (u32)z * 0x85EBCA77u);
}

static Vec3 PointPosition(const Point24* p)
{
    Vec3 out = { p->v[0], p->v[1], p->v[2] };
    return out;
}

static Vec3 InterpolatePosition(const Point24* a, const Point24* b, float t)
{
    Vec3 out;
    out.x = a->v[0] + (b->v[0] - a->v[0]) * t;
    out.y = a->v[1] + (b->v[1] - a->v[1]) * t;
    out.z = a->v[2] + (b->v[2] - a->v[2]) * t;
    return out;
}

static int PositionFromController(void* controller, u32 offset, Vec3* out)
{
    if (!controller || !out || !Readable((u8*)controller + offset, sizeof(Vec3))) return 0;
    *out = *(Vec3*)((u8*)controller + offset);
    return ValidPosition(*out);
}

static void ReadToolName(void* controller)
{
    g_toolName[0] = 0;
    if (!controller || !Readable((u8*)controller + g_toolNameOffset, sizeof(void*))) return;
    const char* p = *(const char**)((u8*)controller + g_toolNameOffset);
    if (!p) return;
    int i = 0;
    for (; i < (int)sizeof(g_toolName) - 1; ++i)
    {
        if (!Readable(p + i, 1)) break;
        char c = p[i];
        g_toolName[i] = c;
        if (!c) return;
    }
    g_toolName[i] = 0;
}

static void LoadConfig()
{
    int v;
    g_cursorOffset = (u32)H->configInt(kIni, kSection, "advanced_cursor_offset", (int)g_cursorOffset);
    g_rawCursorOffset = (u32)H->configInt(kIni, kSection, "advanced_raw_cursor_offset", (int)g_rawCursorOffset);
    g_toolNameOffset = (u32)H->configInt(kIni, kSection, "advanced_tool_name_offset", (int)g_toolNameOffset);
    g_systemCountOffset = (u32)H->configInt(kIni, kSection, "advanced_system_count_offset", (int)g_systemCountOffset);
    g_systemEntriesOffset = (u32)H->configInt(kIni, kSection, "advanced_system_entries_offset", (int)g_systemEntriesOffset);
    g_systemEntryStride = (u32)H->configInt(kIni, kSection, "advanced_system_entry_stride", (int)g_systemEntryStride);
    g_gameRootSlotRva = (u32)H->configInt(kIni, kSection, "advanced_game_root_slot_rva", (int)g_gameRootSlotRva);
    g_terrainOffset = (u32)H->configInt(kIni, kSection, "advanced_terrain_offset", (int)g_terrainOffset);
    g_terrainResolutionOffset = (u32)H->configInt(kIni, kSection, "advanced_terrain_resolution_offset", (int)g_terrainResolutionOffset);
    g_inputObjectRva = (u32)H->configInt(kIni, kSection, "advanced_input_object_rva", (int)g_inputObjectRva);
    /* A zero advanced RVA means "automatic/current-build default" in the
       established ForceNodes.ini contract.  Do not turn zero into exeBase. */
    v = H->configInt(kIni, kSection, "advanced_render_context_rva", 0);
    if (v > 0 && (usize)(u32)v < H->exeSize)
        g_renderContextRva = (u32)v;
    else if (v != 0)
        H->log("ForceNodes  ignored invalid render-context RVA override 0x%X", (u32)v);

    v = H->configInt(kIni, kSection, "advanced_sphere_mesh_slot_rva", 0);
    if (v > 0 && (usize)(u32)v + sizeof(void*) <= H->exeSize)
        g_sphereMeshSlotRva = (u32)v;
    else if (v != 0)
        H->log("ForceNodes  ignored invalid marker-mesh RVA override 0x%X", (u32)v);
    g_fontManagerRva = (u32)H->configInt(kIni, kSection, "advanced_font_manager_rva", (int)g_fontManagerRva);
    g_fontSlotRva = (u32)H->configInt(kIni, kSection, "advanced_font_slot_rva", (int)g_fontSlotRva);
    g_uiScaleRva = (u32)H->configInt(kIni, kSection, "advanced_ui_scale_rva", (int)g_uiScaleRva);
    g_screenWidthRva = (u32)H->configInt(kIni, kSection, "advanced_screen_width_rva", (int)g_screenWidthRva);
    g_screenHeightRva = (u32)H->configInt(kIni, kSection, "advanced_screen_height_rva", (int)g_screenHeightRva);

    v = H->configInt(kIni, kSection, "wire_grid_origin_x_cm", 0); g_gridOriginX = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "wire_grid_origin_z_cm", 0); g_gridOriginZ = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "wire_grid_tolerance_cm", 65); if (v >= 1) g_gridTolerance = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "selection_radius_cm", 800); if (v >= 50) g_selectRadius = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "overlay_radius_m", 850); if (v >= 25) g_renderRadius = (float)v;
    v = H->configInt(kIni, kSection, "minimum_endpoint_clearance_cm", 150); if (v >= 20) g_minEndpointDistance = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "existing_point_snap_cm", 10); if (v >= 1) g_existingPointSnap = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "shared_node_guard_cm", 75); if (v >= 5) g_sharedNodeGuard = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "node_marker_scale_percent", 110); if (v >= 10) g_nodeScale = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "candidate_marker_scale_percent", 62); if (v >= 10) g_candidateScale = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "selected_marker_scale_percent", 95); if (v >= 10) g_selectedScale = (float)v * 0.01f;
    v = H->configInt(kIni, kSection, "marker_height_cm", 20); g_markerHeight = (float)v * 0.01f;
    g_candidateLimit = ClampInt(H->configInt(kIni, kSection, "maximum_green_markers", 6000), 128, MAX_CANDIDATES);
    g_nodeLimit = ClampInt(H->configInt(kIni, kSection, "maximum_purple_markers", 6000), 128, MAX_NODES);
    g_includeConnectionPaths = H->configInt(kIni, kSection, "include_connection_paths", 0) != 0;
    g_includeDisabledPaths = H->configInt(kIni, kSection, "include_disabled_paths", 0) != 0;
    if (!g_includeDisabledPaths)
        g_includeDisabledPaths = H->configInt(kIni, kSection, "include_special_paths", 0) != 0;
    g_hudEnabled = H->configInt(kIni, kSection, "hud_enabled", 1) != 0;
    g_hudRightMargin = H->configInt(kIni, kSection, "hud_right_margin_px", 238);
    g_hudVerticalOffset = H->configInt(kIni, kSection, "hud_vertical_offset_px", -58);
    g_logActions = H->configInt(kIni, kSection, "service_log", 1) != 0;
    g_squareGrid = H->configInt(kIni, kSection, "wire_default", 0) != 0;

    v = H->configInt(kIni, kSection, "wire_grid_step_cm", 0);
    if (v >= 50)
    {
        g_gridStep = (float)v * 0.01f;
        H->log("ForceNodes  grid override active  step=%.6f m", g_gridStep);
    }
    else
    {
        int calibrated = 0;
        if (g_gameRootSlotRva + sizeof(void*) <= H->exeSize)
        {
            void** rootSlot = (void**)(H->exeBase + g_gameRootSlotRva);
            if (Readable(rootSlot, sizeof(void*)) && *rootSlot &&
                Readable((u8*)*rootSlot + g_terrainOffset, sizeof(void*)))
            {
                void* terrain = *(void**)((u8*)*rootSlot + g_terrainOffset);
                if (terrain && Readable((u8*)terrain + g_terrainResolutionOffset, sizeof(int)))
                {
                    int resolution = *(int*)((u8*)terrain + g_terrainResolutionOffset);
                    if (resolution >= 64 && resolution <= 16384)
                    {
                        g_gridStep = 20000.0f / (float)resolution;
                        calibrated = 1;
                        H->log("ForceNodes  native grid calibrated  resolution=%d step=%.6f m", resolution, g_gridStep);
                    }
                }
            }
        }
        if (!calibrated)
            H->log("ForceNodes  native grid unavailable; fallback step=%.6f m", g_gridStep);
    }
}

static int ControllerReadable(void* controller)
{
    return controller &&
           Readable((u8*)controller + g_cursorOffset, sizeof(Vec3)) &&
           Readable((u8*)controller + g_systemCountOffset, sizeof(i32));
}

static int MarkWorldSeen(void* world)
{
    for (int i = 0; i < g_seenWorldCount; ++i)
        if (g_seenWorld[i] == world) return 1;
    if (g_seenWorldCount < MAX_WORLDS) g_seenWorld[g_seenWorldCount++] = world;
    return 0;
}

static int EligibleWorldType(int type)
{
    if (type == 0 || type == 3 || type == 4) return 1;
    if (type == 5 && g_includeConnectionPaths) return 1;
    return 0;
}

static int ValidPathPoints(void* path, Point24** beginOut, int* countOut)
{
    if (!path || !Readable((u8*)path + 8, 0x18)) return 0;
    u8* begin = *(u8**)((u8*)path + 8);
    u8* end = *(u8**)((u8*)path + 0x10);
    u8* cap = *(u8**)((u8*)path + 0x18);
    if (!begin || !end || !cap || begin > end || end > cap) return 0;
    usize bytes = (usize)(end - begin);
    if (bytes % sizeof(Point24)) return 0;
    usize count = bytes / sizeof(Point24);
    if (count < 2 || count > 100000) return 0;
    if (!Readable(begin, bytes)) return 0;
    *beginOut = (Point24*)begin;
    *countOut = (int)count;
    return 1;
}

static int FarEnoughFromPathEnds(const Vec3& p, Point24* points, int count)
{
    float m2 = g_minEndpointDistance * g_minEndpointDistance;
    return Distance2XZ(p, PointPosition(points)) >= m2 &&
           Distance2XZ(p, PointPosition(points + count - 1)) >= m2;
}

static int OnGridLine(float value, float origin, float* snappedOut)
{
    if (g_gridStep <= 0.01f) return 0;
    double q = ((double)value - (double)origin) / (double)g_gridStep;
    i32 k = RoundToInt(q);
    float snapped = origin + (float)((double)k * (double)g_gridStep);
    if (snappedOut) *snappedOut = snapped;
    return AbsF(value - snapped) <= g_gridTolerance;
}

static int ExistingPointCrossesGrid(Point24* points, int index, int count, Vec3* snappedPosition)
{
    (void)count;
    Vec3 p = PointPosition(points + index);
    Vec3 previous = PointPosition(points + index - 1);
    Vec3 next = PointPosition(points + index + 1);
    float sx = p.x, sz = p.z;
    int onX = OnGridLine(p.x, g_gridOriginX, &sx);
    int onZ = OnGridLine(p.z, g_gridOriginZ, &sz);
    const float eps = 0.01f;
    int crossesX = onX && (AbsF(previous.x - p.x) > eps || AbsF(next.x - p.x) > eps);
    int crossesZ = onZ && (AbsF(previous.z - p.z) > eps || AbsF(next.z - p.z) > eps);
    if (!crossesX && !crossesZ) return 0;
    if (crossesX) p.x = sx;
    if (crossesZ) p.z = sz;
    *snappedPosition = p;
    return 1;
}

static Candidate MakeExistingCandidate(void* world, void* path, int worldType,
                                       int pathClass, int pathIndex,
                                       int pointIndex, int pointCount,
                                       Point24* points)
{
    Candidate c;
    memset(&c, 0, sizeof(c));
    c.world = world; c.path = path; c.worldType = worldType; c.pathClass = pathClass;
    c.pathIndex = pathIndex; c.pointIndex = pointIndex; c.segmentIndex = -1;
    c.pointCount = pointCount; c.kind = 1; c.position = PointPosition(points + pointIndex);
    return c;
}

static Candidate MakeInsertCandidate(void* world, void* path, int worldType,
                                     int pathClass, int pathIndex,
                                     int segmentIndex, int pointCount,
                                     float t, Point24* points)
{
    Candidate c;
    memset(&c, 0, sizeof(c));
    c.world = world; c.path = path; c.worldType = worldType; c.pathClass = pathClass;
    c.pathIndex = pathIndex; c.pointIndex = -1; c.segmentIndex = segmentIndex;
    c.pointCount = pointCount; c.kind = 2; c.t = t;
    c.position = InterpolatePosition(points + segmentIndex, points + segmentIndex + 1, t);
    return c;
}

static int CandidateDuplicate(const Candidate& c)
{
    const i32 qx = RoundToInt((double)c.position.x * 20.0); /* 5 cm cells */
    const i32 qz = RoundToInt((double)c.position.z * 20.0);
    u32 slot = (HashPointer(c.path) ^ HashPair(qx, qz)) & (HASH_SIZE - 1);
    for (int probe = 0; probe < 48; ++probe)
    {
        CandidateHashEntry& e = g_candidateHash[(slot + (u32)probe) & (HASH_SIZE - 1)];
        if (!e.indexPlusOne)
        {
            e.path = c.path; e.x = qx; e.z = qz; e.indexPlusOne = g_candidateCount + 1;
            return 0;
        }
        if (e.path == c.path && e.x == qx && e.z == qz)
        {
            const Candidate& old = g_candidates[e.indexPlusOne - 1];
            if (Distance2XZ(old.position, c.position) < 0.0025f) return 1;
        }
    }
    return 0;
}

static void ConsiderOverlaySelected(const Candidate& c, const Vec3& cursor)
{
    float d2 = Distance2XZ(c.position, cursor);
    if (d2 <= g_overlaySelectedD2)
    {
        g_overlaySelected = c;
        g_overlaySelectedD2 = d2;
        g_overlaySelectedValid = 1;
    }
}

static void ConsiderForceSelected(const Candidate& c, const Vec3& cursor)
{
    float d2 = Distance2XZ(c.position, cursor);
    if (d2 <= g_forceSelectedD2)
    {
        g_forceSelected = c;
        g_forceSelectedD2 = d2;
        g_forceSelectedValid = 1;
    }
}

static void AddOverlayCandidate(const Candidate& c, const Vec3& cursor)
{
    if (!ValidPosition(c.position)) return;
    if (Distance2XZ(c.position, cursor) > g_renderRadius * g_renderRadius) return;
    if (g_candidateCount >= g_candidateLimit || g_candidateCount >= MAX_CANDIDATES) return;
    if (CandidateDuplicate(c)) return;
    g_candidates[g_candidateCount++] = c;
    ConsiderOverlaySelected(c, cursor);
}

static int NodeFindOrCreate(void* node, void* world, const Vec3& cursor)
{
    if (!node || g_nodeCount >= g_nodeLimit || g_nodeCount >= MAX_NODES) return -1;
    u32 slot = HashPointer(node) & (HASH_SIZE - 1);
    for (int probe = 0; probe < 64; ++probe)
    {
        NodeHashEntry& e = g_nodeHash[(slot + (u32)probe) & (HASH_SIZE - 1)];
        if (e.node == node) return e.indexPlusOne - 1;
        if (!e.node)
        {
            if (!Readable((u8*)node + 4, sizeof(Vec3))) return -1;
            Vec3 p = *(Vec3*)((u8*)node + 4);
            if (!ValidPosition(p) || Distance2XZ(p, cursor) > g_renderRadius * g_renderRadius) return -1;
            int index = g_nodeCount++;
            memset(&g_nodes[index], 0, sizeof(NodeMarker));
            g_nodes[index].node = node;
            g_nodes[index].world = world;
            g_nodes[index].position = p;
            for (int i = 0; i < 4; ++i) g_nodes[index].path[i] = -1;
            e.node = node;
            e.indexPlusOne = index + 1;
            return index;
        }
    }
    return -1;
}

static void AddNodeIncident(void* node, void* world, int pathIndex,
                            int pathClass, const Vec3& cursor)
{
    int index = NodeFindOrCreate(node, world, cursor);
    if (index < 0) return;
    NodeMarker& n = g_nodes[index];
    if (n.world != world) n.protectedNode = 1;
    if (pathClass == 10 || pathClass == 20)
    {
        n.connectorNode = 1;
        n.protectedNode = 1;
    }
    for (int i = 0; i < n.degree && i < 4; ++i)
        if (n.path[i] == pathIndex) return;
    if (n.degree < 4) n.path[n.degree] = pathIndex;
    ++n.degree;
    if (n.degree > 2) n.protectedNode = 1;
}

static int FindCellSlot(i32 x, i32 z, int create)
{
    u32 slot = HashPair(x, z) & (CELL_HASH_SIZE - 1);
    for (int probe = 0; probe < 64; ++probe)
    {
        CellEntry& e = g_cells[(slot + (u32)probe) & (CELL_HASH_SIZE - 1)];
        if (e.used && e.x == x && e.z == z) return (int)((slot + (u32)probe) & (CELL_HASH_SIZE - 1));
        if (!e.used)
        {
            if (!create) return -1;
            e.used = 1; e.x = x; e.z = z; e.head = -1;
            return (int)((slot + (u32)probe) & (CELL_HASH_SIZE - 1));
        }
    }
    return -1;
}

static void ProtectSharedNodeClusters(void)
{
    if (g_sharedNodeGuard <= 0.01f) return;
    const float guard2 = g_sharedNodeGuard * g_sharedNodeGuard;
    for (int i = 0; i < g_nodeCount; ++i)
    {
        NodeMarker& n = g_nodes[i];
        i32 cx = FloorToInt((double)n.position.x / (double)g_sharedNodeGuard);
        i32 cz = FloorToInt((double)n.position.z / (double)g_sharedNodeGuard);
        for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
        {
            int s = FindCellSlot(cx + dx, cz + dz, 0);
            if (s < 0) continue;
            for (int j = g_cells[s].head; j >= 0; j = g_cellNext[j])
            {
                if (g_nodes[j].node == n.node) continue;
                if (Distance2XZ(g_nodes[j].position, n.position) <= guard2)
                {
                    g_nodes[j].sharedNode = 1;
                    g_nodes[j].protectedNode = 1;
                    n.sharedNode = 1;
                    n.protectedNode = 1;
                }
            }
        }
        int own = FindCellSlot(cx, cz, 1);
        if (own >= 0)
        {
            g_cellNext[i] = g_cells[own].head;
            g_cells[own].head = i;
        }
    }
}

static void EmitGridAxisIntersections(void* world, void* path, int worldType,
                                      int pathClass, int pathIndex, int segmentIndex,
                                      Point24* points, int count, const Vec3& overlayCursor,
                                      const Vec3& forceCursor, int emitOverlay, int emitForce,
                                      int useX)
{
    Vec3 a = PointPosition(points + segmentIndex);
    Vec3 b = PointPosition(points + segmentIndex + 1);
    double da = useX ? (double)a.x : (double)a.z;
    double db = useX ? (double)b.x : (double)b.z;
    double delta = db - da;
    double origin = useX ? (double)g_gridOriginX : (double)g_gridOriginZ;
    if ((delta > -0.0001 && delta < 0.0001) || g_gridStep <= 0.01f) return;
    double minimum = da < db ? da : db;
    double maximum = da > db ? da : db;
    i32 first = FloorToInt((minimum - origin) / (double)g_gridStep) - 1;
    i32 last  = FloorToInt((maximum - origin) / (double)g_gridStep) + 1;
    int emitted = 0;
    for (i32 k = first; k <= last && emitted < MAX_GRID_POINTS_PER_SEGMENT; ++k)
    {
        double line = origin + (double)k * (double)g_gridStep;
        double td = (line - da) / delta;
        if (td <= 0.0005 || td >= 0.9995) continue;
        Candidate c = MakeInsertCandidate(world, path, worldType, pathClass,
                                          pathIndex, segmentIndex, count, (float)td, points);
        if (useX) c.position.x = (float)line;
        else c.position.z = (float)line;
        if (!FarEnoughFromPathEnds(c.position, points, count)) continue;
        /* At a lattice vertex the X pass owns the marker. */
        if (!useX)
        {
            float ignored = 0.0f;
            if (OnGridLine(c.position.x, g_gridOriginX, &ignored)) continue;
        }
        if (emitOverlay) AddOverlayCandidate(c, overlayCursor);
        if (emitForce) ConsiderForceSelected(c, forceCursor);
        ++emitted;
    }
}

static void ScanExistingCandidates(void* world, void* path, int worldType,
                                   int pathClass, int pathIndex, Point24* points,
                                   int count, const Vec3& cursor)
{
    if (pathClass == 10 || pathClass == 20) return; /* building spline points are geometry, not sockets */
    for (int i = 1; i < count - 1; ++i)
    {
        Candidate c = MakeExistingCandidate(world, path, worldType, pathClass,
                                            pathIndex, i, count, points);
        if (!ValidPosition(c.position) || !FarEnoughFromPathEnds(c.position, points, count)) continue;
        if (g_squareGrid)
        {
            Vec3 snapped;
            if (!ExistingPointCrossesGrid(points, i, count, &snapped)) continue;
            c.position = snapped;
        }
        AddOverlayCandidate(c, cursor);
    }
}

static void ScanFreeForceSegments(void* world, void* path, int worldType,
                                  int pathClass, int pathIndex, Point24* points,
                                  int count, const Vec3& cursor)
{
    const float snap2 = g_existingPointSnap * g_existingPointSnap;
    for (int i = 0; i < count - 1; ++i)
    {
        Vec3 a = PointPosition(points + i);
        Vec3 b = PointPosition(points + i + 1);
        float dx = b.x - a.x;
        float dz = b.z - a.z;
        float length2 = dx * dx + dz * dz;
        if (length2 < 0.0001f) continue;
        float t = ((cursor.x - a.x) * dx + (cursor.z - a.z) * dz) / length2;
        t = Clamp01(t);
        if (t <= 0.0001f || t >= 0.9999f) continue;
        Candidate c = MakeInsertCandidate(world, path, worldType, pathClass,
                                          pathIndex, i, count, t, points);
        if (!FarEnoughFromPathEnds(c.position, points, count)) continue;
        if (pathClass != 10 && pathClass != 20)
        {
            if (i > 0 && Distance2XZ(c.position, a) <= snap2)
                c = MakeExistingCandidate(world, path, worldType, pathClass,
                                          pathIndex, i, count, points);
            else if (i + 1 < count - 1 && Distance2XZ(c.position, b) <= snap2)
                c = MakeExistingCandidate(world, path, worldType, pathClass,
                                          pathIndex, i + 1, count, points);
        }
        ConsiderForceSelected(c, cursor);
    }
}

static void ScanWorld(void* world, const Vec3& overlayCursor, const Vec3& forceCursor)
{
    if (!world || MarkWorldSeen(world)) return;
    if (!Readable((u8*)world + 0x258, sizeof(i32)) || !Readable((u8*)world + 0x2B0, 0x18)) return;
    int worldType = *(i32*)((u8*)world + 0x258);
    if (!EligibleWorldType(worldType)) return;
    void** pathBegin = *(void***)((u8*)world + 0x2B0);
    void** pathEnd = *(void***)((u8*)world + 0x2B8);
    if (!pathBegin || !pathEnd || pathEnd < pathBegin) return;
    usize count64 = (usize)(pathEnd - pathBegin);
    if (count64 > 200000 || !Readable(pathBegin, count64 * sizeof(void*))) return;
    int pathCount = (int)count64;

    for (int pathIndex = 0; pathIndex < pathCount; ++pathIndex)
    {
        void* path = pathBegin[pathIndex];
        if (!path || !Readable((u8*)path + 0x8, 0x20)) continue;
        if (!g_includeDisabledPaths && Readable((u8*)path + 0x140, 1) && *((u8*)path + 0x140)) continue;
        int pathClass = Readable((u8*)path + 0x120, sizeof(i32)) ? *(i32*)((u8*)path + 0x120) : 0;
        Point24* points = 0;
        int pointCount = 0;
        if (!ValidPathPoints(path, &points, &pointCount)) continue;

        if (Readable((u8*)path + 0x20, 0x10))
        {
            AddNodeIncident(*(void**)((u8*)path + 0x20), world, pathIndex, pathClass, overlayCursor);
            AddNodeIncident(*(void**)((u8*)path + 0x28), world, pathIndex, pathClass, overlayCursor);
        }

        if (g_overlay)
        {
            ScanExistingCandidates(world, path, worldType, pathClass, pathIndex,
                                   points, pointCount, overlayCursor);
            if (g_squareGrid)
            {
                for (int s = 0; s < pointCount - 1; ++s)
                {
                    EmitGridAxisIntersections(world, path, worldType, pathClass,
                                              pathIndex, s, points, pointCount,
                                              overlayCursor, forceCursor, 1, 0, 1);
                    EmitGridAxisIntersections(world, path, worldType, pathClass,
                                              pathIndex, s, points, pointCount,
                                              overlayCursor, forceCursor, 1, 0, 0);
                }
            }
        }

        if (g_force)
        {
            if (!g_squareGrid)
                ScanFreeForceSegments(world, path, worldType, pathClass, pathIndex,
                                      points, pointCount, forceCursor);
            else
            {
                /* Grid-force mode is generated-only. Backend spline/control
                   points can never override the exact wire crossing. */
                for (int s = 0; s < pointCount - 1; ++s)
                {
                    EmitGridAxisIntersections(world, path, worldType, pathClass,
                                              pathIndex, s, points, pointCount,
                                              overlayCursor, forceCursor, 0, 1, 1);
                    EmitGridAxisIntersections(world, path, worldType, pathClass,
                                              pathIndex, s, points, pointCount,
                                              overlayCursor, forceCursor, 0, 1, 0);
                }
            }
        }
    }
}

static void ClearScene()
{
    g_candidateCount = 0;
    g_nodeCount = 0;
    g_seenWorldCount = 0;
    g_overlaySelectedValid = 0;
    g_forceSelectedValid = 0;
    g_removeSelected = 0;
    g_overlaySelectedD2 = g_selectRadius * g_selectRadius;
    g_forceSelectedD2 = g_selectRadius * g_selectRadius;
    g_removeSelectedD2 = g_selectRadius * g_selectRadius;
    memset(g_candidateHash, 0, sizeof(g_candidateHash));
    memset(g_nodeHash, 0, sizeof(g_nodeHash));
    memset(g_cells, 0, sizeof(g_cells));
    for (int i = 0; i < MAX_NODES; ++i) g_cellNext[i] = -1;
    for (int i = 0; i < MAX_WORLDS; ++i) g_seenWorld[i] = 0;
}

static int GetWorldEntries(void* controller, int* countOut, u8** entriesOut)
{
    if (!ControllerReadable(controller)) return 0;
    int count = *(int*)((u8*)controller + g_systemCountOffset);
    if (count < 0 || count > MAX_WORLDS) return 0;
    u8* entries = (u8*)controller + g_systemEntriesOffset;
    if (!Readable(entries, (usize)count * g_systemEntryStride)) return 0;
    *countOut = count;
    *entriesOut = entries;
    return 1;
}

static void BuildScene(void* controller, const Vec3& overlayCursor, const Vec3& forceCursor)
{
    ClearScene();
    int count = 0;
    u8* entries = 0;
    if (!GetWorldEntries(controller, &count, &entries)) return;
    for (int i = 0; i < count; ++i)
    {
        void* world = *(void**)(entries + (usize)i * g_systemEntryStride);
        ScanWorld(world, overlayCursor, forceCursor);
    }
    ProtectSharedNodeClusters();

    for (int i = 0; i < g_nodeCount; ++i)
    {
        NodeMarker& n = g_nodes[i];
        if (n.degree != 2 || n.protectedNode || n.connectorNode || n.sharedNode) continue;
        float d2 = Distance2XZ(n.position, forceCursor);
        if (d2 <= g_removeSelectedD2)
        {
            g_removeSelected = &n;
            g_removeSelectedD2 = d2;
        }
    }
}

struct RenderAssets
{
    void* context;
    void* mesh;
};

static int ResolveRenderAssets(RenderAssets* assets)
{
    if (!assets || !RenderMesh() || !NodeCtor() || !NodeCreate()) return 0;
    if (!g_renderContextRva || !g_sphereMeshSlotRva ||
        (usize)g_renderContextRva + 1 > H->exeSize ||
        (usize)g_sphereMeshSlotRva + sizeof(void*) > H->exeSize) return 0;

    assets->context = H->exeBase + g_renderContextRva;
    void** meshSlot = (void**)(H->exeBase + g_sphereMeshSlotRva);
    if (!Readable(assets->context, 1) || !Readable(meshSlot, sizeof(void*))) return 0;

    void* mesh = *meshSlot;
    /* Match the known-good 0.3.6 engine guard: validating the slot alone is
       insufficient because a bad RVA can yield arbitrary non-null bytes. */
    if (!mesh || !Readable(mesh, sizeof(void*))) return 0;
    assets->mesh = mesh;
    return 1;
}

static void RenderOrb(const RenderAssets& assets, const Vec3& position,
                      float scale, const Vec4& color)
{
    alignas(16) NodeBlob node;
    memset(&node, 0, sizeof(node));
    NodeCtor()(&node);
    Vec3 p = position;
    p.y += g_markerHeight;
    Vec3 rotation = {0.0f, 0.0f, 0.0f};
    Vec3 s = {scale, scale, scale};
    Vec4 mutableColor = color;
    NodeCreate()(&node, p, rotation, s);
    RenderMesh()(assets.context, assets.mesh, &node, &mutableColor);
}

static void RenderScene()
{
    RenderAssets assets = {};
    if (!ResolveRenderAssets(&assets)) return;
    const Vec4 purple = {1.00f, 0.05f, 1.00f, 0.88f};
    const Vec4 green = {0.10f, 1.00f, 0.15f, g_force ? 0.37f : 0.74f};
    const Vec4 yellow = {1.00f, 0.95f, 0.10f, 1.00f};
    const Vec4 orange = {1.00f, 0.36f, 0.05f, 1.00f};
    if (g_overlay)
    {
        for (int i = 0; i < g_nodeCount; ++i)
        {
            if (g_removeSelected == &g_nodes[i])
                RenderOrb(assets, g_nodes[i].position, g_nodeScale, orange);
            else
                RenderOrb(assets, g_nodes[i].position, g_nodeScale, purple);
        }
        for (int i = 0; i < g_candidateCount; ++i)
            RenderOrb(assets, g_candidates[i].position, g_candidateScale, green);
    }
    if (g_force && g_forceSelectedValid)
        RenderOrb(assets, g_forceSelected.position, g_selectedScale, yellow);
    else if (!g_force && g_overlaySelectedValid)
        RenderOrb(assets, g_overlaySelected.position, g_selectedScale, yellow);
}

static int HudResources(void** managerOut, void** fontOut)
{
    if (!H || !FontPrint() || !managerOut || !fontOut) return 0;
    if ((usize)g_fontManagerRva + 8 > H->exeSize ||
        (usize)g_fontSlotRva + sizeof(void*) > H->exeSize) return 0;

    void* manager = H->exeBase + g_fontManagerRva;
    void** fontSlot = (void**)(H->exeBase + g_fontSlotRva);
    if (!Readable(manager, 8) || !Readable(fontSlot, sizeof(void*))) return 0;

    void* font = *fontSlot;
    if (!font || !Readable(font, 8)) return 0;
    *managerOut = manager;
    *fontOut = font;
    return 1;
}

static void HudLine(float x, float y, const wchar_t* text, unsigned long color)
{
    void* manager = 0;
    void* font = 0;
    if (!text || !HudResources(&manager, &font)) return;

    /* Match the established 0.3.6-engine-core HUD: a one-pixel dark shadow
       followed by the coloured text.  The C3D font API takes screen-space
       pixel coordinates; UI scale belongs in our offsets/line spacing, not
       as a divisor applied to the final coordinates. */
    FontPrint()(manager, font, x + 1.0f, y + 1.0f, 0xE0000000u, text);
    FontPrint()(manager, font, x, y, color, text);
}

static void RenderHud()
{
    if (!g_hudEnabled || (!g_overlay && !g_force && !g_squareGrid)) return;
    if ((usize)g_screenWidthRva + sizeof(int) > H->exeSize ||
        (usize)g_screenHeightRva + sizeof(int) > H->exeSize) return;

    int* width = (int*)(H->exeBase + g_screenWidthRva);
    int* height = (int*)(H->exeBase + g_screenHeightRva);
    if (!Readable(width, sizeof(int)) || !Readable(height, sizeof(int))) return;
    if (*width < 320 || *height < 200 || *width >= 20001 || *height >= 20001) return;

    void* manager = 0;
    void* font = 0;
    if (!HudResources(&manager, &font)) return;

    float uiScale = 1.0f;
    if ((usize)g_uiScaleRva + sizeof(float) <= H->exeSize &&
        Readable(H->exeBase + g_uiScaleRva, sizeof(float)))
    {
        float candidate = *(float*)(H->exeBase + g_uiScaleRva);
        if (IsFiniteFloat(candidate) && candidate >= 0.25f && candidate <= 8.0f)
            uiScale = candidate;
    }

    /* This is deliberately the same coordinate model used by the last
       known-good ForceNodes HUD.  Dividing x/y by uiScale moves the whole
       outliner off-screen at common UI scales below 1.0. */
    float x = (float)(*width) - (float)g_hudRightMargin * uiScale;
    float y = (float)(*height) * 0.5f + (float)g_hudVerticalOffset * uiScale;
    const float step = 17.0f * uiScale;

    const unsigned long white = 0xFFFFFFFFu;
    const unsigned long inactive = 0xFF8A8A8Au;
    const unsigned long overlayActive = 0xFF38E46Au;
    const unsigned long forceActive = 0xFFFFD83Du;
    const unsigned long gridActive = 0xFF55D9FFu;
    const unsigned long secondary = 0xFFD0D0D0u;

    HudLine(x, y, L"FORCE NODES", white); y += step;
    HudLine(x, y, g_overlay ? L"[8] OVERLAY   ON" : L"[8] OVERLAY   OFF",
            g_overlay ? overlayActive : inactive); y += step;
    HudLine(x, y, g_force ? L"[9] FORCE     ON" : L"[9] FORCE     OFF",
            g_force ? forceActive : inactive); y += step;
    HudLine(x, y, g_squareGrid ? L"[0] GRID      SQUARE" : L"[0] GRID      OFF",
            g_squareGrid ? gridActive : inactive); y += step;
    HudLine(x, y, L"M4 ADD   M5 REMOVE", secondary); y += step;
    HudLine(x, y, L"ESC EXIT MODES", secondary);
}

static int CandidateStillValid(const Candidate& c, Point24** pointsOut, int* countOut)
{
    if (!c.world || !c.path || c.pathIndex < 0) return 0;
    if (!Readable((u8*)c.world + 0x2B0, 0x18)) return 0;
    void** begin = *(void***)((u8*)c.world + 0x2B0);
    void** end = *(void***)((u8*)c.world + 0x2B8);
    if (!begin || !end || end < begin || c.pathIndex >= (int)(end - begin)) return 0;
    if (!Readable(begin + c.pathIndex, sizeof(void*)) || begin[c.pathIndex] != c.path) return 0;
    return ValidPathPoints(c.path, pointsOut, countOut);
}

static int PrepareRoadByteInsertion(void* world, void* path, int oldPointCount,
                                    int insertionIndex)
{
    if (!Readable((u8*)world + 0x258, sizeof(i32))) return 0;
    int worldType = *(i32*)((u8*)world + 0x258);
    if (worldType != 0) return 1;
    u8** vector = (u8**)((u8*)path + 0xE8);
    if (!Readable(vector, 3 * sizeof(void*))) return 0;
    u8* begin = vector[0]; u8* end = vector[1]; u8* cap = vector[2];
    if (!begin || !end || !cap || begin > end || end > cap) return 0;
    usize count = (usize)(end - begin);
    if (count < (usize)oldPointCount || count > (usize)oldPointCount + 8u) return 0;
    if (insertionIndex < 0 || (usize)insertionIndex > count) return 0;
    if (end == cap)
    {
        ReserveByte()(vector, 1);
        begin = vector[0]; end = vector[1]; cap = vector[2];
        if (!begin || !end || !cap || begin > end || end >= cap) return 0;
    }
    return 1;
}

static int FinishRoadByteInsertion(void* world, void* path, int insertionIndex)
{
    int worldType = *(i32*)((u8*)world + 0x258);
    if (worldType != 0) return 1;
    u8** vector = (u8**)((u8*)path + 0xE8);
    u8* begin = vector[0]; u8* end = vector[1]; u8* cap = vector[2];
    if (!begin || !end || !cap || begin > end || end >= cap) return 0;
    usize count = (usize)(end - begin);
    if (insertionIndex < 0 || (usize)insertionIndex > count) return 0;
    u8 value = 0;
    if (count) value = insertionIndex > 0 ? begin[insertionIndex - 1] : begin[0];
    for (u8* p = end; p > begin + insertionIndex; --p) *p = *(p - 1);
    begin[insertionIndex] = value;
    vector[1] = end + 1;
    return 1;
}

static int InsertPointAndSplit(const Candidate& c, Point24* points, int count)
{
    if (!SplitPath() || !InsertPoint() || !ReserveByte()) return 0;
    int segment = c.segmentIndex;
    if (segment < 0 || segment >= count - 1) return 0;
    int insertionIndex = segment + 1;
    if (!PrepareRoadByteInsertion(c.world, c.path, count, insertionIndex))
    {
        H->log("ForceNodes  refused insertion: road lane-byte vector did not match points");
        return 0;
    }
    Point24 inserted;
    for (int i = 0; i < 6; ++i)
        inserted.v[i] = points[segment].v[i] + (points[segment + 1].v[i] - points[segment].v[i]) * c.t;
    inserted.v[0] = c.position.x;
    inserted.v[1] = c.position.y;
    inserted.v[2] = c.position.z;
    u8* where = (u8*)points + (usize)insertionIndex * sizeof(Point24);
    void* outIterator = 0;
    InsertPoint()((u8*)c.path + 8, &outIterator, where, &inserted, &inserted);
    Point24* newPoints = 0;
    int newCount = 0;
    if (!ValidPathPoints(c.path, &newPoints, &newCount) || newCount != count + 1)
    {
        H->log("ForceNodes  point insertion verification FAILED");
        return 0;
    }
    if (!FinishRoadByteInsertion(c.world, c.path, insertionIndex))
    {
        H->log("ForceNodes  lane-byte insertion FAILED after point insertion");
        return 0;
    }
    int result = SplitPath()(c.world, c.pathIndex, insertionIndex, 0);
    if (RefreshWorld()) RefreshWorld()(c.world);
    if (g_logActions) H->log("ForceNodes  forced node created  path=%d point=%d result=%d", c.pathIndex, insertionIndex, result);
    return result != 0;
}

static int ExecuteCandidate(const Candidate& c)
{
    Point24* points = 0;
    int count = 0;
    if (!CandidateStillValid(c, &points, &count))
    {
        H->log("ForceNodes  selected road/path changed; click ignored");
        return 0;
    }
    if (c.kind == 1)
    {
        if (c.pointIndex <= 0 || c.pointIndex >= count - 1 || !SplitPath()) return 0;
        int result = SplitPath()(c.world, c.pathIndex, c.pointIndex, 0);
        if (RefreshWorld()) RefreshWorld()(c.world);
        if (g_logActions) H->log("ForceNodes  backend node promoted  path=%d point=%d result=%d", c.pathIndex, c.pointIndex, result);
        return result != 0;
    }
    if (c.kind == 2) return InsertPointAndSplit(c, points, count);
    return 0;
}

static int RemoveSelectedNode()
{
    if (!g_removeSelected || !MergePaths()) return 0;
    NodeMarker snapshot = *g_removeSelected;
    if (snapshot.degree != 2 || snapshot.protectedNode || snapshot.path[0] < 0 || snapshot.path[1] < 0) return 0;
    if (!Readable((u8*)snapshot.world + 0x2B0, 0x18)) return 0;
    void** begin = *(void***)((u8*)snapshot.world + 0x2B0);
    void** end = *(void***)((u8*)snapshot.world + 0x2B8);
    if (!begin || !end || end < begin) return 0;
    int count = (int)(end - begin);
    if (snapshot.path[0] >= count || snapshot.path[1] >= count) return 0;
    if (!Readable(begin + snapshot.path[0], sizeof(void*)) || !Readable(begin + snapshot.path[1], sizeof(void*))) return 0;
    void* a = begin[snapshot.path[0]];
    void* b = begin[snapshot.path[1]];
    if (!a || !b || !Readable((u8*)a + 0x20, 0x10) || !Readable((u8*)b + 0x20, 0x10)) return 0;
    int aTouches = (*(void**)((u8*)a + 0x20) == snapshot.node || *(void**)((u8*)a + 0x28) == snapshot.node);
    int bTouches = (*(void**)((u8*)b + 0x20) == snapshot.node || *(void**)((u8*)b + 0x28) == snapshot.node);
    if (!aTouches || !bTouches)
    {
        H->log("ForceNodes  node is no longer a valid simple merge; Mouse 5 ignored");
        return 0;
    }
    unsigned long long result = MergePaths()(snapshot.world, snapshot.path[0], snapshot.path[1], 0);
    if (RefreshWorld()) RefreshWorld()(snapshot.world);
    if (g_logActions) H->log("ForceNodes  node removal merge  paths=%d,%d result=%s", snapshot.path[0], snapshot.path[1], result ? "OK" : "FAILED");
    return result != 0;
}

static int g_dispatchPhase = -1;

static int NativeMousePressed(void* fn)
{
    if (!fn || !g_inputApi.object) return 0;
    return ((u8 (__fastcall*)(void*))fn)(g_inputApi.object) ? 1 : 0;
}

static void FillFrameContext(ForceNodesFrameContext* ctx, unsigned phase,
                             const Vec3& cursor, const Vec3& raw)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->structSize = sizeof(*ctx);
    ctx->phase = phase;
    ctx->constructionObject = g_controller;
    ctx->cursor.x = cursor.x; ctx->cursor.y = cursor.y; ctx->cursor.z = cursor.z;
    ctx->rawCursor.x = raw.x; ctx->rawCursor.y = raw.y; ctx->rawCursor.z = raw.z;
    ctx->toolName = g_toolName;
    ctx->effectiveCursor = ctx->cursor;
    ctx->connectionStatus = FORCE_NODES_CONNECTION_NONE;
    ctx->mouseLeftPressed = NativeMousePressed(R.getMouseLeftPress);
    ctx->mouseRightPressed = NativeMousePressed(R.getMouseRightPress);
}

static void DispatchClients(ForceNodesFrameContext* ctx)
{
    g_dispatchPhase = (int)ctx->phase;
    for (int i = 0; i < g_clientCount; ++i)
        if (g_clients[i].fn) g_clients[i].fn(ctx, g_clients[i].user);
    g_dispatchPhase = -1;
}

static void ApplyPendingCursorOverride()
{
    if (!g_pendingCursorValid || !g_controller) return;
    Vec3 p = {g_pendingCursor.x, g_pendingCursor.y, g_pendingCursor.z};
    u8* c = (u8*)g_controller;
    g_restoreCursor = 0;
    g_restoreRaw = 0;
    if (Readable(c + g_cursorOffset, sizeof(Vec3)))
    {
        g_originalCursor = *(Vec3*)(c + g_cursorOffset);
        *(Vec3*)(c + g_cursorOffset) = p;
        g_restoreCursor = 1;
    }
    if (Readable(c + g_rawCursorOffset, sizeof(Vec3)))
    {
        g_originalRaw = *(Vec3*)(c + g_rawCursorOffset);
        *(Vec3*)(c + g_rawCursorOffset) = p;
        g_restoreRaw = 1;
    }
    g_pendingCursorValid = 0;
}

static void RestoreCursorOverride()
{
    if (!g_controller) return;
    u8* c = (u8*)g_controller;
    if (g_restoreCursor && Readable(c + g_cursorOffset, sizeof(Vec3)))
        *(Vec3*)(c + g_cursorOffset) = g_originalCursor;
    if (g_restoreRaw && Readable(c + g_rawCursorOffset, sizeof(Vec3)))
        *(Vec3*)(c + g_rawCursorOffset) = g_originalRaw;
    g_restoreCursor = 0;
    g_restoreRaw = 0;
}

static void LogModes()
{
    H->log("ForceNodes  overlay=%s force=%s grid=%s",
           g_overlay ? "ON" : "OFF",
           g_force ? "ON" : "OFF",
           g_squareGrid ? "SQUARE" : "OFF");
}

} /* namespace */

int Core_Init(const TsmHost* host, const CoreResolved* resolved)
{
    if (!host || !resolved || !resolved->inputObject || !resolved->getKeyDown ||
        !resolved->getMouseX1Press || !resolved->getMouseX2Press) return 0;
    H = host;
    R = *resolved;
    memset(&g_inputApi, 0, sizeof(g_inputApi));
    g_inputApi.object = R.inputObject;
    g_inputApi.keyDown = (u8 (__fastcall*)(void*, int))R.getKeyDown;
    g_inputApi.mouseLeft = (u8 (__fastcall*)(void*))R.getMouseLeftPress;
    g_inputApi.mouseRight = (u8 (__fastcall*)(void*))R.getMouseRightPress;
    g_inputApi.mouseX1 = (u8 (__fastcall*)(void*))R.getMouseX1Press;
    g_inputApi.mouseX2 = (u8 (__fastcall*)(void*))R.getMouseX2Press;
    LoadConfig();
    H->log("ForceNodes  render context     exe+0x%X", g_renderContextRva);
    H->log("ForceNodes  marker mesh slot   exe+0x%X", g_sphereMeshSlotRva);
    if (!Bindings_Init(H, &g_inputApi)) return 0;
    g_overlay = 0;
    g_force = 0;
    g_ready = 0;
    H->log("ForceNodes  core initialised; configurable bindings active");
    H->log("ForceNodes  controls overlay=%s force=%s grid=%s add=%s remove=%s",
           Bindings_OverlayText(), Bindings_ForceText(), Bindings_GridText(),
           Bindings_AddText(), Bindings_RemoveText());
    return 1;
}

void Core_SetReady(int ready) { g_ready = ready ? 1 : 0; }
int Core_IsReady(void) { return g_ready; }

void Core_BeforeNativeFrame(void* controller)
{
    if (!g_ready || g_inFrame) return;
    g_inFrame = 1;
    ++g_frameNumber;
    g_controller = controller;
    g_pendingCursorValid = 0;
    g_restoreCursor = 0;
    g_restoreRaw = 0;
    if (!ControllerReadable(controller))
    {
        g_inFrame = 0;
        return;
    }

    Bindings_Poll(&g_bindingEvents);
    int changed = 0;
    if (g_bindingEvents.escapePressed)
    {
        g_overlay = 0; g_force = 0; g_squareGrid = 0; changed = 1;
        H->log("ForceNodes  all modes OFF");
    }
    else
    {
        if (g_bindingEvents.overlayPressed) { g_overlay = !g_overlay; changed = 1; }
        if (g_bindingEvents.forcePressed) { g_force = !g_force; changed = 1; }
        if (g_bindingEvents.gridPressed) { g_squareGrid = !g_squareGrid; changed = 1; }
    }
    if (changed) LogModes();

    Vec3 cursor = {0,0,0};
    Vec3 raw = {0,0,0};
    PositionFromController(controller, g_cursorOffset, &cursor);
    if (!PositionFromController(controller, g_rawCursorOffset, &raw)) raw = cursor;
    ReadToolName(controller);
    ForceNodesFrameContext ctx;
    FillFrameContext(&ctx, FORCE_NODES_PHASE_BEFORE_MAIN, cursor, raw);
    DispatchClients(&ctx);
    ApplyPendingCursorOverride();
}

void Core_AfterNativeFrame(void* controller)
{
    if (!g_inFrame) return;
    g_controller = controller;
    RestoreCursorOverride();
    if (!g_ready || !ControllerReadable(controller))
    {
        g_inFrame = 0;
        return;
    }

    Vec3 cursor = {0,0,0};
    Vec3 raw = {0,0,0};
    if (!PositionFromController(controller, g_cursorOffset, &cursor))
    {
        g_inFrame = 0;
        return;
    }
    if (!PositionFromController(controller, g_rawCursorOffset, &raw)) raw = cursor;
    ReadToolName(controller);
    ForceNodesFrameContext ctx;
    FillFrameContext(&ctx, FORCE_NODES_PHASE_AFTER_MAIN, cursor, raw);
    DispatchClients(&ctx);

    if (g_overlay || g_force)
    {
        BuildScene(controller, cursor, raw);
        RenderScene();
        if (g_bindingEvents.addPressed)
        {
            const Candidate* selected = g_force ? (g_forceSelectedValid ? &g_forceSelected : 0)
                                                 : (g_overlaySelectedValid ? &g_overlaySelected : 0);
            if (selected) ExecuteCandidate(*selected);
            else if (g_logActions) H->log("ForceNodes  Mouse 4 ignored: no eligible node point within %.1f m", g_selectRadius);
        }
        if (g_bindingEvents.removePressed) RemoveSelectedNode();
    }
    RenderHud();
    g_inFrame = 0;
}

int Core_RegisterFrameClient(ForceNodesFrameClient client, void* userData)
{
    if (!client || g_clientCount >= MAX_CLIENTS) return 0;
    for (int i = 0; i < g_clientCount; ++i)
        if (g_clients[i].fn == client && g_clients[i].user == userData) return 1;
    g_clients[g_clientCount].fn = client;
    g_clients[g_clientCount].user = userData;
    ++g_clientCount;
    return 1;
}

int Core_EnumerateSegments(ForceNodesSegmentVisitor visitor, void* userData)
{
    if (!g_ready || !visitor || !g_controller) return 0;
    int worldCount = 0;
    u8* entries = 0;
    if (!GetWorldEntries(g_controller, &worldCount, &entries)) return 0;
    int delivered = 0;
    for (int wi = 0; wi < worldCount; ++wi)
    {
        void* world = *(void**)(entries + (usize)wi * g_systemEntryStride);
        if (!world || !Readable((u8*)world + 0x258, 4) || !Readable((u8*)world + 0x2B0, 0x18)) continue;
        int worldType = *(int*)((u8*)world + 0x258);
        if (!EligibleWorldType(worldType)) continue;
        void** pathsBegin = *(void***)((u8*)world + 0x2B0);
        void** pathsEnd = *(void***)((u8*)world + 0x2B8);
        if (!pathsBegin || !pathsEnd || pathsEnd < pathsBegin) continue;
        usize pathCount = (usize)(pathsEnd - pathsBegin);
        if (pathCount > 200000 || !Readable(pathsBegin, pathCount * sizeof(void*))) continue;
        for (int pi = 0; pi < (int)pathCount; ++pi)
        {
            void* path = pathsBegin[pi];
            if (!path) continue;
            if (!g_includeDisabledPaths && Readable((u8*)path + 0x140, 1) && *((u8*)path + 0x140)) continue;
            int pathClass = Readable((u8*)path + 0x120, 4) ? *(int*)((u8*)path + 0x120) : 0;
            Point24* points = 0;
            int pointCount = 0;
            if (!ValidPathPoints(path, &points, &pointCount)) continue;
            for (int si = 0; si < pointCount - 1; ++si)
            {
                ForceNodesSegmentRef ref;
                memset(&ref, 0, sizeof(ref));
                ref.structSize = sizeof(ref);
                ref.world = world; ref.path = path;
                ref.worldType = worldType; ref.pathClass = pathClass;
                ref.pathIndex = pi; ref.segmentIndex = si; ref.pointCount = pointCount;
                Vec3 a = PointPosition(points + si);
                Vec3 b = PointPosition(points + si + 1);
                ref.a.x = a.x; ref.a.y = a.y; ref.a.z = a.z;
                ref.b.x = b.x; ref.b.y = b.y; ref.b.z = b.z;
                ++delivered;
                if (!visitor(&ref, userData)) return delivered;
            }
        }
    }
    return delivered;
}

int Core_SplitSegment(const ForceNodesSegmentRef* segment, const ForceNodesVec3* target)
{
    if (!g_ready || !segment || !target || !segment->world || !segment->path) return 0;
    Point24* points = 0;
    int count = 0;
    Candidate c;
    memset(&c, 0, sizeof(c));
    c.world = segment->world; c.path = segment->path;
    c.worldType = segment->worldType; c.pathClass = segment->pathClass;
    c.pathIndex = segment->pathIndex; c.segmentIndex = segment->segmentIndex;
    c.pointCount = segment->pointCount; c.kind = 2;
    if (!CandidateStillValid(c, &points, &count) || c.segmentIndex < 0 || c.segmentIndex >= count - 1) return 0;
    Vec3 a = PointPosition(points + c.segmentIndex);
    Vec3 b = PointPosition(points + c.segmentIndex + 1);
    Vec3 p = {target->x, target->y, target->z};
    float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    float apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
    float denom = abx*abx + aby*aby + abz*abz;
    if (denom < 0.000001f) return 0;
    c.t = (apx*abx + apy*aby + apz*abz) / denom;
    if (c.t <= 0.0001f || c.t >= 0.9999f) return 0;
    c.position = InterpolatePosition(points + c.segmentIndex, points + c.segmentIndex + 1, c.t);
    return InsertPointAndSplit(c, points, count);
}

int Core_RequestCursorOverride(const ForceNodesVec3* target)
{
    if (!g_ready || !target || !g_inFrame || g_dispatchPhase != (int)FORCE_NODES_PHASE_BEFORE_MAIN) return 0;
    g_pendingCursor = *target;
    g_pendingCursorValid = 1;
    return 1;
}

unsigned Core_PathPreviewCapabilities(void)
{
    return 0u; /* v1.8 keeps optional native path-builder hooks disabled. */
}

int Core_RequestConnectionTarget(const ForceNodesSegmentRef*, const ForceNodesVec3*,
                                 int, int)
{
    return 0;
}

int Core_CancelCurrentPathBuild(void)
{
    return 0;
}

int Core_GetKeyDown(int dik)
{
    return Bindings_KeyDown(dik);
}
