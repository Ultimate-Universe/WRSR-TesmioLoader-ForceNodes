/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_BINDINGS_H
#define FORCE_NODES_BINDINGS_H
#include "Types.h"
#include "../include/tesmio_api.h"

typedef u8 (__fastcall *C3DKeyDownFn)(void*,int);
typedef u8 (__fastcall *C3DMousePressFn)(void*);

struct InputApi
{
    void* object;
    C3DKeyDownFn keyDown;
    C3DMousePressFn mouseLeft;
    C3DMousePressFn mouseRight;
    C3DMousePressFn mouseX1;
    C3DMousePressFn mouseX2;
};

struct BindingEvents
{
    int overlayPressed;
    int forcePressed;
    int gridPressed;
    int addPressed;
    int removePressed;
    int escapePressed;
};

int Bindings_Init(const TsmHost* host, const InputApi* input);
void Bindings_SetInputObject(void* inputObject);
void Bindings_Poll(BindingEvents* events);
int Bindings_KeyDown(int dik);
const char* Bindings_OverlayText(void);
const char* Bindings_ForceText(void);
const char* Bindings_GridText(void);
const char* Bindings_AddText(void);
const char* Bindings_RemoveText(void);

#endif
