# Source status

This document distinguishes the two runtime DLLs in ForceNodes v1.8.0.

## `ForceNodes.dll`

Status: **complete buildable source included**.

Relevant files:

The complete implementation is under `source/src/`, with public and platform headers under `source/include/`. Root build scripts and `build_support/` reproduce the distributed binary, including its Windows version resource.

## `ForceNodes.Engine.dll`

Status: **complete buildable source included**.

`source/src/EngineShim.cpp` implements the small, hook-free compatibility module retained for the established three-file installation layout. All active node-editing functionality is compiled into `ForceNodes.dll`.

## Publication consequence

The v1.8.0 repository contains corresponding editable source for both distributed DLLs. No opaque binary engine dependency or binary patch step remains.
