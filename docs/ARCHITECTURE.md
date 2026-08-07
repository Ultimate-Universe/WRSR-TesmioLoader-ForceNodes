# ForceNodes architecture

## Runtime components

### `ForceNodes.dll`

The main plugin contains the complete active implementation:

- configuration and input binding parser;
- current-game signature resolver;
- main construction-frame hook;
- road and pedestrian-path world scanning;
- native and generated node-candidate selection;
- square-grid intersection generation;
- genuine node creation and safe node removal;
- building-connector and shared crosswalk protection;
- marker and HUD rendering;
- versioned ForceNodes service APIs.

It follows TesmioLoader's two-phase lifecycle:

1. `TsmPluginInit` validates the host contract, reads configuration, resolves every required native function, initialises the core, and publishes service interfaces.
2. `TsmPluginStart` installs the single active frame hook after every plugin has had an opportunity to publish its services.

### `ForceNodes.Engine.dll`

This is a source-built, hook-free compatibility module. It preserves the established three-file installation layout and replaces any older engine file during an upgrade. It does not scan or patch game memory. All active functionality is integrated into `ForceNodes.dll`.

## Native resolution

`Signatures.cpp` contains only the seven patterns required by ForceNodes. The scanner:

- inspects executable PE sections only;
- requires exactly one match;
- validates the current verified RVA as a diagnostic fallback;
- accepts a verified main-frame RVA only when it already contains the exact TesmioLoader-style 14-byte absolute detour form needed for safe chaining;
- logs and refuses activation when a required function is absent or ambiguous.

There is no generic process scanner, PEB traversal, custom export walker, or silent unverified-address fallback.

## Hooking and chaining

The only active code hook is the main construction-frame function.

- On an unmodified target, ForceNodes calls `TsmHost::installInlineHook` with the exact 14-byte native prefix. TesmioLoader validates the bytes, creates the trampoline, and applies its standard absolute detour.
- If another TesmioLoader plugin has already installed that same standard detour, ForceNodes preserves the existing destination and atomically makes its own frame wrapper the new head of the chain.
- The wrapper performs ForceNodes and service `BEFORE_MAIN` work, calls the previous/native frame exactly once, then performs `AFTER_MAIN` scanning, editing, and rendering.
- Re-entry guards prevent recursive dispatch.

A non-native target that is not the supported standard detour is refused rather than overwritten.

## Input and rendering

Input methods and C3D rendering methods are resolved from the normal `C3DDLL64.dll` import/export interface. The plugin uses the game's input object and native marker render queue, but does not modify the selected construction tool.

The overlay is independent of the road-building toolbar. Mouse 4 and Mouse 5 are edge-latched by the binding layer, so holding a side button does not repeatedly split or merge paths.

## Path scanning and editing

The core scans eligible loaded road and pedestrian path worlds while Overlay or Force mode is active. It validates vector bounds and readable ranges through `TsmHost::readablePtr` before using game-owned data.

Ordinary roads and pedestrian paths combine native eligible points with generated square-grid crossings. Building access paths, identified by native path classes 10 and 20, use generated crossings only so automatic spline-control points cannot override grid selection.

A new forced point is inserted through the game's native 24-byte path-point vector routine and then split through the native path splitter. Road lane-byte metadata is kept in step with the inserted point. Every stage is verified before continuing; failure is logged and the split is refused.

Removal is allowed only for a simple degree-two node whose two incident path records are still valid and compatible. Building connectors, endpoints, junctions, and coincident/shared crosswalk clusters are protected.

## Service API

The stable plugin publishes service versions 1, 2, 3, and 6. All interfaces are static POD tables whose lifetime matches the plugin.

Supported operations include:

- frame callbacks before and after the native construction frame;
- validated segment enumeration;
- verified segment splitting;
- pre-frame cursor override requests;
- readiness and key-state queries.

Experimental service versions 4 and 5 are intentionally not published. Native path-preview transaction hooks are disabled in v1.7.0, so the capability mask is zero and preview-only transaction methods return failure without changing game state.

## Failure model

ForceNodes does not partially activate. Startup is refused when any required signature, C3D export, host function, input object, or core invariant is unavailable. The log names the failed component. No path editing occurs before the full runtime contract has passed validation.

## Binary design

Both DLLs are conventional x64 PE images with:

- normal import directories;
- ordinary KERNEL32 and UCRT imports;
- x64 unwind metadata;
- ASLR, high-entropy VA, and NX compatibility;
- release checksums and reproducible-build metadata;
- no packing, encryption, string hiding, or binary padding;
- no writable-executable sections;
- no embedded local PDB path.

The build uses no third-party hooking library. The only detour implementation used for an unmodified function is TesmioLoader's host API.
