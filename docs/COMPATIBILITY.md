# ForceNodes v1.8.0 compatibility record

## Assessment

**CURRENT AND CORRECT FOR WRSR 1.1.1.9 AND TESMIOLOADER API 4.**

The seven native functions, required C3D exports, calling conventions, controller offsets, path structures, and global objects used by ForceNodes were compared against the supplied 1.1.1.9 decompiler/Ghidra evidence. The final v1.8.0 implementation was confirmed in game before release.

This release deliberately contains only the verified WRSR 1.1.1.9 fallback layout. It does not retain fallback RVAs for 1.1.1.7.

## Verified target files

| File | SHA-256 |
|---|---|
| `SOVIET64.exe` 1.1.1.9 | `296644a9f207d609031fc2ae73fed2dcb34619a1d55a35d1c7b51965ce6841b8` |
| `C3DDLL64.dll` 1.1.1.9 | `65fcb6845c3cb7c25a22121f25904a855ea88657e31a3b50a15adac26e231516` |

TesmioLoader contract: **API version 4**.

## Native function signatures

| Purpose | Verified 1.1.1.9 RVA |
|---|---:|
| Main construction frame | `0x2F0F10` |
| Real path splitter | `0x5446D0` |
| General path merger | `0x53E630` |
| Path-world refresh | `0x5518B0` |
| 24-byte path-point insertion | `0x576260` |
| Road lane-byte vector reserve | `0x01E3C0` |
| Marker render queue | `0x409660` |

Runtime signature scanning remains the primary resolver. A listed RVA is accepted only if the bytes at that location match the expected function signature. Missing, ambiguous, or mismatched targets fail closed before ForceNodes installs its frame hook.

The first 14 bytes of the main-frame target remain complete instruction boundaries with no RIP-relative operand and satisfy TesmioLoader's inline-hook contract.

## Required C3D exports

The supplied 1.1.1.9 `C3DDLL64.dll` export evidence contains all methods used by ForceNodes:

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

These values are unchanged in the 1.1.1.9 evidence:

| Assumption | Value |
|---|---:|
| Controller world-system count | `+0x14034` |
| Controller world-system entries | `+0x14038` |
| World-system entry stride | `0x50` |
| Controller cursor / raw cursor | `+0x0F6C`, `+0x0F78` |
| Controller active tool name | `+0xD428` |
| World type / path vector | `+0x258`, `+0x2B0` |
| Path point vector / record size | `+0x008`, `24` bytes |
| Path endpoint-node pointers | `+0x020`, `+0x028` |
| Path class / hidden flag / committed byte | `+0x120`, `+0x140`, `+0x141` |
| Road lane-byte vector | `+0x0E8` |
| Building-connector path classes | `10`, `20` |
| Game-root pointer slot | `exe+0x9941F0` |
| Terrain pointer / resolution | game root `+0xED8`, terrain `+0x220` |
| Input object | `exe+0xA54B90` |
| Font manager / slot | `exe+0x996FB0`, `exe+0x994200` |
| UI scale | `exe+0x992088` |
| Screen width / height | `exe+0x99528C`, `exe+0x995274` |
| Render context / sphere mesh slot | `exe+0x9D4F10`, `exe+0x9963C0` |

`advanced_render_context_rva = 0` and `advanced_sphere_mesh_slot_rva = 0` select the verified automatic defaults. They do not mean executable-relative address zero.

## Hook compatibility

ForceNodes installs one active main-frame hook through TesmioLoader. If the target already contains TesmioLoader's supported 14-byte absolute detour, ForceNodes validates and chains its destination. Any other unexpected patch form is refused instead of overwritten.

## DLC independence

ForceNodes works with road and pedestrian path systems in the base game and has no mandatory DLC asset, object, research, or entitlement dependency. Optional DLC content may use the same base path systems, but no DLC-created pointer or asset is required for initialization or core operation.

## Stable service scope

ForceNodes service API versions 1, 2, 3, and 6 are published. Experimental versions 4 and 5 are not. Optional native path-preview transaction hooks remain disabled and capability queries return zero.

## Confirmed runtime result

The mod author confirmed v1.8.0 initializes, becomes active, and works in WRSR 1.1.1.9 through TesmioLoader API 4.
