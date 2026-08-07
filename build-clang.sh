#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$ROOT/build}"
CLANG_CL="${CLANG_CL:-clang-cl}"
LLD_LINK="${LLD_LINK:-lld-link}"

rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/lib" "$OUT/plugins"

"$LLD_LINK" /lib /nologo /machine:x64 \
  /def:"$ROOT/build_support/kernel32.def" /out:"$OUT/lib/kernel32.lib"
"$LLD_LINK" /lib /nologo /machine:x64 \
  /def:"$ROOT/build_support/ucrtbase.def" /out:"$OUT/lib/ucrtbase.lib"

COMMON=(
  /nologo /c /O2 /Oi /Gy /Gw /GS- /GR- /EHs-c- /Zl /Brepro
  /W4 /WX /std:c++17 /DWIN32 /D_WIN64 /DNDEBUG
  "/I$ROOT/source/include" "/I$ROOT/source/src"
)

compile() {
  local source="$1" object="$2"
  "$CLANG_CL" "${COMMON[@]}" "/Fo$object" "$source"
}

compile "$ROOT/source/src/Bindings.cpp"   "$OUT/obj/Bindings.obj"
compile "$ROOT/source/src/Core.cpp"       "$OUT/obj/Core.obj"
compile "$ROOT/source/src/Signatures.cpp" "$OUT/obj/Signatures.obj"
compile "$ROOT/source/src/ForceNodes.cpp" "$OUT/obj/ForceNodes.obj"
compile "$ROOT/source/src/EngineShim.cpp" "$OUT/obj/EngineShim.obj"

LINK_COMMON=(
  /nologo /dll /machine:x64 /subsystem:windows,6.0 /nodefaultlib
  /dynamicbase /highentropyva /nxcompat /opt:ref /opt:icf /release /Brepro /version:1.7
)

"$LLD_LINK" "${LINK_COMMON[@]}" /entry:DllMain \
  /out:"$OUT/plugins/ForceNodes.dll" /implib:"$OUT/lib/ForceNodes.lib" \
  "$OUT/obj/Bindings.obj" "$OUT/obj/Core.obj" "$OUT/obj/Signatures.obj" "$OUT/obj/ForceNodes.obj" \
  "$OUT/lib/kernel32.lib" "$OUT/lib/ucrtbase.lib"

"$LLD_LINK" "${LINK_COMMON[@]}" /entry:DllMain \
  /out:"$OUT/plugins/ForceNodes.Engine.dll" /implib:"$OUT/lib/ForceNodes.Engine.lib" \
  "$OUT/obj/EngineShim.obj" "$OUT/lib/kernel32.lib"

cp "$ROOT/config/ForceNodes.ini" "$OUT/plugins/ForceNodes.ini"
python3 "$ROOT/tools/test_build.py" --plugins "$OUT/plugins" --write-sha "$OUT/SHA256SUMS.txt"

printf '\nBuilt ForceNodes v1.7.0 in %s/plugins\n' "$OUT"
cat "$OUT/SHA256SUMS.txt"
