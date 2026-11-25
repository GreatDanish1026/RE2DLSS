# RE2DLSS 2.0 (no ReShade): builds bin\RE2DLSS.dll, a REFramework plugin holding the whole add-on and
# the rdx D3D12 hook layer (src\rdx) that replaces ReShade. MSVC (Visual Studio 2022+ with C++ tools).
# Usage: .\build.ps1 [-Install] [-Package] [-GameDir <folder of re2.exe>]
#   -Install  copies RE2DLSS.dll to reframework\plugins, the NVIDIA / AMD runtimes and the Lua script
#             (game closed). A 1.x install there (RE2DLSS.addon64 for ReShade, RE2DLSS_Reflex.dll) is
#             switched off by renaming it to *.1x-off, so the two never run together.
#   -Package  makes bin\release\RE2DLSS-<version>.zip for Fluffy Mod Manager: one folder, RE2DLSS,
#             with modinfo.ini and the files laid out as they go in the game folder.
# NVIDIA's signed DLSS / Ray Reconstruction 310.9.16 DLLs go in third_party\bin, the Streamline 2.14.16
# release's bin\x64 in third_party\streamline\bin\x64, AMD's FSR SDK 2.4.0 signedbin DLLs in
# third_party\fidelityfx\bin (none are in git).
param(
    [switch]$Install,
    [switch]$Package,
    [string]$GameDir = "E:\SteamLibrary\steamapps\common\RESIDENT EVIL 2  BIOHAZARD RE2"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$exe = "re2.exe"
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'DSX' }) -join ';'  # Steam's DSX entry breaks cmd /c

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ x64 tools not found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

$obj = "$root\bin\obj2"
New-Item -ItemType Directory -Force $obj | Out-Null
$dll = "$root\bin\RE2DLSS.dll"

$shaders = @(
    "fxc /nologo /T vs_5_0 /E VsMain /O3 /Vn kBlitVs /Fh `"$obj\blit_vs.h`" `"$root\src\blit.hlsl`" >nul",
    "fxc /nologo /T ps_5_0 /E PsMain /O3 /Vn kBlitPs /Fh `"$obj\blit_ps.h`" `"$root\src\blit.hlsl`" >nul",
    "fxc /nologo /T cs_5_1 /E main /O3 /Vn kUiAlphaCs /Fh `"$obj\ui_alpha_cs.h`" `"$root\src\ui_alpha.hlsl`" >nul",
    "fxc /nologo /T cs_5_1 /E main /O3 /Vn kHudComposeCs /Fh `"$obj\hud_compose_cs.h`" `"$root\src\hud_compose.hlsl`" >nul",
    "fxc /nologo /T cs_5_1 /E main /O3 /Vn kGuideCs /Fh `"$obj\guide_cs.h`" `"$root\src\guide.hlsl`" >nul",
    "fxc /nologo /T cs_5_1 /E main /O3 /Vn kGuideViewCs /Fh `"$obj\guide_view_cs.h`" `"$root\src\guide_view.hlsl`" >nul",
    "fxc /nologo /T cs_5_1 /E main /O3 /Vn kRcasCs /Fh `"$obj\rcas_cs.h`" `"$root\src\rcas.hlsl`" >nul",
    "fxc /nologo /T cs_5_1 /E main /O3 /Vn kMvFixCs /Fh `"$obj\mvfix_cs.h`" `"$root\src\mvfix.hlsl`" >nul"
)
# src\rdx comes first: its reshade.hpp and descriptor_tracking.hpp stand in for ReShade's.
$inc = "/I `"$root\src\rdx`" /I `"$obj`" /I `"$root\src`" /I `"$root\third_party\imgui`" /I `"$root\third_party\reshade\include`" " +
       "/I `"$root\third_party\dlss\include`" /I `"$root\third_party\streamline\include`" " +
       "/I `"$root\third_party\fidelityfx\api\include`" /I `"$root\third_party\fidelityfx\upscalers\include`" " +
       "/I `"$root\third_party\fidelityfx\framegeneration\include`""
$sources = @("main.cpp", "upscaler.cpp", "dlss.cpp", "fsr.cpp", "framegen.cpp", "fsrfg.cpp", "refplugin.cpp",
             "rdx\rdx_events.cpp", "rdx\rdx_track.cpp", "rdx\rdx_api.cpp", "rdx\rdx_hooks.cpp", "rdx\rdx_overlay.cpp") |
           ForEach-Object { "`"$root\src\$_`"" }
$cmd = (@("call `"$vcvars`" >nul") + $shaders + @(
    ("cl /nologo /c /O2 /MT /W3 /Fo`"$obj\\`" `"$root\src\rdx\rdx_slots.c`""),
    ("cl /nologo /std:c++17 /O2 /EHsc /MT /W3 /MP /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DRD_NO_RESHADE $inc /Fo`"$obj\\`" " +
     ($sources -join ' ') + " `"$obj\rdx_slots.obj`" " +
     "user32.lib advapi32.lib ole32.lib d3d12.lib dxgi.lib `"$root\third_party\dlss\lib\nvsdk_ngx_s.lib`" " +
     "/link /DLL /OUT:`"$dll`" /PDB:`"$obj\RE2DLSS.pdb`" /DEBUG /OPT:REF")
)) -join " && "
cmd /c $cmd
if ($LASTEXITCODE) { throw "build failed" }
Write-Host "Built $dll"

$ngx = "$root\third_party\bin\nvngx_dlss.dll"
$ngxd = "$root\third_party\bin\nvngx_dlssd.dll"
$sl = "$root\third_party\streamline\bin\x64"
$slFiles = "sl.interposer.dll", "sl.common.dll", "sl.dlss_g.dll", "sl.reflex.dll", "sl.pcl.dll", "nvngx_dlssg.dll"
$ffx = "$root\third_party\fidelityfx\bin"
$ffxFiles = "amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_upscaler_dx12.dll", "amd_fidelityfx_framegeneration_dx12.dll"
if ($Install -or $Package) {
    foreach ($f in @($ngx, $ngxd) + ($slFiles | ForEach-Object { "$sl\$_" }) + ($ffxFiles | ForEach-Object { "$ffx\$_" })) {
        if (-not (Test-Path $f)) { throw "$f missing (see the top of build.ps1)" }
    }
}

# The plugin, the NVIDIA and AMD runtimes and the Lua script, as they go in the game folder.
function Copy-Runtime([string]$dest) {
    New-Item -ItemType Directory -Force "$dest\reframework\plugins", "$dest\reframework\autorun", "$dest\streamline" | Out-Null
    Copy-Item $dll "$dest\reframework\plugins" -Force
    Copy-Item $ngx, $ngxd $dest -Force
    foreach ($f in $ffxFiles) { Copy-Item "$ffx\$f" $dest -Force }
    foreach ($f in $slFiles) { Copy-Item "$sl\$f" "$dest\streamline" -Force }
    # Streamline starts NGX with the streamline folder as where DLSS lives: DLSS looks there too.
    Copy-Item $ngx, $ngxd "$dest\streamline" -Force
    Copy-Item "$root\reframework\RE2DLSS.lua" "$dest\reframework\autorun" -Force
}

if ($Install) {
    if (-not (Test-Path "$GameDir\$exe")) { throw "$exe not found in $GameDir (pass -GameDir)" }
    if (Get-Process ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue) { throw "Close the game first." }
    if (-not (Test-Path "$GameDir\reframework")) { throw "REFramework isn't installed in $GameDir (2.0 is a REFramework plugin)" }
    # 1.x off (kept, renamed).
    foreach ($old in "$GameDir\RE2DLSS.addon64", "$GameDir\reframework\plugins\RE2DLSS_Reflex.dll") {
        if (Test-Path $old) { Move-Item $old "$old.1x-off" -Force; Write-Host "switched off $old" }
    }
    Copy-Runtime $GameDir
    Copy-Item "$obj\RE2DLSS.pdb" "$GameDir\reframework\plugins" -Force
    Write-Host "Installed to $GameDir"
}

if ($Package) {
    $version = [regex]::Match((Get-Content "$root\src\version.h" -Raw), 'RD_VERSION "([^"]+)"').Groups[1].Value
    $stage = "$root\bin\release\RE2DLSS-$version"
    $mod = "$stage\RE2DLSS"
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Force "$mod\RE2DLSS-licenses" | Out-Null
    Copy-Runtime $mod
    Copy-Item "$root\README.md" "$mod\RE2DLSS-README.md"
    Copy-Item "$root\THIRD_PARTY_NOTICES.txt" "$mod\RE2DLSS-THIRD_PARTY_NOTICES.txt"
    Copy-Item "$root\LICENSE" "$mod\RE2DLSS-licenses\RE2DLSS-LICENSE.txt"
    Copy-Item "$root\third_party\dlss\LICENSE.txt" "$mod\RE2DLSS-licenses\NVIDIA-DLSS-LICENSE.txt"
    Copy-Item "$root\third_party\streamline\license.txt" "$mod\RE2DLSS-licenses\Streamline-LICENSE.txt"
    Copy-Item "$root\third_party\streamline\licenses\reflex.license.txt" "$mod\RE2DLSS-licenses\NVIDIA-Reflex-LICENSE.txt"
    Copy-Item "$root\third_party\reshade\LICENSE.md" "$mod\RE2DLSS-licenses\ReShade-LICENSE.md"
    Copy-Item "$root\third_party\imgui\LICENSE.txt" "$mod\RE2DLSS-licenses\DearImGui-LICENSE.txt"
    Copy-Item "$root\third_party\fidelityfx\license.md" "$mod\RE2DLSS-licenses\AMD-FSR-LICENSE.txt"

    # Fluffy Mod Manager's mod information (key=value, one per line).
    @(
        "name=RE2DLSS - DLSS, FSR 3/4 and frame generation for Resident Evil 2",
        "version=v$version",
        "description=Replaces the game's TAA with NVIDIA DLSS or AMD FSR 3.1 / FSR 4: native anti-aliasing (DLAA), or upscaling with the game's bloom, tonemapping (RenoDX HDR works) and HUD on top at full resolution; adds DLSS or FSR frame generation, and DLSS Ray Reconstruction for the ray-traced reflections and lighting. DLSS needs an NVIDIA RTX card (frame generation RTX 40/50); FSR runs on any DirectX 12 GPU. 2.0: a REFramework plugin, ReShade no longer needed (uninstall RE2DLSS 1.x first). Settings: REFramework's menu (Insert), RE2DLSS; F9 switches upscaling on and off.",
        "author=GreatDanish"
    ) | Set-Content "$mod\modinfo.ini" -Encoding ascii

    # Zip with forward-slash entry names (Compress-Archive in PowerShell 5.1 writes backslashes).
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zip = "$root\bin\release\RE2DLSS-$version.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
    try {
        Get-ChildItem $stage -Recurse -File | ForEach-Object {
            $entry = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry,
                [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
    Write-Host "Packaged $zip"
    Get-ChildItem $stage -Recurse -File | ForEach-Object {
        "{0,-60} {1,12:N0}" -f $_.FullName.Substring($stage.Length + 1), $_.Length
    }
}
