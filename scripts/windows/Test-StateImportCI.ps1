[CmdletBinding()]
param()

# Compatibility entry point for the historical-import workflow and local docs.
& (Join-Path $PSScriptRoot 'Test-NativeCurricularCI.ps1') -Suite StateImport
