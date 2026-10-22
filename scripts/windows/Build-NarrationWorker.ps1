param([string]$Resources, [string]$EnglishResources)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$workerArguments = @((Join-Path $repository 'native\narration\worker\build_worker.py'))
if ($Resources) { $workerArguments += @('--resources', $Resources) }
if ($EnglishResources) { $workerArguments += @('--english-resources', $EnglishResources) }
& python @workerArguments
if ($LASTEXITCODE -ne 0) { throw "Shared Kokoro frontend build failed ($LASTEXITCODE)." }
