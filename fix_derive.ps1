$filePath = "C:\Users\bkmcm\Documents\XXMLStudio\Projects\Hello\src\Main.XXML"
$content = Get-Content $filePath -Raw

# Replace the first instance: append(").append(propname -> append(").append(this.propname
$content = $content -replace '\.append\(String::Constructor\("\\"="\)\.append\(\)', '.append(String::Constructor("\"=").append(this.)'

# Replace the second instance: append(")).append(propname -> append(")).append(this.propname
$content = $content -replace '\.append\(String::Constructor\("\\"="\)\)\.append\(\)', '.append(String::Constructor("\"=")).append(this.)'

Set-Content $filePath $content -NoNewline
