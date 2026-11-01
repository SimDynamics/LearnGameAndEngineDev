param([string]$Resources, [string]$EnglishResources)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$originalToolEnvironment = @{}
foreach ($name in @('JAVA_HOME','LGAED_JAVA_HOME','LGAED_PYTHON_EXECUTABLE','LGAED_FFMPEG_EXECUTABLE','VULKAN_SDK','PATH')) {
    $originalToolEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $tools = & (Join-Path $PSScriptRoot 'Resolve-NativeTools.ps1')
    $workerArguments = @((Join-Path $repository 'native\narration\worker\build_worker.py'), '--java-home', $tools.JavaHome)
    if ($Resources) { $workerArguments += @('--resources', $Resources) }
    if ($EnglishResources) { $workerArguments += @('--english-resources', $EnglishResources) }
    & $tools.Python @workerArguments
    if ($LASTEXITCODE -ne 0) { throw "Shared Kokoro frontend build failed ($LASTEXITCODE)." }
} finally {
    foreach ($name in $originalToolEnvironment.Keys) {
        $originalValue = $originalToolEnvironment[$name]
        if ($null -eq $originalValue) { $originalValue = [System.Management.Automation.Language.NullString]::Value }
        [Environment]::SetEnvironmentVariable($name, $originalValue, 'Process')
    }
}
