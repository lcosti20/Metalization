<#
Rebuilds every generated Metalization asset and level from the graybox models (Art/Graybox).

    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1

Stages:
    1. blender_export.py   Art/Graybox/**/*.blend (one file per model) -> Assets/Metalization/Models (FBX, manifest.json).
                           Unchanged models are skipped, so Unity only re-imports what changed.
    2. generate_sfx.py     synthesized sound effects -> Assets/Metalization/Audio (WAV)
    3. Unity (batch mode)  MetalAssetPipeline.BuildAllBatch: materials, weapon and robot data, the sound library, ship
                           prefabs, then every level (MetalLevels: Assets/Metalization/Scenes/MZ_*.unity) and the
                           build scene list.

Unity locks the project while the editor has it open. With the editor open, run stages 1-2 with -SkipUnity and
then use Metalization > Build All Assets in the editor instead.
#>
param(
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    [string]$Unity = "C:\Program Files\Unity\Hub\Editor\6000.6.1f1\Editor\Unity.exe",
    [string]$Source = "",
    [switch]$SkipBlender,
    [switch]$SkipSfx,
    [switch]$SkipUnity
)
$ErrorActionPreference = "Stop"
$Tools = $PSScriptRoot
$Project = (Resolve-Path (Join-Path $Tools "..\..")).Path
if (-not $Source) { $Source = Join-Path $Project "Art\Graybox" }
$Models = Join-Path $Project "Assets\Metalization\Models"
$Audio = Join-Path $Project "Assets\Metalization\Audio"
$UnityLog = Join-Path $Project "Logs\pipeline.log"

if (-not $SkipBlender) {
    Write-Host "== blender_export.py" -ForegroundColor Cyan
    & $Blender -b --factory-startup --python (Join-Path $Tools "blender_export.py") -- $Models $Source | Select-String "\[MZExport\] (assembled|skipped|DONE)"
    if ($LASTEXITCODE -ne 0) { throw "Blender export failed" }
}
if (-not $SkipSfx) {
    Write-Host "== generate_sfx.py" -ForegroundColor Cyan
    & python (Join-Path $Tools "generate_sfx.py") $Audio | Select-String "\[MZSfx\] DONE"
    if ($LASTEXITCODE -ne 0) { throw "Sound generation failed" }
}
if (-not $SkipUnity) {
    if (Get-Process -Name "Unity" -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like "Metalization*" }) {
        throw "The Unity editor has the project open: close it, or rerun with -SkipUnity and use Metalization > Build All Assets."
    }
    Write-Host "== Unity: MetalAssetPipeline.BuildAllBatch" -ForegroundColor Cyan
    & $Unity -batchmode -quit -projectPath $Project -executeMethod Metalization.Editor.MetalAssetPipeline.BuildAllBatch -logFile $UnityLog | Out-Null
    $code = $LASTEXITCODE
    Select-String -Path $UnityLog -Pattern "\[MZSetup\]" | ForEach-Object { "  " + ($_.Line -replace '^.*\[MZSetup\]\s*', '') }
    if ($code -ne 0) { throw "Unity pipeline failed (exit $code). See $UnityLog" }
}
Write-Host "Pipeline finished. Open Assets/Metalization/Scenes/MZ_Title (or any MZ_* level) and press Play." -ForegroundColor Green
