# Changelog

## 1.8.0

- Updated ForceNodes for Workers & Resources: Soviet Republic 1.1.1.9.
- Updated both TesmioLoader plugins from host API 3 to host API 4.
- Revalidated all seven native targets against the 1.1.1.9 executable and replaced the obsolete 1.1.1.7 fallback RVAs.
- Preserved signature-first resolution; the 1.1.1.9 RVAs are validated fallbacks rather than blind addresses.
- Removed the previous-game fallback layout so this release fails closed on unsupported WRSR builds.
- Updated loader, log, configuration, PE image and Windows version metadata to v1.8.0.
- Added compiled Windows version resources to the reproducible source build and refreshed release verification.

## 1.7.0

- Rebuilt ForceNodes as a complete source-built TesmioLoader plugin with a conventional Windows PE/import structure.
- Reduced antivirus false-positive surface by removing the old importless/manual module-resolution architecture and unnecessary generic patching machinery.
- Moved the active frame hook to TesmioLoader's supported hook API while preserving compatible hook chaining.
- Centralised and revalidated the seven required WRSR signatures against the current game build; missing or ambiguous signatures now fail closed with precise logging.
- Preserved the established node overlay, force placement, square-grid placement, safe node removal, building-connector handling, crosswalk protection, configurable controls, status HUD, and stable ForceNodes service APIs 1/2/3/6.
- Fixed the rebuilt overlay renderer's automatic render-context/marker-mesh defaults and restored the established right-side status HUD positioning/scaling behaviour.
- Added reproducible LLVM build scripts, static PE/signature validation, release checksums, and complete source for every distributed ForceNodes component.
