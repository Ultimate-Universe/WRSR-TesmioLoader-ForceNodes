/* SPDX-License-Identifier: GPL-3.0-only */
#include "Signatures.h"

namespace
{
static const TsmHost* H;

static u16 Read16(const u8* p) { return (u16)p[0] | (u16)((u16)p[1] << 8); }
static u32 Read32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

struct Pattern
{
    const char* label;
    const u8* bytes;
    const char* mask;
    usize length;
    u32 verifiedRva;
};

static int MaskMatch(const u8* p, const Pattern& pattern)
{
    for (usize i = 0; i < pattern.length; ++i)
        if (pattern.mask[i] == 'x' && p[i] != pattern.bytes[i]) return 0;
    return 1;
}

static u8* FindUnique(const Pattern& pattern, int* countOut)
{
    *countOut = 0;
    if (!H || !H->exeBase || H->exeSize < 0x100 || pattern.length == 0) return 0;
    u8* base = H->exeBase;
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    u32 pe = Read32(base + 0x3C);
    if ((usize)pe + 24 > H->exeSize) return 0;
    u8* nt = base + pe;
    if (nt[0] != 'P' || nt[1] != 'E' || nt[2] || nt[3]) return 0;
    u16 sections = Read16(nt + 6);
    u16 optionalSize = Read16(nt + 20);
    u8* sh = nt + 24 + optionalSize;
    if ((usize)(sh - base) + (usize)sections * 40 > H->exeSize) return 0;
    u8* found = 0;
    for (u16 s = 0; s < sections; ++s)
    {
        u8* row = sh + (usize)s * 40;
        if ((Read32(row + 36) & 0x20000000u) == 0) continue;
        u32 va = Read32(row + 12);
        u32 vs = Read32(row + 8);
        u32 rs = Read32(row + 16);
        usize size = vs > rs ? vs : rs;
        if ((usize)va >= H->exeSize) continue;
        if (size > H->exeSize - (usize)va) size = H->exeSize - (usize)va;
        if (size < pattern.length) continue;
        u8* begin = base + va;
        if (!H->readablePtr(begin, size)) continue;
        for (usize i = 0; i + pattern.length <= size; ++i)
        {
            if (!MaskMatch(begin + i, pattern)) continue;
            found = begin + i;
            ++*countOut;
            if (*countOut > 1) return 0;
        }
    }
    return *countOut == 1 ? found : 0;
}

static u8* Resolve(const Pattern& pattern, int allowDetouredVerifiedRva)
{
    int count = 0;
    u8* p = FindUnique(pattern, &count);
    if (p)
    {
        H->log("ForceNodes  signature ok      %s exe+0x%llX", pattern.label,
               (unsigned long long)(p - H->exeBase));
        return p;
    }
    if (pattern.verifiedRva && (usize)pattern.verifiedRva + pattern.length <= H->exeSize)
    {
        u8* configured = H->exeBase + pattern.verifiedRva;
        if (H->readablePtr(configured, pattern.length))
        {
            if (MaskMatch(configured, pattern))
            {
                H->log("ForceNodes  signature at verified current RVA  %s exe+0x%X",
                       pattern.label, pattern.verifiedRva);
                return configured;
            }
            if (allowDetouredVerifiedRva && configured[0] == 0xFF && configured[1] == 0x25 &&
                configured[2] == 0 && configured[3] == 0 && configured[4] == 0 && configured[5] == 0)
            {
                H->log("ForceNodes  %s already detoured at verified RVA; compatible chaining requested",
                       pattern.label);
                return configured;
            }
        }
    }
    H->log("ForceNodes  signature FAILED  %s matches=%d", pattern.label, count);
    return 0;
}

static const u8 kFrame[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x8D,
    0xA8,0,0,0,0,0x48,0x81,0xEC,0,0,0,0,0x0F,0x29,0x70,0xC8
};
static const char kFrameMask[] = "xxxxxxxxxxxxxxxxx????xxx????xxxx";

static const u8 kSplit[] = {
    0x48,0x8B,0xC4,0x44,0x89,0x40,0x18,0x48,0x89,0x48,0x08,0x55,0x53,0x56,0x57,0x41,
    0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0x88,0x48,0x81,0xEC,0,0,0,0
};
static const char kSplitMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????";

static const u8 kMerge[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x48,0x08,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0x68,0xA8,0x48,0x81,0xEC,0x30,0x01,0x00,0x00,0x48,0xC7,0x45,0xE0,0xFE
};
static const char kMergeMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static const u8 kRefresh[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x83,0x3D,0,0,0,0,0x02,0x48,0x8B,0xD9,
    0x0F,0x8C,0,0,0,0,0x83,0xB9,0x58,0x02,0x00,0x00,0x00,0x0F,0x85
};
static const char kRefreshMask[] = "xxxxxxxx????xxxxxx????xxxxxxxxx";

static const u8 kInsert24[] = {
    0x4C,0x89,0x4C,0x24,0x20,0x4C,0x89,0x44,0x24,0x18,0x53,0x56,0x57,0x41,0x54,0x41,
    0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x50,0x48,0xC7,0x44,0x24,0x30,0xFE,0xFF,
    0xFF,0xFF,0x4D,0x8B,0xC8,0x4C,0x8B,0xE2,0x48,0x8B,0xF9,0x4C,0x8B,0x19,0x4D,0x2B,
    0xC3,0x49,0xBD,0xAB,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x2A,0x49,0x8B,0xC5,0x49,0xF7,
    0xE8,0x4C,0x8B,0xF2,0x49,0xC1,0xFE,0x02
};
static const char kInsert24Mask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static const u8 kReserveByte[] = {
    0x48,0x83,0xEC,0x28,0x4C,0x8B,0x51,0x10,0x4C,0x8B,0xC2,0x48,0x8B,0x51,0x08,0x49,
    0x8B,0xC2,0x48,0x2B,0xC2,0x4C,0x8B,0xC9,0x49,0x3B,0xC0
};
static const char kReserveByteMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxx";

static const u8 kRenderQueue[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x48,0x81,0xEC,0,0,0,0,0x80,0x3D
};
static const char kRenderQueueMask[] = "xxxxxxxxxxxxxxxxxxx????xx";

static_assert(sizeof(kFrame) == sizeof(kFrameMask) - 1, "frame signature mask length");
static_assert(sizeof(kSplit) == sizeof(kSplitMask) - 1, "split signature mask length");
static_assert(sizeof(kMerge) == sizeof(kMergeMask) - 1, "merge signature mask length");
static_assert(sizeof(kRefresh) == sizeof(kRefreshMask) - 1, "refresh signature mask length");
static_assert(sizeof(kInsert24) == sizeof(kInsert24Mask) - 1, "insert signature mask length");
static_assert(sizeof(kReserveByte) == sizeof(kReserveByteMask) - 1, "reserve signature mask length");
static_assert(sizeof(kRenderQueue) == sizeof(kRenderQueueMask) - 1, "render signature mask length");

static const Pattern pFrame = {"main construction frame", kFrame, kFrameMask, sizeof(kFrame), 0x2F0E70u};
static const Pattern pSplit = {"real path splitter", kSplit, kSplitMask, sizeof(kSplit), 0x544600u};
static const Pattern pMerge = {"general path merger", kMerge, kMergeMask, sizeof(kMerge), 0x53E560u};
static const Pattern pRefresh = {"path-world refresh", kRefresh, kRefreshMask, sizeof(kRefresh), 0x5517E0u};
static const Pattern pInsert = {"24-byte path-point insert", kInsert24, kInsert24Mask, sizeof(kInsert24), 0x576190u};
static const Pattern pReserve = {"byte-vector reserve", kReserveByte, kReserveByteMask, sizeof(kReserveByte), 0x1E3C0u};
static const Pattern pRender = {"marker render queue", kRenderQueue, kRenderQueueMask, sizeof(kRenderQueue), 0x4095C0u};
}

int ResolveNativeAddresses(const TsmHost* host, NativeAddresses* out)
{
    if (!host || !out) return 0;
    H = host;
    out->frame = Resolve(pFrame, 1);
    out->split = Resolve(pSplit, 0);
    out->merge = Resolve(pMerge, 0);
    out->refresh = Resolve(pRefresh, 0);
    out->insert24 = Resolve(pInsert, 0);
    out->reserveByte = Resolve(pReserve, 0);
    out->renderQueue = Resolve(pRender, 0);
    return out->frame && out->split && out->merge && out->refresh &&
           out->insert24 && out->reserveByte && out->renderQueue;
}

int MatchFrameNativePrefix(const void* address)
{
    return address && memcmp(address, kFrame, 14) == 0;
}

int IsAbsoluteJumpDetour(const void* address, void** destinationOut)
{
    if (!address) return 0;
    const u8* p = (const u8*)address;
    if (p[0] != 0xFF || p[1] != 0x25 || p[2] || p[3] || p[4] || p[5]) return 0;
    void* destination = *(void* const*)(p + 6);
    if (!destination) return 0;
    if (destinationOut) *destinationOut = destination;
    return 1;
}
