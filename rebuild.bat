@echo off
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" XXMLCompiler.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m
