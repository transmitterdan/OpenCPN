@echo off
@setlocal enableextensions
goto :main
:usage
@echo ****************************************************************************
@echo *  This script can be used to create a local build environment for OpenCPN.*
@echo *  Here are some basic steps that are known to work.  There are            *
@echo *  probably other ways to do this, but this is the way that works for me.  *
@echo *                                                                          *
@echo *  1. Install Visual Studio 2022 Community Edition.                        *
@echo *              https://visualstudio.microsoft.com/downloads/               *
@echo *  2. Install PoEdit (https://poedit.net/download/)                        *
@echo *     Be sure the program 'xgettext.exe' is in the environment %PATH%      *
@echo *     Usually: C:\Program Files (x86)\Poedit\GettextTools\bin\xgettext.exe *
@echo *  3. Install NullSoft Installer (https://nsis.sourceforge.io/Download)    *
@echo *  4. Open 'x86 Native Tools Command Prompt for Visual Studio 2022'        *
@echo *  5. Create folder where you want to work with OpenCPN sources            *
@echo *        Example: mkdir \Users\myname\source\repos                         *
@echo *                 cd \Users\myname\source\repos                            *
@echo *  6. Clone Opencpn                                                        *
@echo *        Example: clone https://github.com/opencpn/opencpn                 *
@echo *                 cd \Users\myname\source\repos\opencpn                    *
@echo *  7. Set up local build environment by executing this script              *
@echo *        Example: setupLocalWinBuild.bat                                   *
@echo *  8. Open solution file (type solution file name at VS command prompt)    *
@echo *        Example: .\build\opencpn.sln                                      *
@echo *                                                                          *
@echo *  Start building and debugging in Visual Studio.                          *
@echo ****************************************************************************
goto :EOF
:main
set "OD=%CD%
@echo "OD=%OD%"

where msbuild && goto :vsok
@echo Please run this from "x86 Native Tools Command Prompt for VS2022
goto :usage

:vsok
where cmake && goto :cmakeok
@echo Please install cmake first
goto :usage

:cmakeok
where xgettext && goto :save
@echo Please install poedit first
goto :usage

:save

set folder=Release
call :backup
@echo returned
set folder=RelWithDebInfo
call :backup
set folder=Debug
call :backup
set folder=MinSizeRel
call :backup

if not exist "%OD%\build" (mkdir "%OD%\build") else (rmdir /s /q "%OD%\build" & mkdir "%OD%\build")
SET wxBuild="%OD%\build\buildwxWidgets"
SET buildWINtmp="%OD%\build\buildwintemp"
SET CACHE_DIR="%OD%\cache"
@echo wxBuild="%OD%\build\buildwxWidgets"
@echo buildWINtmp="%OD%\build\buildwintemp"
@echo CACHE_DIR="%OD%\cache"
if exist "%CACHE_DIR%" (rmdir /s /q "%CACHE_DIR%")
if not exist "%CACHE_DIR%" (mkdir "%CACHE_DIR%")
if not exist "%CACHE_DIR%\buildwin" (mkdir "%CACHE_DIR%\buildwin")


set PSH=powershell
where pwsh > NUL 2> NUL && set PSH=pwsh

@echo Downloading Windows depencencies from OpenCPN repository
if exist "%buildWINTmp%" (rmdir /s /q "%buildWINtmp%")
mkdir "%buildWINtmp%"
%PSH% -Command "[System.Net.ServicePointManager]::MaxServicePointIdleTime = 5000000; if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; Invoke-WebRequest https://github.com/OpenCPN/OCPNWindowsCoreBuildSupport/archive/refs/tags/v0.3.zip   -OutFile '%buildWINtmp%\OCPNWindowsCoreBuildSupport.zip'; exit $LASTEXITCODE"
%PSH% -Command "if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; Expand-Archive -Path '%buildWINtmp%\\OCPNWindowsCoreBuildSupport.zip' -DestinationPath '%buildWINtmp%' "
if errorlevel 1 (echo not OK) else (xcopy /e /q /y "%buildWINtmp%\OCPNWindowsCoreBuildSupport-0.3\buildwin" "%CACHE_DIR%\buildwin" && echo OK)

@echo Downloading wxWidgets sources
if exist "%wxBuild%" (rmdir /s /q "%wxBuild%")
mkdir "%wxBuild%"
%PSH% -Command "[System.Net.ServicePointManager]::MaxServicePointIdleTime = 5000000; if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; Invoke-WebRequest https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.2.1/wxWidgets-3.2.2.1.zip -OutFile '%wxBuild%\wxWidgets-3.2.2.1.zip'; exit $LASTEXITCODE"
%PSH% -Command "if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; Expand-Archive -Path '%wxBuild%\\wxWidgets-3.2.2.1.zip' -DestinationPath '%wxBuild%'"
if errorlevel 1 (echo not OK) else (echo OK)

@echo Downloading Windows Webview2 kit
%PSH% -Command "[System.Net.ServicePointManager]::MaxServicePointIdleTime = 5000000; if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; Invoke-WebRequest https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2 -OutFile '%wxBuild%\webview2.zip'; exit $LASTEXITCODE"
%PSH% -Command "if ($PSVersionTable.PSVersion.Major -lt 6) { $ProgressPreference = 'SilentlyContinue' }; Expand-Archive -Path '%wxBuild%\\webview2.zip' -DestinationPath '%wxBuild%\\build\3rdparty\webview2'"
if errorlevel 1 (echo not OK) else (echo OK)

@echo Building wxWidgets
pushd "%wxBuild%"
msbuild /noLogo /m "-p:Configuration=DLL Debug" -p:Platform=Win32 build\msw\wx_vc17.sln
msbuild /noLogo /m "-p:Configuration=DLL Release" -p:Platform=Win32 build\msw\wx_vc17.sln
if not exist "%CACHE_DIR%\buildwin\wxWidgets" (mkdir "%CACHE_DIR%\buildwin\wxWidgets")
xcopy /e /q /y .\lib\vc_dll\ "%CACHE_DIR%\buildwin\wxWidgets"
if not exist "%CACHE_DIR%\buildwin\wxWidgets\locale" (mkdir "%CACHE_DIR%\buildwin\wxWidgets\locale")
xcopy /e /q /y .\locale\ "%CACHE_DIR%\buildwin\wxWidgets\locale"
popd
endlocal

rem ############### We need to export this code to the vs startup batch files
set "wxDIR=%CD%\build\buildwxWidgets"
@echo set "wxWidgets_ROOT_DIR="%wxDIR%"
set "wxWidgets_ROOT_DIR=%wxDIR%"
@echo set "wxWidgets_LIB_DIR=%wxDIR%\lib\vc_dll"
set "wxWidgets_LIB_DIR=%wxDIR%\lib\vc_dll"
rem set path=%PATH%;%wxWidgets_LIB_DIR%;%OD%\cache\buildwin;%OD%\buildwin\crashrpt"
rem ############### We need to export this code to the vs startup batch files
pushd build

call ..\docopyAll.bat Debug
call ..\docopyAll.bat RelWithDebInfo
call ..\docopyAll.bat MinSizeRel
call ..\docopyAll.bat Release

rem restore configurations if we saved them
setlocal
set folder=Release
call :restore
set folder=RelWithDebInfo
call :restore
set folder=Debug
call :restore
set folder=MinSizeRel
call :restore
endlocal

cmake -A Win32 -G "Visual Studio 17 2022" -DCMAKE_GENERATOR_PLATFORM=Win32 -DwxWidgets_LIB_DIR="%wxWidgets_LIB_DIR%" -DwxWidgets_ROOT_DIR="%wxWidgets_ROOT_DIR%" -D CMAKE_CXX_FLAGS="/MP /EHsc /DWIN32" -D CMAKE_C_FLAGS="/MP" -D OCPN_ENABLE_SYSTEM_CMD_SOUND=ON -DOCPN_CI_BUILD=OFF -DOCPN_BUNDLE_WXDLLS=ON -DOCPN_BUILD_TEST=OFF ..
if errorlevel 1 goto :cmakeErr
popd
@echo To build OpenCPN for debugging at command line do this:
@echo > cd build
@echo > msbuild /noLogo /m -p:Configuration=Debug -p:Platform=Win32 opencpn.sln
@echo > devenv opencpn.sln
@echo and start debugging
goto :EOF

:cmakeErr
popd
@echo CMake failed to configure OpenCPN build folder.
@echo Review the error messages and read the OpenCPN Developer Manual for help.
@echo Be sure to install all dependencies such as GetText and NullSoft installer.
goto :EOF

rem Local subroutines

:backup
@echo "Backing up %OD%\build\%folder%"
if not exist "%OD%\build\%folder%" goto :breturn
if not exist .\tmp (mkdir .\tmp)
if not exist ".\tmp\%folder%" (mkdir ".\tmp\%folder%")
@echo backing up %folder%
xcopy /Q /Y "%OD%\build\%folder%\opencpn.ini" ".\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.log.log" ".\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.log" ".\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.dat" ".\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\*.csv" ".\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\navobj.*" ".\tmp\%folder%"
xcopy /Q /Y "%OD%\build\%folder%\navobj.xml.?" ".\tmp\%folder%"
if not exist "%OD%\build\%folder%\Charts" goto :breturn
@echo cmake -E copy_directory "%OD%\build\%folder%\Charts" "tmp\%folder%"
cmake -E copy_directory "%OD%\build\%folder%\Charts" "tmp\%folder%"
:breturn
@echo backup returning
exit /b 0

:restore
if not exist "..\tmp\%folder%" goto :rreturn
@echo Restoring %folder% settings
cmake -E copy_directory "..\tmp\%folder%" ".\%folder%"
if errorlevel 0 (rmdir /s /q "..\tmp\%folder%")

:rreturn
@echo restore returning
exit /b 0
