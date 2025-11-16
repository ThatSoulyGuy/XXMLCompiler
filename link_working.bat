@echo off
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
link.exe working.obj build\lib\Release\XXMLLLVMRuntime.lib /OUT:working.exe /SUBSYSTEM:CONSOLE
if exist working.exe (
    echo SUCCESS: working.exe created!
) else (
    echo ERROR: Failed to create working.exe
)
