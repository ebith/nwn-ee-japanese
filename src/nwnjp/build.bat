@echo off
rem Builds the hook DLL and the loader into dist\.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "DIST=%~dp0..\..\dist"
if not exist "%DIST%" mkdir "%DIST%"
if not exist "%DIST%\obj" mkdir "%DIST%\obj"
pushd "%~dp0"
cl /nologo /std:c++20 /O2 /EHsc /MT /W4 /LD /Fe:"%DIST%\nwnjp.dll" /Fo:"%DIST%\obj\\" nwnjp.cpp
if errorlevel 1 goto done
cl /nologo /std:c++20 /utf-8 /O2 /EHsc /MT /W4 /Fe:"%DIST%\nwnjp_loader.exe" /Fo:"%DIST%\obj\\" launcher.cpp
:done
set R=%errorlevel%
popd
exit /b %R%
