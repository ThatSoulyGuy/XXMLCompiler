$file = 'C:\Users\bkmcm\Documents\VisualStudio\Projects\XXMLCompiler\src\Backends\LLVMBackend.cpp'
$lines = Get-Content $file
$before = $lines[0..2046]
$after = $lines[6240..($lines.Length-1)]
$result = $before + $after
$result | Set-Content $file -Encoding UTF8
Write-Host "Removed lines 2048-6240. New line count: $($result.Length)"
