@echo off
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
link.exe simple_hello.obj build\lib\Release\XXMLLLVMRuntime.lib /OUT:simple_hello.exe /SUBSYSTEM:CONSOLE
if exist simple_hello.exe (
    echo SUCCESS: simple_hello.exe created!
    echo Running:
    simple_hello.exe
) else (
    echo ERROR: Failed to create simple_hello.exe
)
