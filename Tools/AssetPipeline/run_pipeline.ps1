<#
Rebuilds every generated Metalization asset and the demo levels from the graybox models (Art/Graybox).

    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1

Close the Unreal Editor first (the commandlet needs exclusive access to the assets) and build the
MetalizationEditor target so the C++ classes the data assets use exist.

Stages:
    1. blender_export.py    Art/Graybox/**/*.blend (one file per model) -> Saved/AssetPipeline (FBX, manifest.json)
    2. ue_import_assets.py  materials, skeletal/static meshes (characters, weapons, props, ships) under /Game/Metalization
    1b. generate_sfx.py     synthesized sound effects -> Saved/AssetPipeline/Audio (WAV)
    1c. import_plan.py      decides what changed since the last import (import_state.json)
    3. ue_setup_gameplay.py weapon + robot data assets, DA_Sounds, the maps (MZ_ProductionAlpha, MZ_Title, MZ_Outpost_Demo, ...)
    4. ue_validate.py       sanity checks (sizes, material slots, skeletal-mesh usage flags)
#>
param(
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string]$Editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [string]$Source = "",
    [switch]$SkipBlender,
    [switch]$SkipImport,
    [switch]$SkipGameplay,
    # Re-import every asset and rebuild every map (the old behaviour). Normally only what changed is redone.
    [switch]$Full,
    # Record the current models and maps as up to date without importing or rebuilding anything (use once
    # when Content is known to match the .blend files, e.g. after a -Full run or a fresh pull).
    [switch]$Adopt
)
$ErrorActionPreference = "Stop"
$Tools = $PSScriptRoot
$Project = (Resolve-Path (Join-Path $Tools "..\..")).Path
$UProject = Join-Path $Project "Metalization.uproject"
if (-not $Source) { $Source = Join-Path $Project "Art\Graybox" }
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
    & $Blender -b --factory-startup --python (Join-Path $Tools "blender_export.py") -- $Out $Source | Select-String "\[MZExport\] (assembled|DONE)"
    if ($LASTEXITCODE -ne 0) { throw "Blender export failed" }
}
# Standard-library helper scripts run on the engine's own Python.
$Python = Join-Path (Split-Path (Split-Path (Split-Path $Editor))) "Binaries\ThirdParty\Python3\Win64\python.exe"
if (-not (Test-Path $Python)) { $Python = "python" }
if (-not $SkipImport) {
    # Sound effects are synthesized (no source files).
    Write-Host "== generate_sfx.py" -ForegroundColor Cyan
    & $Python (Join-Path $Tools "generate_sfx.py") (Join-Path $Out "Audio") | Select-String "\[MZSfx\] DONE"
    if ($LASTEXITCODE -ne 0) { throw "Sound generation failed" }
}
if (-not $SkipImport) {
    # Only what changed is re-imported: import_plan.py compares the export with import_state.json, deletes the
    # stale .uasset files (the commandlet can't delete files, and re-importing over a mesh keeps its old
    # material slots) and tells the importer what to import. -Full wipes and re-imports everything.
    Write-Host "== import_plan.py" -ForegroundColor Cyan
    if ($Adopt) {
        & $Python (Join-Path $Tools "import_plan.py") adopt | Select-String "\[MZPlan\]"
        if ($LASTEXITCODE -ne 0) { throw "import_plan.py adopt failed" }
    }
    $planArgs = @((Join-Path $Tools "import_plan.py"), "plan")
    if ($Full) { $planArgs += "--full" }
    & $Python @planArgs | Select-String "\[MZPlan\]"
    if ($LASTEXITCODE -ne 0) { throw "import_plan.py failed" }
    Invoke-UnrealScript "ue_import_assets.py" "MZImport"
    & $Python (Join-Path $Tools "import_plan.py") commit | Select-String "\[MZPlan\]"
}
if (-not $SkipGameplay) {
    if ($Full) { $env:MZ_FULL = "1" }
    if ($Adopt) { $env:MZ_ADOPT = "1" }
    try { Invoke-UnrealScript "ue_setup_gameplay.py" "MZSetup" } finally { Remove-Item Env:MZ_FULL, Env:MZ_ADOPT -ErrorAction SilentlyContinue }
}
Invoke-UnrealScript "ue_validate.py" "MZValidate"
Write-Host "Pipeline finished. Open /Game/Metalization/Maps/MZ_Outpost_Demo (mission) or MZ_Arena (free roam) and press Play." -ForegroundColor Green
