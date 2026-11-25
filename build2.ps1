# RE2DLSS 2.0 (no ReShade): builds bin\RE2DLSS.dll, a REFramework plugin holding the whole add-on and
# the rdx D3D12 hook layer (src\rdx) that replaces ReShade. MSVC (Visual Studio 2022+ with C++ tools).
# Usage: .\build2.ps1 [-Install] [-GameDir <folder of re2.exe>]
#   -Install  copies RE2DLSS.dll to reframework\plugins, the NVIDIA / AMD runtimes and the Lua script
#             (game closed). A 1.x install there (RE2DLSS.addon64 for ReShade, RE2DLSS_Reflex.dll) is
#             switched off by renaming it to *.1x-off, so the two never run together.
param(
    [switch]$Install,
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
             "rdx\rdx_events.cpp", "rdx\rdx_track.cpp", "rdx\rdx_api.cpp", "rdx\rdx_hooks.cpp") |
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

if ($Install) {
    if (-not (Test-Path "$GameDir\$exe")) { throw "$exe not found in $GameDir (pass -GameDir)" }
    if (Get-Process ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue) { throw "Close the game first." }
    if (-not (Test-Path "$GameDir\reframework")) { throw "REFramework isn't installed in $GameDir (2.0 is a REFramework plugin)" }
    $ngx = "$root\third_party\bin\nvngx_dlss.dll"
    $ngxd = "$root\third_party\bin\nvngx_dlssd.dll"
    $sl = "$root\third_party\streamline\bin\x64"
    $slFiles = "sl.interposer.dll", "sl.common.dll", "sl.dlss_g.dll", "sl.reflex.dll", "sl.pcl.dll", "nvngx_dlssg.dll"
    $ffx = "$root\third_party\fidelityfx\bin"
    $ffxFiles = "amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_upscaler_dx12.dll", "amd_fidelityfx_framegeneration_dx12.dll"
    # 1.x off (kept, renamed).
    foreach ($old in "$GameDir\RE2DLSS.addon64", "$GameDir\reframework\plugins\RE2DLSS_Reflex.dll") {
        if (Test-Path $old) { Move-Item $old "$old.1x-off" -Force; Write-Host "switched off $old" }
    }
    New-Item -ItemType Directory -Force "$GameDir\reframework\plugins", "$GameDir\reframework\autorun", "$GameDir\streamline" | Out-Null
    Copy-Item $dll "$GameDir\reframework\plugins" -Force
    Copy-Item "$obj\RE2DLSS.pdb" "$GameDir\reframework\plugins" -Force
    Copy-Item $ngx, $ngxd $GameDir -Force
    foreach ($f in $ffxFiles) { Copy-Item "$ffx\$f" $GameDir -Force }
    foreach ($f in $slFiles) { Copy-Item "$sl\$f" "$GameDir\streamline" -Force }
    Copy-Item $ngx, $ngxd "$GameDir\streamline" -Force
    Copy-Item "$root\reframework\RE2DLSS.lua" "$GameDir\reframework\autorun" -Force
    Write-Host "Installed to $GameDir"
}
