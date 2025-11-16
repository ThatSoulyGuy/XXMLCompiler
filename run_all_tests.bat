@echo off
cd build\bin\Release
call "D:\VisualStudio\Installs\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

echo ========================================
echo XXML Test Suite
echo ========================================
echo.

REM Test 2
echo [Test 2] Integer arithmetic
link test2.obj ..\..\lib\Release\XXMLLLVMRuntime.lib /OUT:test2.exe /SUBSYSTEM:CONSOLE >nul 2>&1
test2.exe
echo.

REM Test 3
xxml.exe Test3_String.XXML test3.ll 2 >nul 2>&1
clang -c -O0 -o test3.obj test3.ll >nul 2>&1
echo [Test 3] String operations
link test3.obj ..\..\lib\Release\XXMLLLVMRuntime.lib /OUT:test3.exe /SUBSYSTEM:CONSOLE >nul 2>&1
test3.exe
echo.

REM Test 4
xxml.exe Test4_Class.XXML test4.ll 2 >nul 2>&1
clang -c -O0 -o test4.obj test4.ll >nul 2>&1
echo [Test 4] User-defined class
link test4.obj ..\..\lib\Release\XXMLLLVMRuntime.lib /OUT:test4.exe /SUBSYSTEM:CONSOLE >nul 2>&1
test4.exe
echo.

REM Hello.XXML
xxml.exe Hello.XXML hello_final.ll 2 >nul 2>&1
clang -c -O0 -o hello_final.obj hello_final.ll >nul 2>&1
echo [Hello.XXML] Full feature test
link hello_final.obj ..\..\lib\Release\XXMLLLVMRuntime.lib /OUT:hello_final.exe /SUBSYSTEM:CONSOLE >nul 2>&1
hello_final.exe

echo.
echo ========================================
echo All tests complete!
echo ========================================
