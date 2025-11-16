@echo off
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
link %1 build\lib\Release\XXMLLLVMRuntime.lib /OUT:%2 /SUBSYSTEM:CONSOLE /DEFAULTLIB:libcmt.lib /DEFAULTLIB:ucrt.lib
