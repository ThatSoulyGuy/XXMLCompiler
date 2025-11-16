@echo off
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
link.exe working_fixed.obj build\lib\Release\XXMLLLVMRuntime.lib /OUT:working_fixed.exe /SUBSYSTEM:CONSOLE
if exist working_fixed.exe (
    echo SUCCESS: working_fixed.exe created!
    echo Running program:
    working_fixed.exe
) else (
    echo ERROR: Failed to create working_fixed.exe
)
