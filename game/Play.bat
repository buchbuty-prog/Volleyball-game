@echo off
rem Starts the prototype in its own window, without opening the editor.
rem Build it first (docs/SETUP-UNREAL.md). Add -VC26Autoplay to watch CPU vs CPU.
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0VolleyballCareer26.uproject" -game -windowed -ResX=1600 -ResY=900 %*
