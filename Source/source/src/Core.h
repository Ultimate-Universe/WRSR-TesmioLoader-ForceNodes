/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_CORE_H
#define FORCE_NODES_CORE_H

#include "Bindings.h"

struct CoreResolved
{
    void* frameTarget;
    void* splitPath;
    void* mergePaths;
    void* refreshWorld;
    void* insertPoint24;
    void* reserveByteVector;
    void* renderQueue;
    void* nodeCtor;
    void* nodeTransform;
    void* fontPrint;
    void* inputObject;
    void* getKeyDown;
    void* getMouseLeftPress;
    void* getMouseRightPress;
    void* getMouseX1Press;
    void* getMouseX2Press;
};

int Core_Init(const TsmHost* host, const CoreResolved* resolved);
void Core_SetReady(int ready);
int Core_IsReady(void);

void Core_BeforeNativeFrame(void* controller);
void Core_AfterNativeFrame(void* controller);

int Core_RegisterFrameClient(ForceNodesFrameClient client, void* userData);
int Core_EnumerateSegments(ForceNodesSegmentVisitor visitor, void* userData);
int Core_SplitSegment(const ForceNodesSegmentRef* segment, const ForceNodesVec3* target);
int Core_RequestCursorOverride(const ForceNodesVec3* target);
unsigned Core_PathPreviewCapabilities(void);
int Core_RequestConnectionTarget(const ForceNodesSegmentRef* segment,
                                 const ForceNodesVec3* target,
                                 int requireExistingNode,
                                 int blockOnFailure);
int Core_CancelCurrentPathBuild(void);
int Core_GetKeyDown(int dik);

#endif
