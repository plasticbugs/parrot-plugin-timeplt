@echo off
rem sim\run.bat PLUGIN_DIR [options] -- sim/run.sh for Windows' own command
rem prompt (docs/simulator.md). It builds the simulator for the plugin in
rem build-sim\NAME with CMake (Visual Studio's, or vcpkg's SDL3 through
rem CMAKE_TOOLCHAIN_FILE) and runs it; every option after PLUGIN_DIR goes to
rem the simulator ("sim\run.bat src --help" lists them). For a plugin that
rem needs more sources to build, docs/simulator.md has the CMake lines.
setlocal
if "%~1"=="" (
    echo usage: sim\run.bat PLUGIN_DIR [options]
    exit /b 2
)
for %%K in ("%~dp0..") do set "kit=%%~fK"
set "dir=%~f1"
if "%dir:~-1%"=="\" set "dir=%dir:~0,-1%"
for %%D in ("%dir%") do set "name=%%~nxD"
if /i "%name%"=="src" for %%P in ("%dir%\..") do set "name=%%~nxP"
set "build=%kit%\build-sim\%name%"
if not exist "%build%" mkdir "%build%"
cmake -S "%kit%\sim" -B "%build%" -DPLUGIN_DIR="%dir%" > "%build%\configure.log" 2>&1
if errorlevel 1 (
    type "%build%\configure.log"
    exit /b 1
)
cmake --build "%build%" --config Release > "%build%\build.log" 2>&1
if errorlevel 1 (
    type "%build%\build.log"
    echo sim\run.bat: the build failed
    exit /b 1
)
shift
set args=
:more
if "%~1"=="" goto run
set args=%args% %1
shift
goto more
:run
"%build%\parrotsim.exe" --out "%build%\out" %args%
