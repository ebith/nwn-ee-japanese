@echo off
rem Builds everything that is shipped and leaves it in dist\.
rem
rem dist\ holds the two binaries and the installer, so that a release
rem is just that folder. Nothing in dist\ is committed.
setlocal
call "%~dp0nwnjp\build.bat"
if errorlevel 1 exit /b %errorlevel%
copy /y "%~dp0install\install.bat" "%~dp0..\dist\install.bat" >nul
echo Built into dist\
exit /b 0
