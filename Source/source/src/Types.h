/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_TYPES_H
#define FORCE_NODES_TYPES_H
#include "../include/Platform.h"
#include "../include/ForceNodes_API.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;
typedef uint64_t usize;

typedef ForceNodesVec3 Vec3;
struct Vec4 { float x, y, z, w; };
struct Point24 { union { struct { float x, y, z, a, b, c; }; float v[6]; }; };
struct NodeBlob { u8 data[0x160]; };

static_assert(sizeof(Vec3) == 12, "C3DVECTOR3 ABI changed");
static_assert(sizeof(Vec4) == 16, "render colour ABI changed");
static_assert(sizeof(Point24) == 24, "path point ABI changed");
static_assert(sizeof(NodeBlob) == 0x160, "C3D_NODE scratch size changed");

static inline u32 Mix32(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
#endif
