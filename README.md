# ForceNodes

ForceNodes is a TesmioLoader plugin for **Workers & Resources: Soviet Republic**. It exposes and edits genuine road and pedestrian path nodes while keeping protected building connectors and shared road/path nodes guarded.

Current version: **1.6.1 stable**

## Features

- Displays genuine existing path nodes and generated placement candidates.
- Creates or promotes a node at the selected position.
- Removes a safe, simple node by merging its adjoining paths.
- Supports free force placement and the game-aligned square grid.
- Protects building connectors, crosswalk/shared-node clusters, and unsafe endpoints.
- Provides a versioned `ForceNodes` service API for dependent TesmioLoader plugins.
- Allows independent keyboard or mouse bindings for Overlay, Force, Grid, Add Node, and Remove Node.

## Requirements

- Workers & Resources: Soviet Republic on 64-bit Windows.
- TesmioLoader installed and working.

## Installation

Copy these files from a ForceNodes release into:

```text
tesmioloader\build\plugins\
```

Required runtime files:

```text
ForceNodes.dll
ForceNodes.Engine.dll
ForceNodes.ini
```

The Workshop package is installed manually after subscription because Steam places the files in the Workshop content directory rather than TesmioLoader's plugin directory.

## Default controls

```text
Ctrl + Numpad 8   Toggle node overlay
Ctrl + Numpad 9   Toggle free force placement
Ctrl + Numpad 0   Cycle grid mode OFF / SQUARE
Mouse button 4    Add or promote the selected node
Mouse button 5    Remove a highlighted safe simple node
Escape            Exit ForceNodes modes
```

All five action bindings can be changed in `ForceNodes.ini`. The INI contains the accepted names and exact formatting examples.

## Building `ForceNodes.dll`

The wrapper is intentionally self-contained and has no external Windows or C runtime imports.

Requirements:

- LLVM with `clang-cl` and `lld-link` available in `PATH`.
- Python 3 for the static verification script.

Build on Windows:

```bat
cd source
build-clang.bat
python test_build.py
```

The wrapper is written to:

```text
source\out\ForceNodes.dll
```

It must be installed beside:

```text
binary-dependency\ForceNodes.Engine.dll
```

## Public API

Dependent plugins can include:

```text
source/ForceNodes_API.h
```

Stable ForceNodes publishes API versions 1, 2, 3, and 6. Experimental API versions 4 and 5 are retained in the header for historical source compatibility but are not published by the stable build.

## Important source-status notice

This repository contains the complete, buildable source used for **`ForceNodes.dll`**, including its input binding layer and public service API.

The internal **`ForceNodes.Engine.dll`** predates the service wrapper. Its original editable C/C++ source was not preserved. The repository therefore includes:

- the exact v1.6.0 engine binary required at runtime; and
- `source/patch_engine_v1_6.py`, which documents and reproduces the v1.6 display/input-related binary patches when given the earlier v0.3.4 engine binary.

## Licence

The repository is distributed under the GNU General Public License version 3. See `LICENSE`.

Workers & Resources: Soviet Republic, TesmioLoader, Steam, and their respective names and assets belong to their owners. This project is not affiliated with or endorsed by 3DIVISION, Hooded Horse, Valve, or the TesmioLoader author.
