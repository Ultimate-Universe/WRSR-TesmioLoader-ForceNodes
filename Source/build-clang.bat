@echo off
rem SPDX-License-Identifier: GPL-3.0-only
setlocal EnableExtensions

set "ROOT=%~dp0"
if "%~1"=="" (
  set "OUT=%ROOT%build"
) else (
  set "OUT=%~1"
)
if "%CLANG_CL%"=="" set "CLANG_CL=clang-cl"
if "%LLD_LINK%"=="" set "LLD_LINK=lld-link"
if "%LLVM_RC%"=="" set "LLVM_RC=llvm-rc"

if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\obj" || exit /b 1
mkdir "%OUT%\lib" || exit /b 1
mkdir "%OUT%\plugins" || exit /b 1

"%LLD_LINK%" /lib /nologo /machine:x64 /def:"%ROOT%build_support\kernel32.def" /out:"%OUT%\lib\kernel32.lib" || exit /b 1
"%LLD_LINK%" /lib /nologo /machine:x64 /def:"%ROOT%build_support\ucrtbase.def" /out:"%OUT%\lib\ucrtbase.lib" || exit /b 1

set "CFLAGS=/nologo /c /O2 /Oi /Gy /Gw /GS- /GR- /EHs-c- /Zl /Brepro /W4 /WX /std:c++17 /DWIN32 /D_WIN64 /DNDEBUG /I%ROOT%source\include /I%ROOT%source\src"

"%CLANG_CL%" %CFLAGS% /Fo"%OUT%\obj\Bindings.obj"   "%ROOT%source\src\Bindings.cpp" || exit /b 1
"%CLANG_CL%" %CFLAGS% /Fo"%OUT%\obj\Core.obj"       "%ROOT%source\src\Core.cpp" || exit /b 1
"%CLANG_CL%" %CFLAGS% /Fo"%OUT%\obj\Signatures.obj" "%ROOT%source\src\Signatures.cpp" || exit /b 1
"%CLANG_CL%" %CFLAGS% /Fo"%OUT%\obj\ForceNodes.obj" "%ROOT%source\src\ForceNodes.cpp" || exit /b 1
"%CLANG_CL%" %CFLAGS% /Fo"%OUT%\obj\EngineShim.obj" "%ROOT%source\src\EngineShim.cpp" || exit /b 1

"%LLVM_RC%" /nologo /fo "%OUT%\obj\ForceNodes.res" "%ROOT%build_support\ForceNodes.rc" || exit /b 1
"%LLVM_RC%" /nologo /fo "%OUT%\obj\ForceNodes.Engine.res" "%ROOT%build_support\ForceNodes.Engine.rc" || exit /b 1

set "LFLAGS=/nologo /dll /machine:x64 /subsystem:windows,6.0 /nodefaultlib /dynamicbase /highentropyva /nxcompat /opt:ref /opt:icf /release /Brepro /version:1.8 /entry:DllMain"

"%LLD_LINK%" %LFLAGS% /out:"%OUT%\plugins\ForceNodes.dll" /implib:"%OUT%\lib\ForceNodes.lib" "%OUT%\obj\Bindings.obj" "%OUT%\obj\Core.obj" "%OUT%\obj\Signatures.obj" "%OUT%\obj\ForceNodes.obj" "%OUT%\obj\ForceNodes.res" "%OUT%\lib\kernel32.lib" "%OUT%\lib\ucrtbase.lib" || exit /b 1
"%LLD_LINK%" %LFLAGS% /out:"%OUT%\plugins\ForceNodes.Engine.dll" /implib:"%OUT%\lib\ForceNodes.Engine.lib" "%OUT%\obj\EngineShim.obj" "%OUT%\obj\ForceNodes.Engine.res" "%OUT%\lib\kernel32.lib" || exit /b 1

copy /y "%ROOT%config\ForceNodes.ini" "%OUT%\plugins\ForceNodes.ini" >nul || exit /b 1
python "%ROOT%tools\set_pe_checksum.py" "%OUT%\plugins\ForceNodes.dll" "%OUT%\plugins\ForceNodes.Engine.dll" || exit /b 1
python "%ROOT%tools\test_build.py" --plugins "%OUT%\plugins" --write-sha "%OUT%\SHA256SUMS.txt" || exit /b 1

echo.
echo Built ForceNodes v1.8.0 in "%OUT%\plugins"
type "%OUT%\SHA256SUMS.txt"
endlocal
