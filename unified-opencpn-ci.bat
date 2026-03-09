@echo off
setlocal enabledelayedexpansion

REM =====================================================================
REM  Unified OpenCPN CI Helper
REM
REM  PURPOSE:
REM    This script automates two tasks needed when working on a fork of
REM    the OpenCPN repository:
REM
REM      1. Trigger CI for a feature branch by force-pushing it to
REM         origin/master.
REM
REM      2. Restore the real master branch by syncing it with
REM         upstream/master and force-pushing it back to origin/master.
REM
REM    Additionally, this script updates the GitHub Actions secret
REM    CLOUDSMITH_REPO so that:
REM
REM      - CI runs for feature branches publish to:
REM            dan-dickey/opencpn-unstable-testing
REM
REM      - CI runs for the real master branch publish to:
REM            dan-dickey/opencpn-github
REM
REM    This ensures artifacts always go to the correct Cloudsmith repo.
REM
REM  REQUIREMENTS:
REM      - GitHub CLI installed (winget install GitHub.cli)
REM      - gh auth login completed once
REM      - 'origin' is your fork
REM      - 'upstream' is the official OpenCPN repo
REM =====================================================================


if "%1"=="" goto :usage
if "%1"=="ci" goto :run_ci
if "%1"=="restore" goto :restore_master
goto :usage



:run_ci
echo ------------------------------------------------------------
echo  OPERATION: Trigger CI by pushing current branch to origin/master
echo ------------------------------------------------------------

REM Detect current branch
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD') do set CURRENT_BRANCH=%%b
echo Current branch detected: %CURRENT_BRANCH%
echo.

REM Update GitHub secret for feature-branch CI builds
echo Setting CLOUDSMITH_REPO for CI mode...
gh secret set CLOUDSMITH_REPO --repo transmitterdan/OpenCPN --body "dan-dickey/opencpn-unstable-testing"
echo CLOUDSMITH_REPO updated for CI mode.
echo.

REM Push feature branch to origin/master
echo Pushing %CURRENT_BRANCH% to origin/master --force...
git push origin %CURRENT_BRANCH%:master --force

echo.
echo CI trigger push complete.
goto :eof



:restore_master
echo ------------------------------------------------------------
echo  OPERATION: Restore real master from upstream/master
echo ------------------------------------------------------------

REM Update GitHub secret for real master builds
echo Setting CLOUDSMITH_REPO for restore mode...
gh secret set CLOUDSMITH_REPO --repo transmitterdan/OpenCPN --body "dan-dickey/opencpn-github"
echo CLOUDSMITH_REPO updated for restore mode.
echo.

REM Switch to local master
echo Checking out local master...
git checkout master

REM Sync with upstream
echo Fetching upstream...
git fetch upstream

echo Resetting local master to upstream/master...
git reset --hard upstream/master

REM Push clean master back to origin
echo Force pushing clean master back to origin/master...
git push origin master --force

echo.
echo Master successfully restored.
goto :eof



:usage
echo.
echo Usage:
echo    unified-opencpn-ci.bat ci
echo        Pushes current branch to origin/master and sets CLOUDSMITH_REPO
echo        to dan-dickey/opencpn-unstable-testing
echo.
echo    unified-opencpn-ci.bat restore
echo        Restores master from upstream and sets CLOUDSMITH_REPO
echo        to dan-dickey/opencpn-github
echo.
goto :eof
