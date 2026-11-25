# Builds probe\bin\RE2Probe.dll (REFramework plugin, RE2DLSS 2.0 phase 0). -Install copies it to reframework\plugins.
param([switch]$Install, [string]$GameDir = "E:\SteamLibrary\steamapps\common\RESIDENT EVIL 2  BIOHAZARD RE2")
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'DSX' }) -join ';'  # Steam DSX entry breaks cmd /c
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$bin = "$root\bin"
New-Item -ItemType Directory -Force "$bin\obj" | Out-Null
$cmd = "call `"$vcvars`" >nul && cl /nologo /std:c++17 /O2 /EHsc /MT /W3 /D_CRT_SECURE_NO_WARNINGS /Fo`"$bin\obj\\`" " +
       "`"$root\probe.cpp`" `"$root\vtidx.c`" user32.lib /link /DLL /OUT:`"$bin\RE2Probe.dll`" /PDB:`"$bin\obj\RE2Probe.pdb`" /DEBUG"
cmd /c $cmd
if ($LASTEXITCODE) { throw "build failed" }
Write-Host "Built $bin\RE2Probe.dll"
if ($Install) {
    if (Get-Process re2 -ErrorAction SilentlyContinue) { throw "close the game first" }
    Copy-Item "$bin\RE2Probe.dll" "$GameDir\reframework\plugins" -Force
    Write-Host "Installed to $GameDir\reframework\plugins"
}
