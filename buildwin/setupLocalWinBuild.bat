@echo off
setlocal enableextensions
goto :main
:usage
@echo ****************************************************************************
@echo *  This script can be used to create a local build environment for OpenCPN.*
@echo *  Here are some basic steps that are known to work.  There are            *
@echo *  probably other ways to do this, but this is the way that works for me.  *
@echo *                                                                          *
@echo *  1. Install Visual Studio 2022 Community Edition.                        *
@echo *              https://visualstudio.microsoft.com/downloads/               *
@echo *  2. Install git for Windows                                              *
@echo *              https://git-scm.com/download/win                            *
@echo *  3. Open 'x86 Native Tools Command Prompt for Visual Studio 2022'        *
@echo *  4. Create folder where you want to work with OpenCPN sources            *
@echo *        Example: mkdir \Users\myname\source\repos                         *
@echo *                 cd \Users\myname\source\repos                            *
@echo *  5. Clone Opencpn                                                        *
@echo *        Example: clone https://github.com/opencpn/opencpn                 *
@echo *                 cd \Users\myname\source\repos\opencpn                    *
@echo *                 git checkout localWinBuild                               *
@echo *  6. Set up local build environment by executing this script              *
@echo *        Example: setupLocalWinBuild.bat                                   *
@echo *  7. Open solution file (type solution file name at VS command prompt)    *
@echo *        Example: .\build\opencpn.sln                                      *
@echo *        Reinitialize build folder: .\build\opencpn.sln --clean            *
@echo *                                                                          *
@echo *  Start building and debugging in Visual Studio.                          *
@echo ****************************************************************************
goto :EOF
:main
set "OD=%~dp0.."
@echo OD=%OD%
set "wxDIR=%OD%\cache\buildwxWidgets"
set "wxWidgets_ROOT_DIR=%wxDIR%"
set "wxWidgets_LIB_DIR=%wxDIR%\lib\vc_dll"
set "OLDPATH=%PATH%"
rem we want these to remain in the environment after completion
@echo set "wxDIR=%wxDIR%" > "%OD%\configdev.bat"
@echo set "wxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR%" >> "%OD%\configdev.bat"
@echo set "wxWidgets_LIB_DIR=%wxWidgets_LIB_DIR%" >> "%OD%\configdev.bat"

SET "CACHE_DIR=%OD%\cache"
SET "buildWINtmp=%CACHE_DIR%\buildwintemp"

set PSH=powershell
where pwsh > NUL 2> NUL && set PSH=pwsh
where msbuild && goto :vsok
@echo Please run this from "x86 Native Tools Command Prompt for VS2022
goto :usage

:vsok

:save

rem Save user configuration
set folder=Release
call :backup
set folder=RelWithDebInfo
call :backup
set folder=Debug
call :backup
set folder=MinSizeRel
call :backup

rem Shall we start from scratch?
if [%1]==[--clean] (^
  if exist "%OD%\build" (^
   @echo Clearing "%OD%\build"
   rmdir /s /q "%OD%\build"
  )
  if exist "%CACHE_DIR%" (rmdir /s /q "%CACHE_DIR%")
  if exist "%buildWINtmp%" (rmdir /s /q "%buildWINtmp%")
)

if not exist "%OD%\build" (mkdir "%OD%\build")
if not exist "%CACHE_DIR%" (mkdir "%CACHE_DIR%")
if not exist "%CACHE_DIR%\buildwin" (mkdir "%CACHE_DIR%\buildwin")
if not exist "%buildWINtmp%" (mkdir "%buildWINtmp%")

rem Get dependencies
@echo Downloading nuget
set "URL=https://dist.nuget.org/win-x86-commandline/latest/nuget.exe"
set "DEST=%buildWINtmp%\nuget.exe"
call :download
if errorlevel 1 (exit /b 1) 

@echo Downloading Windows depencencies from OpenCPN repository
set "URL=https://github.com/OpenCPN/OCPNWindowsCoreBuildSupport/archive/refs/tags/v0.3.zip"
set "DEST=%buildWINtmp%\OCPNWindowsCoreBuildSupport.zip"
call :download

@echo Exploding Windows dependencies
set "SOURCE=%DEST%"
set "DEST=%buildWINtmp%"
call :explode
if errorlevel 1 (echo not OK) else (
  xcopy /e /q /y "%buildWINtmp%\OCPNWindowsCoreBuildSupport-0.3\buildwin" "%CACHE_DIR%\buildwin"
  if errorlevel 1 (echo NOT OK) else (echo OK))
if not exist "%wxDIR%" (
  @echo Downloading wxWidgets sources
  mkdir "%wxDIR%"
  set "URL=https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.2.1/wxWidgets-3.2.2.1.zip"
  set "DEST=%wxDIR%\wxWidgets-3.2.2.1.zip"
  call :download
  if errorlevel 1 (echo not OK) else (echo OK)

  @echo exploding wxWidgets
  set "SOURCE=%wxDIR%\wxWidgets-3.2.2.1.zip"
  set "DEST=%wxDIR%"
  call :explode
  if errorlevel 1 (echo not OK) else (echo OK)

  @echo Downloading Windows WebView2 kit
  set "URL=https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2"
  set "DEST=%wxDIR%\webview2.zip"
  call :download
  if errorlevel 1 (echo not OK) else (echo OK)

  @echo Exploding WebView2
  set "SOURCE=%wxDIR%\webview2.zip"
  set "DEST=%wxDIR%\build\3rdparty\webview2"
  call :explode
  if errorlevel 1 (echo not OK) else (echo OK)
)

@echo Building wxWidgets
cd %wxDIR%
rem if not exist cmake (mkdir cmake)
rem cmake -E chdir %WXDIR%\cmake cmake -DCMAKE_CONFIGURATION_TYPES="Debug;RelWithDebInfo;Release;MinSizeRel" -dwxBUILD_VENDOR="vc14x" -DwxBUILD_SAMPLES:STRING="OFF" -DwxBUILD_TESTS:STRING="OFF" -G "Visual Studio 17 2022" -T "v143" -A Win32 ..
msbuild /noLogo /m "-p:Configuration=DLL Debug" ^
  -p:Platform=Win32;wxVendor=14x;wxVersionString=32;wxToolkitDllNameSuffix="_vc14x" ^
  -t:Rebuild build\msw\wx_vc17.sln
msbuild /noLogo /m "-p:Configuration=DLL Release" ^
  -p:Platform=Win32;wxVendor=14x;wxVersionString=32;wxToolkitDllNameSuffix="_vc14x" ^
  -t:Rebuild build\msw\wx_vc17.sln
if not exist "%CACHE_DIR%\buildwin\wxWidgets" (
    mkdir "%CACHE_DIR%\buildwin\wxWidgets"
)
xcopy /e /q /y "%WXDIR%\lib\vc_dll\" "%CACHE_DIR%\buildwin\wxWidgets"
if not exist "%CACHE_DIR%\buildwin\wxWidgets\locale" (
  mkdir "%CACHE_DIR%\buildwin\wxWidgets\locale"
)
xcopy /e /q /y "%WXDIR%\locale\" "%CACHE_DIR%\buildwin\wxWidgets\locale"
cd
@echo cd %OD%\build
cd "%OD%\build"
cd
rem Copy files needed to run OpenCPN
call "%OD%\buildwin\docopyAll.bat" Debug
call "%OD%\buildwin\docopyAll.bat" RelWithDebInfo
call "%OD%\buildwin\docopyAll.bat" MinSizeRel
call "%OD%\buildwin\docopyAll.bat" Release

rem restore user configurations if we saved them
set folder=Release
call :restore
set folder=RelWithDebInfo
call :restore
set folder=Debug
call :restore
set folder=MinSizeRel
call :restore

rem Install some tools CMake will need
%buildWINtmp%\nuget install Gettext.Tools
%buildWINtmp%\nuget install NSIS-Package

for /D %%D in ("Gettext*") do (set gettext=%%~D)
for /D %%D in ("NSIS-Package*") do (set nsis=%%~D)
@echo gettext=%gettext%
@echo nsis=%nsis%

@echo Finishing %OD%\configdev.bat
set "_addpath=%OD%\build\%nsis%\NSIS\;%OD%\build\%nsis%\NSIS\bin\"
set "_addpath=%_addpath%;%OD%\build\%gettext%\tools\bin\"
@echo SET "PATH=%%PATH%%;%_addpath%" >> "%OD%\configdev.bat"
@echo set _addpath= >> "%OD%\configdev.bat"
@echo cd "%OD%\build" >> "%OD%\configdev.bat"
@echo exit /b 0 >> "%OD%\configdev.bat"
cd %OD%
endlocal

rem set environment for VS development
call configdev.bat
cmake -A Win32 -G "Visual Studio 17 2022" ^
  -DCMAKE_GENERATOR_PLATFORM=Win32 ^
  -DwxWidgets_LIB_DIR="%wxWidgets_LIB_DIR%" ^
  -DwxWidgets_ROOT_DIR="%wxWidgets_ROOT_DIR%" ^
  -DCMAKE_CXX_FLAGS="/MP /EHsc /DWIN32" ^
  -DCMAKE_C_FLAGS="/MP" ^
  -DOCPN_CI_BUILD=OFF ^
  -DOCPN_TARGET_TUPLE=msvc-wx32;10;x86_64 ^
  -DOCPN_BUNDLE_WXDLLS=ON ^
  -DOCPN_BUILD_TEST=OFF ^
  ..
if errorlevel 1 goto :cmakeErr
if exist opencpn.sln (
  msbuild /noLogo /m -p:Configuration=Debug -p:Platform=Win32 opencpn.sln
)
if exist opencpn.sln (
  msbuild /noLogo /m -p:Configuration=RelWithDebInfo -p:Platform=Win32 opencpn.sln
)

@echo To build OpenCPN for debugging at command line do this in the folder
@echo where you cloned OpenCPN:
@echo.
@echo  configdev.bat ^&^& cd build
@echo  msbuild /noLogo /m -p:Configuration=Debug -p:Platform=Win32 opencpn.sln
@echo  devenv opencpn.sln
@echo.
@echo Now you are ready to start debugging
@echo.
@echo [101;93mIf you close this CMD prompt and open another be sure to run:[0m
@echo  configdev.bat
@echo [101;93mfirst before starting Visual Studio[0m.
goto :EOF

:cmakeErr
@echo CMake failed to configure OpenCPN build folder.
@echo Review the error messages and read the OpenCPN Developer Manual for help.
goto :EOF

rem Local subroutines

:backup
@echo "Backing up %OD%\build\%folder%"
if not exist "%OD%\build\%folder%" goto :breturn
if not exist "%OD%\tmp" (mkdir "%OD%\tmp")
if not exist "%OD%\tmp\%folder%" (mkdir "%OD%\tmp\%folder%")
@echo backing up %folder%
xcopy /Q /Y "%OD%\build\%folder%\opencpn.ini" "%OD%\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.log.log" "%OD%\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.log" "%OD%\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.dat" "%OD%\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.csv" "%OD%\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\navobj.*" "%OD%\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\navobj.xml.?" "%OD%\tmp\%folder%"
if not exist "%OD%\build\%folder%\Charts" goto :breturn
@echo cmake -E copy_directory "%OD%\build\%folder%\Charts" "tmp\%folder%"
cmake -E copy_directory "%OD%\build\%folder%\Charts" "tmp\%folder%"
:breturn
@echo backup returning
exit /b 0

:restore
if not exist "%OD%\tmp\%folder%" goto :rreturn
@echo Restoring %folder% settings
cmake -E copy_directory "%OD%\tmp\%folder%" "%OD%\build\%folder%"
if errorlevel 0 (rmdir /s /q "%OD%\tmp\%folder%")

:rreturn
@echo restore returning
exit /b 0

:download
@echo URL=%URL%
@echo DEST=%DEST%
%PSH% -Command [System.Net.ServicePointManager]::MaxServicePointIdleTime = 5000000; ^
  if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; ^
  Invoke-WebRequest "%URL%" -OutFile '%DEST%'; ^
  exit $LASTEXITCODE
exit /b

:explode
@echo SOURCE=%SOURCE%
@echo DEST=%DEST%
%PSH% -Command if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; ^
  Expand-Archive -Force -Path '%SOURCE%' -DestinationPath '%DEST%'
exit /b