#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Diagnostico','Preparar','Pruebas','Inicializar','Iniciar','Verificar','Respaldo')]
    [string]$Accion = 'Diagnostico'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
$state = Join-Path $env:LOCALAPPDATA 'ServerTec\state'
$venvPython = Join-Path $root '.venv\Scripts\python.exe'

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Fallo el comando; codigo ' + $LASTEXITCODE) }
}

if ($Accion -eq 'Diagnostico') {
    & (Join-Path $root 'Diagnostico-Red.ps1')
    return
}
Push-Location -LiteralPath $root
try {
    if ($Accion -eq 'Preparar') {
        if (-not (Test-Path -LiteralPath $venvPython)) {
            $py = Get-Command py -ErrorAction SilentlyContinue
            $python = Get-Command python -ErrorAction SilentlyContinue
            $check = 'import sys; assert sys.version_info >= (3,13), "Se requiere Python 3.13 o posterior"'
            if ($py) {
                Invoke-Checked $py.Source @('-3', '-c', $check)
                Invoke-Checked $py.Source @('-3', '-m', 'venv', '.venv')
            } elseif ($python) {
                Invoke-Checked $python.Source @('-c', $check)
                Invoke-Checked $python.Source @('-m', 'venv', '.venv')
            } else { throw 'Falta Python 3.13 o posterior. Este script no instala programas automaticamente.' }
        }
        # These are local software packages, not subscriptions or hosted APIs.
        Invoke-Checked $venvPython @('-m', 'pip', 'install', '--disable-pip-version-check', '--only-binary=:all:', '-r', 'requirements-test.txt')
        Invoke-Checked $venvPython @('-m', 'pytest', '-q', '-p', 'no:cacheprovider')
        Write-Host 'Dependencias y pruebas locales terminadas. Falta Inicializar.'
        return
    }
    if (-not (Test-Path -LiteralPath $venvPython)) { throw 'Primero ejecuta -Accion Preparar.' }
    if ($Accion -eq 'Pruebas') {
        Invoke-Checked $venvPython @('-m', 'pytest', '-q', '-p', 'no:cacheprovider')
        return
    }
    if ($Accion -eq 'Inicializar') {
        if ((Test-Path -LiteralPath $state) -and @(Get-ChildItem -LiteralPath $state -Force).Count -gt 0) {
            throw 'El estado ya existe. No se borro ni se sobrescribio.'
        }
        New-Item -ItemType Directory -Path $state -Force | Out-Null
        # Limit only this newly created runtime directory, not the rest of the PC.
        $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        Invoke-Checked 'icacls.exe' @($state, '/inheritance:r', '/grant:r', ('*' + $sid + ':(OI)(CI)F'), '*S-1-5-18:(OI)(CI)F', '*S-1-5-32-544:(OI)(CI)F')
    }
    $commands = @{Inicializar='init'; Iniciar='serve'; Verificar='verify'; Respaldo='backup'}
    Invoke-Checked $venvPython @('manage.py', $commands[$Accion], '--state', $state)
} finally { Pop-Location }
