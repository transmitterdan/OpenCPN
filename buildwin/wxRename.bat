@echo off
setlocal enabledelayedexpansion
pushd %1
for %%a in (*.dll) do (
   set "name=%%a"
   for %%b in ("!name:322=32!") do (
      set "newName=%%b"
      ren "!name!" "!newName!"
   )
)
goto :EOF
