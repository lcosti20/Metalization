<#
Rebuilds every generated Metalization asset and the demo levels from the graybox models (Art/Graybox).

    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1

Close the Unreal Editor first (the commandlet needs exclusive access to the assets) and build the
MetalizationEditor target so the C++ classes the data assets use exist.

Stages:
    1. blender_export.py    Art/Graybox/Metalization_Graybox.blend -> Saved/AssetPipeline (FBX, manifest.json)
    2. ue_import_assets.py  materials, skeletal/static meshes (characters, weapons, props, ships) under /Game/Metalization
    3. ue_setup_gameplay.py weapon + robot data assets, the MZ_Outpost_Demo map and MZ_Arena (ue_setup_arena.py)
    4. ue_validate.py       sanity checks (sizes, material slots, skeletal-mesh usage flags)
#>
param(
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string]$Editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [string]$Blend = "",
    [switch]$SkipBlender,
    [switch]$SkipImport,
    [switch]$SkipGameplay
)
$ErrorActionPreference = "Stop"
$Tools = $PSScriptRoot
$Project = (Resolve-Path (Join-Path $Tools "..\..")).Path
$UProject = Join-Path $Project "Metalization.uproject"
if (-not $Blend) { $Blend = Join-Path $Project "Art\Graybox\Metalization_Graybox.blend" }
$Out = Join-Path $Project "Saved\AssetPipeline"
$EditorLog = Join-Path $Project "Saved\Logs\Metalization.log"

function Invoke-UnrealScript([string]$Script, [string]$Tag) {
    Write-Host "== $Script" -ForegroundColor Cyan
    $scriptPath = (Join-Path $Tools $Script).Replace("\", "/")   # backslashes get eaten by the command-line parser
    & $Editor $UProject.Replace("\", "/") -run=pythonscript "-script=$scriptPath" -unattended -nop4 -nosplash -nullrhi | Out-Null
    $code = $LASTEXITCODE
    Select-String -Path $EditorLog -Pattern "\[$Tag\]" | ForEach-Object { "  " + ($_.Line -replace '^.*\[' + $Tag + '\]\s*', '') }
    if ($code -ne 0) { throw "$Script failed (exit $code). See $EditorLog" }
}

if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close the Unreal Editor before running the pipeline."
}

if (-not $SkipBlender) {
    Write-Host "== blender_export.py" -ForegroundColor Cyan
    if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
    & $Blender -b $Blend --python (Join-Path $Tools "blender_export.py") -- $Out | Select-String "\[MZExport\] DONE"
    if ($LASTEXITCODE -ne 0) { throw "Blender export failed" }
}
if (-not $SkipImport) {
    # Everything here is regenerated from the manifest. Reimporting over an existing mesh keeps its old
    # material slots, and deleting assets from the commandlet doesn't remove the files, so clear them on disk.
    $Generated = Join-Path $Project "Content\Metalization"
    foreach ($folder in "Characters", "Weapons", "Props", "Ships", "Textures", "Materials\Instances") {
        $path = Join-Path $Generated $folder
        if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    }
    Invoke-UnrealScript "ue_import_assets.py" "MZImport"
}
if (-not $SkipGameplay) { Invoke-UnrealScript "ue_setup_gameplay.py" "MZSetup" }
Invoke-UnrealScript "ue_validate.py" "MZValidate"
Write-Host "Pipeline finished. Open /Game/Metalization/Maps/MZ_Outpost_Demo (mission) or MZ_Arena (free roam) and press Play." -ForegroundColor Green
