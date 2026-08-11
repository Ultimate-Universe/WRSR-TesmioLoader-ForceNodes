/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_API_H
#define FORCE_NODES_API_H

#define FORCE_NODES_SERVICE "ForceNodes"
#define FORCE_NODES_API_VERSION_V1 1u
#define FORCE_NODES_API_VERSION_V2 2u
#define FORCE_NODES_API_VERSION_V3 3u
#define FORCE_NODES_API_VERSION_V4 4u /* retired; not provided */
#define FORCE_NODES_API_VERSION_V5 5u /* retired; not provided */
#define FORCE_NODES_API_VERSION 6u

#define FORCE_NODES_PATH_NONE       0u
#define FORCE_NODES_PATH_ROAD       1u
#define FORCE_NODES_PATH_PEDESTRIAN 2u

#define FORCE_NODES_PHASE_BEFORE_MAIN        0u
#define FORCE_NODES_PHASE_AFTER_MAIN         1u
#define FORCE_NODES_PHASE_PATH_PREVIEW       2u
#define FORCE_NODES_PHASE_PATH_AFTER         3u
#define FORCE_NODES_PHASE_BUILDING_PREVIEW   4u
#define FORCE_NODES_PHASE_BUILDING_PRECOMMIT 5u

#define FORCE_NODES_CONTEXT_NONE                     0u
#define FORCE_NODES_CONTEXT_BUILDING_AUTOCONNECTION  1u
#define FORCE_NODES_CONTEXT_BUILDING_COMMIT           2u

#define FORCE_NODES_CONNECTION_NONE     0u
#define FORCE_NODES_CONNECTION_PREVIEW  1u
#define FORCE_NODES_CONNECTION_VERIFIED 2u
#define FORCE_NODES_CONNECTION_REJECTED 3u

typedef struct ForceNodesVec3 { float x, y, z; } ForceNodesVec3;

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
    unsigned pathKind;
    const char* toolName;
    int mouseRightPressed;
    unsigned connectionStatus;
    ForceNodesVec3 effectiveCursor;
    unsigned contextFlags;
    unsigned buildingPreviewSerial;
    const ForceNodesBuildingConnectionRef* buildingConnection;
} ForceNodesFrameContext;

typedef void (*ForceNodesFrameClient)(const ForceNodesFrameContext* frame, void* userData);
typedef int  (*ForceNodesSegmentVisitor)(const ForceNodesSegmentRef* segment, void* userData);

typedef struct ForceNodesApiV1
{
    unsigned structSize, apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
} ForceNodesApiV1;

typedef struct ForceNodesApiV2
{
    unsigned structSize, apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
    unsigned (*pathPreviewCapabilities)(void);
} ForceNodesApiV2;

typedef struct ForceNodesApiV3
{
    unsigned structSize, apiVersion;
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

typedef struct ForceNodesApi
{
    unsigned structSize;
    unsigned apiVersion;
    int (*registerFrameClient)(ForceNodesFrameClient, void*);
    int (*enumerateSegments)(ForceNodesSegmentVisitor, void*);
    int (*splitSegment)(const ForceNodesSegmentRef*, const ForceNodesVec3*);
    int (*requestCursorOverride)(const ForceNodesVec3*);
    int (*isReady)(void);
    unsigned (*pathPreviewCapabilities)(void);
    int (*requestConnectionTarget)(const ForceNodesSegmentRef*, const ForceNodesVec3*, int, int);
    int (*cancelCurrentPathBuild)(void);
    int (*getKeyDown)(int);
} ForceNodesApi;

#endif
