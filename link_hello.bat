@echo off
cd build\bin\Release
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
link.exe goodbye.obj ..\..\lib\Release\XXMLLLVMRuntime.lib /OUT:goodbye.exe /SUBSYSTEM:CONSOLE > link_output.txt 2>&1
type link_output.txt
if exist goodbye.exe (
    echo.
    echo SUCCESS: goodbye.exe created!
    echo.
    echo Running the executable:
    goodbye.exe
) else (
    echo.
    echo ERROR: Failed to create goodbye.exe
)
