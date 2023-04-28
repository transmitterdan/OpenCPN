@echo off
setlocal enableextensions
goto :main
:: ***************************************************************************
:: *
:: * Project:  OpenCPN
:: * Purpose:  Windows local build script
:: * Author:   Dan Dickey
:: *
:: ***************************************************************************
:: *   Copyright (C) 2010-2023 by David S. Register                          *
:: *                                                                         *
:: *   This program is free software; you can redistribute it and/or modify  *
:: *   it under the terms of the GNU General Public License as published by  *
:: *   the Free Software Foundation; either version 2 of the License, or     *
:: *   (at your option) any later version.                                   *
:: *                                                                         *
:: *   This program is distributed in the hope that it will be useful,       *
:: *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
:: *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
:: *   GNU General Public License for more details.                          *
:: *                                                                         *
:: *   You should have received a copy of the GNU General Public License     *
:: *   along with this program; if not, write to the                         *
:: *   Free Software Foundation, Inc.,                                       *
:: *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
:: ***************************************************************************
:: *
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
@echo *        Example: .\buildwin\winConfig.bat                                 *
@echo *  7. Open solution file (type solution file name at VS command prompt)    *
@echo *        Example: .\build\opencpn.sln                                      *
@echo *  8. Start building and debugging in Visual Studio.                       *
@echo *                                                                          *
@echo *  Command line options:                                                   *
@echo *      --clean            Clean build folder entirely first                *
@echo *      --debug            Create Debug configuration                       *
@echo *      --release          Create Relase configuration                      *
@echo *      --relwithdebinfo   Create RelWithDebInfo configuration              *
@echo *      --minsizerel       Create MinSizeRel configuration                  *
@echo *      --all              Create all 4 configurations                      *
@echo *                                                                          *
@echo ****************************************************************************
goto :EOF
:main
::-------------------------------------------------------------
:: Initialize local environment
::-------------------------------------------------------------
set "OD=%~dp0.."
@echo OD=%OD%
set "OCPN_Dir=%OD%"
set "wxDIR=%OD%\cache\buildwxWidgets"
set "wxWidgets_ROOT_DIR=%wxDIR%"
set "wxWidgets_LIB_DIR=%wxDIR%\lib\vc_dll"
if [%VisualStudioVersion%]==[16.0] (^
  set VCver=16
  set "VCstr=Visual Studio 16"
)
if [%VisualStudioVersion%]==[17.0] (^
  set VCver=17
  set "VCstr=Visual Studio 17"
)
::-------------------------------------------------------------
:: Initialize local helper script to reinitialize environment
::-------------------------------------------------------------
@echo set "OCPN_Dir=%OCPN_Dir%" > "%OD%\buildwin\configdev.bat"
@echo set "wxDIR=%wxDIR%" >> "%OD%\buildwin\configdev.bat"
@echo set "wxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR%" >> "%OD%\buildwin\configdev.bat"
@echo set "wxWidgets_LIB_DIR=%wxWidgets_LIB_DIR%" >> "%OD%\buildwin\configdev.bat"
@echo set "VCver=%VCver%" >> "%OD%\buildwin\configdev.bat"
@echo set "VCstr=%VCstr%" >> "%OD%\buildwin\configdev.bat"
::-------------------------------------------------------------
:: Initialize local variables
::-------------------------------------------------------------
SET "CACHE_DIR=%OD%\cache"
SET "buildWINtmp=%CACHE_DIR%\buildwintemp"
set PSH=powershell
where pwsh > NUL 2> NUL && set PSH=pwsh
where %PSH% > NUL 2> NUL || @echo PowerShell is not installed && exit /b 1
where msbuild && goto :vsok
@echo Please run this from "x86 Native Tools Command Prompt for VS2022
goto :usage
:vsok

set ocpn_clean=0
set ocpn_minsizerel=0
set ocpn_release=1
set ocpn_relwitdebinfo=0
set ocpn_debug=1
:parse
if [%1]==[--clean] (set ocpn_clean=1&& shift /1 && goto :parse)
if [%1]==[--minsizerel] (set ocpn_minsizerel=1&& shift /1 && goto :parse)
if [%1]==[--relwithdebinfo] (set ocpn_relwithdebinfo=1&& shift /1 && goto :parse)
if [%1]==[--release] (set ocpn_release=1&& shift /1 && goto :parse)
if [%1]==[--debug] (set ocpn_debug=1&& shift /1 && goto :parse)
if [%1]==[--all] (^
  set ocpn_debug=1
  set ocpn_release=1
  set ocpn_minsizerel=1
  set ocpn_relwithdebinfo=1
  shift /1
  goto :parse
  )
if [%1]==[] (goto :begin) else (^
  @echo Unknown option: %1
  shift /1
  goto :usage
  )
:begin
::-------------------------------------------------------------
:: Save user configuration data and wipe the build folders
::-------------------------------------------------------------
if [%ocpn_clean%]==[1] (
  set folder=Release
  call :backup
  set folder=RelWithDebInfo
  call :backup
  set folder=Debug
  call :backup
  set folder=MinSizeRel
  call :backup
  set folder=
  @echo Backup complete
  timeout /T 1
  if exist "%OD%\build" (rmdir /s /q "%OD%\build" && @echo Cleared %OD%\build)
  if exist "%CACHE_DIR%" (rmdir /s /q "%CACHE_DIR%" && @echo Cleared %CACHE_DIR%)
  if exist "%buildWINtmp%" (rmdir /s /q "%buildWINtmp%" && @echo Cleared %buildWINtmp%)
)
::-------------------------------------------------------------
:: Create needed folders
::-------------------------------------------------------------
if not exist "%OD%\build" (mkdir "%OD%\build")
if not exist "%CACHE_DIR%" (mkdir "%CACHE_DIR%")
if not exist "%CACHE_DIR%\buildwin" (mkdir "%CACHE_DIR%\buildwin")
if not exist "%buildWINtmp%" (mkdir "%buildWINtmp%")

::-------------------------------------------------------------
:: Install nuget
::-------------------------------------------------------------
@echo Downloading nuget
set "URL=https://dist.nuget.org/win-x86-commandline/latest/nuget.exe"
set "DEST=%buildWINtmp%\nuget.exe"
call :download
::-------------------------------------------------------------
:: Download OpenCPN Core dependencies
::-------------------------------------------------------------
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
::-------------------------------------------------------------
:: Download wxWidgets 3.2.2 sources
::-------------------------------------------------------------
if exist "%wxDIR%\build\msw\wx_vc%VCver%.sln" (goto :skipwxDL)
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

:skipwxDL
::-------------------------------------------------------------
:: Build wxWidgets from sources
::-------------------------------------------------------------
@echo Building wxWidgets

msbuild /noLogo /v:m /m "-p:Configuration=DLL Debug";Platform=Win32 ^
  -p:wxVendor=14x;wxVersionString=32;wxToolkitDllNameSuffix="_vc14x" ^
  /l:FileLogger,Microsoft.Build.Engine;logfile=%CACHE_DIR%\buildwin\wxWidgets\MSBuild_DEBUG_WIN32_Debug.log ^
  "%wxDIR%\build\msw\wx_vc%VCver%.sln"

msbuild /noLogo /v:m /m "-p:Configuration=DLL Release";Platform=Win32 ^
  -p:wxVendor=14x;wxVersionString=32;wxToolkitDllNameSuffix="_vc14x" ^
  /l:FileLogger,Microsoft.Build.Engine;logfile=%CACHE_DIR%\buildwin\wxWidgets\MSBuild_RELEASE_WIN32_Debug.log ^
  "%wxDIR%\build\msw\wx_vc%VCver%.sln"

if not exist "%CACHE_DIR%\buildwin\wxWidgets" (
    mkdir "%CACHE_DIR%\buildwin\wxWidgets"
)
xcopy /e /q /y "%WXDIR%\lib\vc_dll\" "%CACHE_DIR%\buildwin\wxWidgets"
if not exist "%CACHE_DIR%\buildwin\wxWidgets\locale" (
  mkdir "%CACHE_DIR%\buildwin\wxWidgets\locale"
)
xcopy /e /q /y "%WXDIR%\locale\" "%CACHE_DIR%\buildwin\wxWidgets\locale"
::-------------------------------------------------------------
:: Initialize the build folder
::-------------------------------------------------------------
@echo cd %OD%\build
cd "%OD%\build"
::-------------------------------------------------------------
:: Initialize folders needed to run OpenCPN (must be in build folder)
:: Restore user configurations
::-------------------------------------------------------------
if [%ocpn_debug%]==[1] (^
  call "%OD%\buildwin\docopyAll.bat" Debug
  set folder=Debug
  call :restore
  )
if [%ocpn_release%]==[1] (^
  call "%OD%\buildwin\docopyAll.bat" Release
  set folder=Release
  call :restore
  )
if [%ocpn_relwithdebinfo%]==[1] (^
  call "%OD%\buildwin\docopyAll.bat" RelWithDebInfo
  set folder=RelWithDebInfo
  call :restore
  )
if [%ocpn_minsizerel%]==[1] (^
  call "%OD%\buildwin\docopyAll.bat" MinSizeRel
  set folder=MinSizeRel
  call :restore
  )
set folder=
::-------------------------------------------------------------
:: Download and initialize build dependencies
::-------------------------------------------------------------
%buildWINtmp%\nuget install Gettext.Tools
%buildWINtmp%\nuget install NSIS-Package
for /D %%D in ("Gettext*") do (set gettext=%%~D)
for /D %%D in ("NSIS-Package*") do (set nsis=%%~D)
@echo gettext=%gettext%
@echo nsis=%nsis%
::-------------------------------------------------------------
:: Finalize local environment helper script
::-------------------------------------------------------------
@echo Finishing %OD%\buildwin\configdev.bat
set "_addpath=%OD%\build\%nsis%\NSIS\;%OD%\build\%nsis%\NSIS\bin\"
set "_addpath=%_addpath%;%OD%\build\%gettext%\tools\bin\"
@echo path^|find /i "%_addpath%"    ^>nul ^|^| set "path=%path%;%_addpath%" >> "%OD%\buildwin\configdev.bat"
@set _addpath=
@echo cd "%OD%\build" >> "%OD%\buildwin\configdev.bat"
endlocal
::-------------------------------------------------------------
:: Change to build folder
::-------------------------------------------------------------
cd /D "%~dp0.."
@echo In folder %CD%
if exist .\buildwin\configdev.bat (call .\buildwin\configdev.bat) else (goto :hint)
::-------------------------------------------------------------
:: Build Release and Debug executables
::-------------------------------------------------------------
if exist .\RelWithDebInfo (^
  @echo Building RelWithDebInfo
  set build_type=RelWithDebInfo
  call :config_build
  )
if exist .\Release (^
  @echo Building Release
  set build_type=Release
  call :config_build
  )
if exist .\MinSizeRel (^
  @echo Building MinSizeRel
  set build_type=MinSizeRel
  call :config_build
  )
if exist .\Debug (^
  @echo Building Debug
  set build_type=Debug
  call :config_build
  )

set build_type=
::-------------------------------------------------------------
:: Offer some helpful hints
::-------------------------------------------------------------
:hint
cd ..
@echo To build OpenCPN for debugging at command line do this in the folder
@echo %CD%
@echo.
@echo  .\buildwin\configdev.bat
@echo  msbuild /noLogo /m -p:Configuration=Debug;Platform=Win32 opencpn.sln
@echo  opencpn.sln
@echo.
@echo Now you are ready to start debugging
@echo.
@echo [101;93mIf you close this CMD prompt and open another be sure to run:[0m
@echo  %CD%\buildwin\configdev.bat
@echo [101;93mfirst, before starting Visual Studio[0m.
cd build
goto :EOF
::-------------------------------------------------------------
:: Local subroutines
::-------------------------------------------------------------
::-------------------------------------------------------------
:: Config and build
::-------------------------------------------------------------
:config_build
cmake -A Win32 -G "%VCstr%" ^
  -DCMAKE_GENERATOR_PLATFORM=Win32 ^
  -DwxWidgets_LIB_DIR="%wxWidgets_LIB_DIR%" ^
  -DwxWidgets_ROOT_DIR="%wxWidgets_ROOT_DIR%" ^
  -DCMAKE_CXX_FLAGS="/MP /EHsc /DWIN32" ^
  -DCMAKE_C_FLAGS="/MP" ^
  -DOCPN_CI_BUILD=OFF ^
  -DOCPN_TARGET_TUPLE=msvc-wx32;10;x86_64 ^
  -DOCPN_BUNDLE_WXDLLS=ON ^
  -DOCPN_BUILD_TEST=OFF ^
  -DCMAKE_BUILD_TYPE=%buildtype% ^
  -DCMAKE_INSTALL_PREFIX="%CD%\%build_type%" ^
  ..
if errorlevel 1 goto :cmakeErr

msbuild /noLogo /v:m /m -p:Configuration=%build_type%;Platform=Win32 ^
/l:FileLogger,Microsoft.Build.Engine;logfile=%CD%\MSBuild_%build_type%_WIN32_Debug.log ^
INSTALL.vcxproj
if errorlevel 1 goto :buildErr
@echo OpenCPN %build_type% build successful!
@echo.
exit /b 0
::-------------------------------------------------------------
:: CMake failed
::-------------------------------------------------------------
:cmakeErr
@echo CMake failed to configure OpenCPN build folder.
@echo Review the error messages and read the OpenCPN
@echo Developer Manual for help.
exit /b 1
::-------------------------------------------------------------
:: Build failed
::-------------------------------------------------------------
:buildErr
@echo Build using msbuild failed.
@echo Review the error messages and read the OpenCPN
@echo Developer Manual for help.
exit /b 1
::-------------------------------------------------------------
:: Backup user configuration
::-------------------------------------------------------------
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
@echo cmake -E copy_directory "%OD%\build\%folder%\plugins" "tmp\%folder%"
cmake -E copy_directory "%OD%\build\%folder%\plugins" "tmp\%folder%"
if not exist "%OD%\build\%folder%\Charts" goto :breturn
@echo cmake -E copy_directory "%OD%\build\%folder%\Charts" "tmp\%folder%"
cmake -E copy_directory "%OD%\build\%folder%\Charts" "tmp\%folder%"
:breturn
@echo backup returning
exit /b 0
::-------------------------------------------------------------
:: Restore user configuration to build folder
::-------------------------------------------------------------
:restore
if not exist "%OD%\tmp\%folder%" (exit /b 0)
@echo Restoring %folder% settings
cmake -E copy_directory "%OD%\tmp\%folder%" "%OD%\build\%folder%"
if errorlevel 1 (
  @echo Restore failed
  goto ::rreturn
  ) else (rmdir /s /q "%OD%\tmp\%folder%")
:rreturn
@echo restore returning
exit /b 0
::-------------------------------------------------------------
:: Download URL to a DEST folder
::-------------------------------------------------------------
:download
@echo URL=%URL%
@echo DEST=%DEST%
%PSH% -Command [System.Net.ServicePointManager]::MaxServicePointIdleTime = 5000000; ^
  if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; ^
  Invoke-WebRequest "%URL%" -OutFile '%DEST%'; ^
  exit $LASTEXITCODE
if errorlevel 1 (exit /b 1) else (@echo Download OK)
exit /b 0
::-------------------------------------------------------------
:: Explode SOURCE zip file to DEST folder
::-------------------------------------------------------------
:explode
@echo SOURCE=%SOURCE%
@echo DEST=%DEST%
%PSH% -Command if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; ^
  Expand-Archive -Force -Path '%SOURCE%' -DestinationPath '%DEST%'
if errorlevel 1 (exit /b 1) else (@echo Unzip OK)
exit /b 0
