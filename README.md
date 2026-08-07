# ForceNodes

ForceNodes is a TesmioLoader plugin for **Workers & Resources: Soviet Republic**. It exposes the genuine road and pedestrian-path nodes used by the game, lets you create new connection points where they are needed, and safely removes eligible simple nodes without demolishing the surrounding infrastructure.

## Features

- Shows existing native nodes and valid placement candidates around the cursor.
- Creates or promotes a genuine road or pedestrian-path node.
- Supports free force placement and square-grid-aligned placement.
- Removes safe simple nodes by merging their two adjoining path sections.
- Protects building connectors, crosswalk/shared-node clusters, junctions, and unsafe endpoints.
- Keeps the overlay and force-placement modes independent.
- Provides a versioned `ForceNodes` service for other TesmioLoader plugins.
- Supports fully configurable keyboard and mouse bindings.

Marker colours:

- **Purple:** existing genuine node.
- **Green:** native or generated placement candidate.
- **Yellow:** selected add-node target.
- **Orange:** selected node that is safe to remove.

## Requirements

- Workers & Resources: Soviet Republic on 64-bit Windows.
- [TesmioLoader](https://steamcommunity.com/sharedfiles/filedetails/?id=3774492854), using plugin API version 3.

ForceNodes cannot run through the normal Steam launch path by itself. The game must be started through `tesmiolauncher.exe`.

## Installation

Copy the three files from `release/plugins/` into:

```text
SovietRepublic\tesmioloader\build\plugins\
```

Files:

```text
ForceNodes.dll
ForceNodes.Engine.dll
ForceNodes.ini
```

Enable both ForceNodes DLLs in the TesmioLoader launcher, then launch the game through `tesmiolauncher.exe`.

`ForceNodes.Engine.dll` is a small, hook-free compatibility module retained for the established installation layout. The active node-editing engine and service are built into `ForceNodes.dll`.

The Steam Workshop package is available here:

- https://steamcommunity.com/sharedfiles/filedetails/?id=3776818669

Steam installs the files into the Workshop content folder. They still need to be copied manually into TesmioLoader's plugin folder.

## Default controls

```text
Ctrl + Numpad 8   Toggle node overlay
Ctrl + Numpad 9   Toggle free force placement
Ctrl + Numpad 0   Toggle grid mode OFF / SQUARE
Mouse button 4    Add or promote the selected node
Mouse button 5    Remove the highlighted safe simple node
Escape            Turn off all ForceNodes modes
```

Each action can be rebound independently in `ForceNodes.ini`. The configuration file documents the accepted key names, mouse names, modifiers, and examples. Set an individual binding to `NONE` to disable it.

When any ForceNodes mode is active, a compact status outliner appears halfway down the right side of the screen and shows the current Overlay, Force and Grid states plus the add/remove controls.

## Configuration

The ordinary settings in `ForceNodes.ini` control:

- input bindings;
- automatic or manually overridden square-grid spacing;
- selection and overlay radii;
- marker sizes and limits;
- HUD position;
- optional inclusion of special or disabled path categories;
- action logging.

The `advanced_*` values are compatibility recovery overrides. Leave them at their supplied defaults unless a documented compatibility update requires otherwise.

## Compatibility and safety

ForceNodes uses narrowly scoped runtime signatures and one main-frame hook because WRSR does not expose native path-node editing through normal Workshop asset scripting. The signatures are resolved before any hook or path edit is enabled. A missing or ambiguous required signature causes ForceNodes to refuse activation and write the exact failure to `tesmioloader.log`; it does not fall back to an unverified address.

ForceNodes v1.7.0 was verified against the current supplied game binaries and TesmioLoader API version 3, then smoke-tested in game for overlay activation and the restored right-side status HUD. See [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) for the exact hashes, native functions, and structure assumptions checked for this release.

Game updates can change code signatures or internal structures. Back up important saves and check the issue tracker before using ForceNodes after a major game update.

## Public service API

Dependent plugins can include [`source/include/ForceNodes_API.h`](source/include/ForceNodes_API.h) and consume the `ForceNodes` service during their `TsmPluginStart` phase.

The stable build publishes API versions **1, 2, 3, and 6**. The service supports frame callbacks, segment enumeration, verified splitting, cursor override requests, readiness checks, and input access. Experimental API versions 4 and 5 are not published. Optional native path-preview transaction hooks remain disabled in the stable release, so the path-preview capability mask is zero.

## Building from source

Requirements:

- LLVM 17 or later with `clang-cl` and `lld-link` in `PATH`;
- Python 3.10 or later for static release checks.

On Windows:

```bat
build-clang.bat
```

On Linux or another environment with the Windows-targeting LLVM tools:

```bash
./build-clang.sh
```

The scripts perform a clean deterministic x64 release build and write the runtime files to `build/plugins/`. They then run `tools/test_build.py` to verify the PE architecture, imports, exports, hardening flags, dependencies, version strings, and configuration.

To check the game signatures and C3D exports as well:

```bash
python tools/test_build.py \
  --plugins build/plugins \
  --game /path/to/SOVIET64.exe \
  --c3d /path/to/C3DDLL64.dll
```

See [`docs/BUILDING.md`](docs/BUILDING.md) and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the build and implementation details.

## Repository layout

```text
source/include/       TesmioLoader contract and public ForceNodes API
source/src/           Complete ForceNodes and compatibility-module source
config/               Shipped configuration
build_support/        Import-library definitions for the SDK-independent build
tools/                Static release verifier
docs/                 Architecture, build, and compatibility documentation
release/plugins/       Current player-facing runtime files
```

## Bug reports

Report problems through the GitHub issue tracker. Include:

- a concise description and reproduction steps;
- whether the issue affects roads, pedestrian paths, or building connectors;
- the active ForceNodes modes and bindings;
- `tesmioloader.log` from the affected launch;
- the game and TesmioLoader versions.

## Licence

ForceNodes is distributed under the **GNU General Public License version 3**. See [`LICENSE`](LICENSE).

Workers & Resources: Soviet Republic, TesmioLoader, Steam, and their respective names and assets belong to their owners. This project is not affiliated with or endorsed by 3DIVISION, Hooded Horse, Valve, or the TesmioLoader author.
