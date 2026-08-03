# Source status

This document distinguishes the two runtime DLLs in ForceNodes v1.6.1.

## `ForceNodes.dll`

Status: **complete buildable source included**.

Relevant files:

```text
source/ForceNodes.cpp
source/ForceNodes_API.h
source/build-clang.bat
source/test_build.py
```

`ForceNodes.cpp` implements the TesmioLoader service wrapper, signature discovery, optional developer hooks, public API publication, and v1.6 configurable input bridge.

## `ForceNodes.Engine.dll`

Status: **runtime binary included; original preferred editable source unavailable**.

Relevant files:

```text
binary-dependency/ForceNodes.Engine.dll
source/patch_engine_v1_6.py
source/patch_engine_v1_6_1.py
```

The patch script records the exact v1.6 changes made to the earlier engine binary:

- disables the retired recalibration branch;
- removes the retired HUD row and closes the gap;
- removes fixed key labels from the HUD;
- updates version and diagnostic strings.

The patch script is useful for auditing and reproduction of those changes, but it does not reconstruct the original engine's algorithms as editable C/C++ source.

## Publication consequence

The repository is suitable for publishing the source that currently exists and for collaborating on the wrapper. It should not be represented as complete corresponding source for every instruction in `ForceNodes.Engine.dll`.

The source-complete route is to replace the binary engine dependency with a clean, buildable implementation of its node discovery, rendering, splitting, promotion, removal, and protection logic.


## v1.6.1 compatibility patch

`source/patch_engine_v1_6_1.py` documents the binary-level compatibility change that recognises an existing standard Tesmio detour at the current game build's main construction-frame RVA and chains it instead of declining to load.
