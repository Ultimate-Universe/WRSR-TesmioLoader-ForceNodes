# Architecture

ForceNodes v1.6.0 is split into two runtime modules.

## Service wrapper

`ForceNodes.dll` is loaded by TesmioLoader and:

- locates and validates the internal engine module;
- publishes the versioned `ForceNodes` service API;
- coordinates frame clients and optional path-preview clients;
- provides signature-scanned access to path splitting and world refresh operations;
- parses independent v1.6 keyboard and mouse bindings;
- maps those bindings onto the internal engine's existing edge-latched actions.

## Internal engine

`ForceNodes.Engine.dll` owns the proven manual node-editing implementation:

- node and candidate collection;
- overlay marker rendering;
- target selection;
- add/promote operations;
- safe node removal and path merging;
- building connector and crosswalk/shared-node protection;
- HUD rendering.

## API boundary

Dependent plugins should consume the `ForceNodes` service rather than calling DLL offsets or installing duplicate topology hooks.

The public ABI is defined in `source/ForceNodes_API.h`.
