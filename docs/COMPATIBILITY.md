# ForceNodes v1.7.0 compatibility record

## Assessment

**CURRENT AND CORRECT — static verification against the supplied current game/TesmioLoader build passed, and the v1.7.0 release candidate was smoke-tested in game for overlay activation and HUD rendering.**

Full gameplay regression testing remains recommended after major game or TesmioLoader updates.

## Verified target files

| File | SHA-256 |
|---|---|
| `SOVIET64.exe` | `ec05bb6257da31cfcaec639c8462683ee7bcf26158e0751a29a2d48025169522` |
| `C3DDLL64.dll` | `a98ad30124026dca626d162fa939cf1464bcb1d9d1235ee2d2fd48a472cdf72c` |

TesmioLoader contract: **API version 3**.

## Native function signatures

Every required pattern matched exactly once in the current `SOVIET64.exe`:

| Purpose | Verified RVA | Match count |
|---|---:|---:|
| Main construction frame | `0x2F0E70` | 1 |
| Real path splitter | `0x544600` | 1 |
| General path merger | `0x53E560` | 1 |
| Path-world refresh | `0x5517E0` | 1 |
| 24-byte path-point insertion | `0x576190` | 1 |
| Road lane-byte vector reserve | `0x01E3C0` | 1 |
| Marker render queue | `0x4095C0` | 1 |

The first 14 bytes of the main-frame target are complete instruction boundaries with no RIP-relative operand. They are suitable for the exact TesmioLoader inline-hook contract used by this build.

## Required C3D exports

The current `C3DDLL64.dll` exports all methods used by ForceNodes:

- `?GetKeyDown@C3D_INPUT@@QEAA_NH@Z`
- `?GetMouseSolid@C3D_INPUT@@QEAA?AVC3DVECTOR3@@XZ`
- `?GetMouseLeftPress@C3D_INPUT@@QEAA_NXZ`
- `?GetMouseRightPress@C3D_INPUT@@QEAA_NXZ`
- `?GetMouseX1Press@C3D_INPUT@@QEAA_NXZ`
- `?GetMouseX2Press@C3D_INPUT@@QEAA_NXZ`
- `??0C3D_NODE@@QEAA@XZ`
- `?CreateFromPositionRotationScale@C3D_NODE@@QEAAXVC3DVECTOR3@@00@Z`
- `?PrintLeftUnicodeNoArg@C3D_FONTMANAGER@@QEAAXPEAVC3D_FONT@@MMKPEB_W@Z`

## Verified structure and global assumptions

| Assumption | Current value |
|---|---:|
| Controller world-system count | `+0x14034` |
| Controller world-system entries | `+0x14038` |
| World-system entry stride | `0x50` |
| Controller cursor | `+0x0F6C` |
| Controller raw cursor | `+0x0F78` |
| Controller active tool name | `+0xD428` |
| World type | `+0x258` |
| World path vector | `+0x2B0` |
| Path point vector | `+0x008` |
| Path point record size | `24` bytes |
| Path endpoint-node pointers | `+0x020`, `+0x028` |
| Path class | `+0x120` |
| Disabled/hidden path flag | `+0x140` |
| Committed-path byte | `+0x141` — deliberately not filtered |
| Road lane-byte vector | `+0x0E8` |
| Building-connector path classes | `10`, `20` |
| Game-root pointer slot | `exe+0x9941F0` |
| Terrain pointer | game root `+0xED8` |
| Terrain resolution | terrain `+0x220` |
| Input object | `exe+0xA54B90` |
| Font manager | `exe+0x996FB0` |
| Font slot | `exe+0x994200` |
| UI scale | `exe+0x992088` |
| Screen width / height | `exe+0x99528C`, `exe+0x995274` |
| Render context | `exe+0x9D4F10` |
| Sphere mesh slot | `exe+0x9963C0` |

`advanced_render_context_rva = 0` and `advanced_sphere_mesh_slot_rva = 0` mean **automatic/current-build defaults**. They are not executable-relative address zero. ForceNodes validates both the mesh slot and the dereferenced mesh object before rendering.

## Hook compatibility

ForceNodes installs one active main-frame hook through TesmioLoader. If the target is already a standard TesmioLoader 14-byte absolute detour, ForceNodes chains the existing destination instead of discarding it. Any other unexpected patch form is refused.

A plugin loaded later may still fail if it assumes the target remains native and does not support chaining. That is a limitation of the later plugin's hook policy, not something ForceNodes can safely overwrite.

## Stable service scope

API versions 1, 2, 3, and 6 are published. Experimental versions 4 and 5 are not. Optional native path-preview transaction hooks are disabled and capability queries return zero, matching the stable release configuration.

## Runtime smoke test

1. Load a backed-up save and confirm both ForceNodes plugins initialise without `signature FAILED` or `hook refused` in `tesmioloader.log`.
2. Toggle Overlay, Force, and Square modes; verify ordinary roads, pedestrian paths, and building entrances all show/select the expected candidates.
3. Add one ordinary node, add one grid-aligned building-connector node, remove one eligible simple node, then confirm normal road construction and frame rate remain stable after Escape.
