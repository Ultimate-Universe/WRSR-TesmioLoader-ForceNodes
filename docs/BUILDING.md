# Building ForceNodes

## Toolchain

ForceNodes uses an SDK-independent LLVM build so the same source can be reproduced from a clean checkout without Visual Studio project files.

Required commands:

- `clang-cl`
- `lld-link`
- Python 3

LLVM 17 or newer is recommended. The shipped build imports only functions that are part of 64-bit Windows and `ucrtbase.dll`.

## Windows

Open a command prompt in the repository root:

```bat
build-clang.bat
```

An alternative output folder may be passed as the first argument:

```bat
build-clang.bat C:\temp\ForceNodes-build
```

## Linux or other LLVM host

The installed `clang-cl` must target `x86_64-pc-windows-msvc` and `lld-link` must be available:

```bash
./build-clang.sh
```

Or select an output folder:

```bash
./build-clang.sh /tmp/ForceNodes-build
```

The commands may be overridden with `CLANG_CL` and `LLD_LINK` environment variables.

## Build output

```text
build/plugins/ForceNodes.dll
build/plugins/ForceNodes.Engine.dll
build/plugins/ForceNodes.ini
build/SHA256SUMS.txt
```

Intermediate objects and generated import libraries remain under `build/` and are excluded by `.gitignore`.

## Why import-library definitions are included

`build_support/kernel32.def` and `build_support/ucrtbase.def` list the small set of conventional imports used by the source. `lld-link /lib` turns these definitions into ordinary import libraries during every clean build. No binary SDK library or opaque build dependency is committed.

## Release checks

The build script automatically runs:

```bash
python tools/test_build.py --plugins build/plugins
```

This verifies:

- x86-64 DLL architecture;
- exact TesmioLoader exports;
- conventional import tables and expected dependencies;
- x64 unwind metadata;
- ASLR, high-entropy VA, and NX flags;
- nonzero release checksums;
- absence of writable-executable sections and CLR metadata;
- embedded release version and default configuration;
- SHA-256 hashes.

To audit against game files as well:

```bash
python tools/test_build.py \
  --plugins build/plugins \
  --game "C:\path\to\SOVIET64.exe" \
  --c3d "C:\path\to\C3DDLL64.dll" \
  --write-sha build/SHA256SUMS.txt
```

The additional checks require every ForceNodes signature to match exactly once at its verified current RVA and every required C3D export to exist.

## Determinism

The release link uses LLVM's reproducible-build mode. Two clean builds with the same source and LLVM toolchain produce byte-identical DLLs. Compiler or linker version changes may legitimately alter the output while preserving functionality.
