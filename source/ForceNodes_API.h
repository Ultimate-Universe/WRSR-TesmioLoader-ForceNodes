/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_API_H
#define FORCE_NODES_API_H

#define FORCE_NODES_SERVICE "ForceNodes"
#define FORCE_NODES_API_VERSION_V1 1u
#define FORCE_NODES_API_VERSION_V2 2u
#define FORCE_NODES_API_VERSION_V3 3u
#define FORCE_NODES_API_VERSION_V4 4u /* retired; not provided by stable v1.5 */
#define FORCE_NODES_API_VERSION_V5 5u /* retired; not provided by stable v1.5 */
#define FORCE_NODES_API_VERSION 6u

#define FORCE_NODES_PATH_NONE       0u
#define FORCE_NODES_PATH_ROAD       1u
#define FORCE_NODES_PATH_PEDESTRIAN 2u

#define FORCE_NODES_PHASE_BEFORE_MAIN       0u
#define FORCE_NODES_PHASE_AFTER_MAIN        1u
#define FORCE_NODES_PHASE_PATH_PREVIEW      2u
#define FORCE_NODES_PHASE_PATH_AFTER        3u
#define FORCE_NODES_PHASE_BUILDING_PREVIEW  4u
#define FORCE_NODES_PHASE_BUILDING_PRECOMMIT 5u

#define FORCE_NODES_CONTEXT_NONE                    0u
#define FORCE_NODES_CONTEXT_BUILDING_AUTOCONNECTION 1u
#define FORCE_NODES_CONTEXT_BUILDING_COMMIT          2u

#define FORCE_NODES_CONNECTION_NONE     0u
#define FORCE_NODES_CONNECTION_PREVIEW  1u
#define FORCE_NODES_CONNECTION_VERIFIED 2u
#define FORCE_NODES_CONNECTION_REJECTED 3u

typedef struct ForceNodesVec3
{
    float x;
    float y;
    float z;
} ForceNodesVec3;

typedef struct ForceNodesSegmentRef
{
    unsigned structSize;
    void* world;
    void* path;
    int worldType;
    int pathClass;
    int pathIndex;
    int segmentIndex;
    int pointCount;
    ForceNodesVec3 a;
    ForceNodesVec3 b;
} ForceNodesSegmentRef;

/* Legacy v5 building-preview layout, retained only so older source can be
   inspected. Stable ForceNodes v1.5 does not provide API v5 or invoke these
   callbacks. */
typedef struct ForceNodesBuildingConnectionRef
{
    unsigned structSize;
    void* constructionObject;
    void* world;
    void* pathData;
    int worldType;
    int pathClass;
    int pathSubtype;
    int pointCount;
    unsigned pathKind;
    int originAtStart;
    ForceNodesVec3 buildingCenter;
    ForceNodesVec3 origin;
    ForceNodesVec3 outwardNeighbour;
    ForceNodesVec3 currentTarget;
} ForceNodesBuildingConnectionRef;

typedef struct ForceNodesFrameContext
{
    unsigned structSize;
    unsigned phase;
    void* constructionObject;
    ForceNodesVec3 cursor;
    ForceNodesVec3 rawCursor;
    int mouseLeftPressed;

    /* API v2 fields. */
    unsigned pathKind;
    const char* toolName;
    int mouseRightPressed;

    /* API v3 fields. */
    unsigned connectionStatus;
    ForceNodesVec3 effectiveCursor;

    /* API v5 fields. */
    unsigned contextFlags;
    unsigned buildingPreviewSerial;
    const ForceNodesBuildingConnectionRef* buildingConnection;
} ForceNodesFrameContext;

typedef void (*ForceNodesFrameClient)(const ForceNodesFrameContext* frame, void* userData);
typedef int  (*ForceNodesSegmentVisitor)(const ForceNodesSegmentRef* segment, void* userData);

typedef struct ForceNodesApiV1
{
    unsigned structSize; unsigned apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
} ForceNodesApiV1;

typedef struct ForceNodesApiV2
{
    unsigned structSize; unsigned apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
    unsigned (*pathPreviewCapabilities)(void);
} ForceNodesApiV2;

typedef struct ForceNodesApiV3
{
    unsigned structSize; unsigned apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
    unsigned (*pathPreviewCapabilities)(void);
    int (*requestConnectionTarget)(const ForceNodesSegmentRef*, const ForceNodesVec3*, int, int);
    int (*cancelCurrentPathBuild)(void);
    int (*getKeyDown)(int);
} ForceNodesApiV3;

/* Retired experimental HUD interface. Stable v1.5 does not provide v4. */
typedef struct ForceNodesApiV4
{
    unsigned structSize; unsigned apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
    unsigned (*pathPreviewCapabilities)(void);
    int (*requestConnectionTarget)(const ForceNodesSegmentRef*, const ForceNodesVec3*, int, int);
    int (*cancelCurrentPathBuild)(void);
    int (*getKeyDown)(int);
    int (*setStraightModeHud)(int);
} ForceNodesApiV4;

/* Stable API v6. Building-ghost and persistent-straight controls were removed.
   Path-preview functions remain available for future developer add-ons only;
   the corresponding native hooks are disabled by default in ForceNodes.ini. */
typedef struct ForceNodesApi
{
    unsigned structSize;
    unsigned apiVersion;

    int (*registerFrameClient)(ForceNodesFrameClient callback, void* userData);
    int (*enumerateSegments)(ForceNodesSegmentVisitor visitor, void* userData);
    int (*splitSegment)(const ForceNodesSegmentRef* segment,
                        const ForceNodesVec3* position);
    int (*requestCursorOverride)(const ForceNodesVec3* position);
    int (*isReady)(void);
    unsigned (*pathPreviewCapabilities)(void);
    int (*requestConnectionTarget)(const ForceNodesSegmentRef* segment,
                                   const ForceNodesVec3* position,
                                   int splitNow,
                                   int requireNativeNode);
    int (*cancelCurrentPathBuild)(void);
    int (*getKeyDown)(int dikScanCode);
} ForceNodesApi;

#endif
