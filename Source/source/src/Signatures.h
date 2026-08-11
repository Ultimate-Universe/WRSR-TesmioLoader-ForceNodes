/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_SIGNATURES_H
#define FORCE_NODES_SIGNATURES_H
#include "Types.h"
#include "../include/tesmio_api.h"

struct NativeAddresses
{
    void* frame;
    void* split;
    void* merge;
    void* refresh;
    void* insert24;
    void* reserveByte;
    void* renderQueue;
};

int ResolveNativeAddresses(const TsmHost* host, NativeAddresses* out);
int MatchFrameNativePrefix(const void* address);
int IsAbsoluteJumpDetour(const void* address, void** destinationOut);

#endif
