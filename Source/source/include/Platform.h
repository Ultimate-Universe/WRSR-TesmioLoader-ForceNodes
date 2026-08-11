/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FORCE_NODES_PLATFORM_H
#define FORCE_NODES_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#define WINAPI __stdcall
#define APIENTRY WINAPI
#define DLL_EXPORT extern "C" __declspec(dllexport)
#define DLL_IMPORT extern "C" __declspec(dllimport)
#define NOINLINE __declspec(noinline)

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define PAGE_EXECUTE_READWRITE 0x40u
#define DLL_PROCESS_ATTACH 1u

typedef void* HANDLE;
typedef void* HMODULE;
typedef void* FARPROC;
typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned long ULONG;
typedef unsigned long long ULONG_PTR;
typedef unsigned long long SIZE_T;
typedef const void* LPCVOID;
typedef void* LPVOID;

DLL_IMPORT FARPROC WINAPI GetProcAddress(HMODULE module, const char* name);
DLL_IMPORT BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, DWORD* oldProtect);
DLL_IMPORT BOOL WINAPI FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size);
DLL_IMPORT HANDLE WINAPI GetCurrentProcess(void);
DLL_IMPORT BOOL WINAPI DisableThreadLibraryCalls(HMODULE module);

DLL_IMPORT void* __cdecl memcpy(void* dst, const void* src, size_t n);
DLL_IMPORT void* __cdecl memset(void* dst, int value, size_t n);
DLL_IMPORT int   __cdecl memcmp(const void* a, const void* b, size_t n);
DLL_IMPORT size_t __cdecl strlen(const char* s);

extern "C" int _fltused;

#endif
