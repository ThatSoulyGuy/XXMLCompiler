@echo off
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
link.exe working.obj build\lib\Release\XXMLLLVMRuntime.lib /OUT:working.exe /SUBSYSTEM:CONSOLE > link_output.txt 2>&1
type link_output.txt
if exist working.exe (
    echo.
    echo SUCCESS: working.exe created!
) else (
    echo.
    echo ERROR: Failed to create working.exe
)
