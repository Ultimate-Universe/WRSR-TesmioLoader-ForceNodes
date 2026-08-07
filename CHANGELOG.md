# Changelog

## 1.7.0

- Rebuilt ForceNodes as a complete source-built TesmioLoader plugin with a conventional Windows PE/import structure.
- Reduced antivirus false-positive surface by removing the old importless/manual module-resolution architecture and unnecessary generic patching machinery.
- Moved the active frame hook to TesmioLoader's supported hook API while preserving compatible hook chaining.
- Centralised and revalidated the seven required WRSR signatures against the current game build; missing or ambiguous signatures now fail closed with precise logging.
- Preserved the established node overlay, force placement, square-grid placement, safe node removal, building-connector handling, crosswalk protection, configurable controls, status HUD, and stable ForceNodes service APIs 1/2/3/6.
- Fixed the rebuilt overlay renderer's automatic render-context/marker-mesh defaults and restored the established right-side status HUD positioning/scaling behaviour.
- Added reproducible LLVM build scripts, static PE/signature validation, release checksums, and complete source for every distributed ForceNodes component.
