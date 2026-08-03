@rem SPDX-License-Identifier: GPL-3.0-only
@echo off
setlocal
if not exist out mkdir out
clang-cl --target=x86_64-pc-windows-msvc /c /O2 /W4 /GS- /GR- /EHs-c- /Zl /Oi /Foout\ForceNodes.obj ForceNodes.cpp || exit /b 1
lld-link /dll /noentry /machine:x64 /opt:ref /opt:icf /out:out\ForceNodes.dll /export:TsmPluginApiVersion /export:TsmPluginInit /export:TsmPluginStart out\ForceNodes.obj || exit /b 1
echo Built ForceNodes v1.6.0 stable.
