# Instalación manual de Driftbox en Windows (alternativa al instalador .exe).
# Uso: doble clic en install_windows.bat (pide permisos de administrador).

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

$vst3Dir = Join-Path $env:CommonProgramFiles "VST3"
$appDir  = Join-Path $env:ProgramFiles "Driftbox"
New-Item -ItemType Directory -Force -Path $vst3Dir, $appDir | Out-Null

Write-Host "Instalando Driftbox..."

if (Test-Path ".\Driftbox.vst3") {
    $dest = Join-Path $vst3Dir "Driftbox.vst3"
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Copy-Item -Recurse ".\Driftbox.vst3" $dest
    Write-Host "  OK  VST3 -> $dest"
}

if (Test-Path ".\Driftbox.exe") {
    Copy-Item -Force ".\Driftbox.exe" $appDir
    $shortcut = Join-Path ([Environment]::GetFolderPath("CommonPrograms")) "Driftbox.lnk"
    $ws = New-Object -ComObject WScript.Shell
    $lnk = $ws.CreateShortcut($shortcut)
    $lnk.TargetPath = Join-Path $appDir "Driftbox.exe"
    $lnk.Save()
    Write-Host "  OK  Standalone -> $appDir"
}

Write-Host ""
Write-Host "Listo. Abre tu DAW y re-escanea los plugins VST3 si no aparece Driftbox."
Read-Host "Presiona Enter para cerrar"
