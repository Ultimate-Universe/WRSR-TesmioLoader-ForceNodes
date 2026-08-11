#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$ROOT/build}"
ZIG="${ZIG:-zig}"

if [[ "$OUT" == "/" || -z "$OUT" ]]; then
  printf 'Refusing unsafe output directory: %s\n' "$OUT" >&2
  exit 1
fi

mkdir -p "$OUT/obj" "$OUT/lib" "$OUT/plugins"
find "$OUT/obj" "$OUT/lib" "$OUT/plugins" -mindepth 1 -maxdepth 1 -type f -delete

"$ZIG" dlltool -m i386:x86-64 -d "$ROOT/build_support/kernel32.def" -l "$OUT/lib/kernel32.lib"
"$ZIG" dlltool -m i386:x86-64 -d "$ROOT/build_support/ucrtbase.def" -l "$OUT/lib/ucrtbase.lib"

COMMON=(
  -target x86_64-windows-msvc -c -O2 -fno-exceptions -fno-rtti
  -fno-stack-protector -ffunction-sections -fdata-sections -std=c++17
  -DWIN32 -D_WIN64 -DNDEBUG "-I$ROOT/source/include" "-I$ROOT/source/src"
)

compile() {
  "$ZIG" c++ "${COMMON[@]}" "$1" -o "$2"
}

compile "$ROOT/source/src/Bindings.cpp"   "$OUT/obj/Bindings.obj"
compile "$ROOT/source/src/Core.cpp"       "$OUT/obj/Core.obj"
compile "$ROOT/source/src/Signatures.cpp" "$OUT/obj/Signatures.obj"
compile "$ROOT/source/src/ForceNodes.cpp" "$OUT/obj/ForceNodes.obj"
compile "$ROOT/source/src/EngineShim.cpp" "$OUT/obj/EngineShim.obj"

"$ZIG" rc /fo "$OUT/obj/ForceNodes.res" -- "$ROOT/build_support/ForceNodes.rc"
"$ZIG" rc /fo "$OUT/obj/ForceNodes.Engine.res" -- "$ROOT/build_support/ForceNodes.Engine.rc"

"$ZIG" build-lib -target x86_64-windows-msvc -dynamic -fentry=DllMain \
  --subsystem windows -O ReleaseFast -fstrip \
  -femit-bin="$OUT/plugins/ForceNodes.dll" \
  "$OUT/obj/Bindings.obj" "$OUT/obj/Core.obj" "$OUT/obj/Signatures.obj" \
  "$OUT/obj/ForceNodes.obj" "$OUT/obj/ForceNodes.res" \
  "$OUT/lib/kernel32.lib" "$OUT/lib/ucrtbase.lib"

"$ZIG" build-lib -target x86_64-windows-msvc -dynamic -fentry=DllMain \
  --subsystem windows -O ReleaseFast -fstrip \
  -femit-bin="$OUT/plugins/ForceNodes.Engine.dll" \
  "$OUT/obj/EngineShim.obj" "$OUT/obj/ForceNodes.Engine.res" \
  "$OUT/lib/kernel32.lib"

cp "$ROOT/config/ForceNodes.ini" "$OUT/plugins/ForceNodes.ini"
python3 "$ROOT/tools/set_pe_checksum.py" --image-version 1.8 \
  "$OUT/plugins/ForceNodes.dll" "$OUT/plugins/ForceNodes.Engine.dll"
python3 "$ROOT/tools/test_build.py" --plugins "$OUT/plugins" --write-sha "$OUT/SHA256SUMS.txt"

printf '\nBuilt ForceNodes v1.8.0 in %s/plugins\n' "$OUT"
cat "$OUT/SHA256SUMS.txt"
