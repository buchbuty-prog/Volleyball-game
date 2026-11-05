@echo off
rem Rebuilds the rigged athletes from the Mixamo files in Content/Mixamo. No window opens.
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\VolleyballCareer26.uproject" -run=pythonscript -script="%~dp0ImportAthletes.py" "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities" -unattended -nop4 -nosplash
