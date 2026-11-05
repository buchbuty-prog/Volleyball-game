@echo off
rem Builds the game and installs it on the phone connected by USB.
rem One-time setup is in docs/SETUP-UNREAL.md. The first run takes a long time.
call "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="%~dp0VolleyballCareer26.uproject" -noP4 -utf8output ^
  -platform=Android -cookflavor=ASTC -clientconfig=Development ^
  -build -cook -stage -pak -package -deploy ^
  -archive -archivedirectory="%~dp0Build"
echo.
if errorlevel 1 (echo BUILD FAILED. Copy the first error above to Claude.) else (echo Done. Open Volleyball Career 26 on your phone.)
pause
