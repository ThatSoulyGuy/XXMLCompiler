@echo off
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
link.exe namespace_method_test.obj build\lib\Release\XXMLLLVMRuntime.lib /OUT:namespace_method_test.exe /SUBSYSTEM:CONSOLE > link_output_namespace.txt 2>&1
type link_output_namespace.txt
if exist namespace_method_test.exe (
    echo.
    echo SUCCESS: namespace_method_test.exe created!
) else (
    echo.
    echo ERROR: Failed to create namespace_method_test.exe
)
