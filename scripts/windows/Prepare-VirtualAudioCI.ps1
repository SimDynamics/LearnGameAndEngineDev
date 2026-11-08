[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $PythonPath,
    [Parameter(Mandatory = $true)]
    [string] $SignToolPath,
    [Parameter(Mandatory = $true)]
    [string] $LogDirectory
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if (-not $IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or
    $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Evaluate this device only on a disposable GitHub-hosted Windows runner.'
}
if ($env:GITHUB_RUN_ID -notmatch '^\d+$' -or $env:GITHUB_RUN_ATTEMPT -notmatch '^[1-9]\d*$') {
    throw 'The hosted workflow run identity is missing.'
}
$qaIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $qaPrincipal = [Security.Principal.WindowsPrincipal]::new($qaIdentity)
    if (-not $qaPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'The disposable runner must already be elevated for a device installation.'
    }
} finally {
    $qaIdentity.Dispose()
}
foreach ($qaTool in @($PythonPath, $SignToolPath)) {
    if (-not (Test-Path -LiteralPath $qaTool -PathType Leaf)) {
        throw "The verified Windows tool is absent: $qaTool"
    }
}
$qaRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaLogRoot = [IO.Path]::GetFullPath((Join-Path $qaRepo 'out\qa')) + [IO.Path]::DirectorySeparatorChar
$LogDirectory = [IO.Path]::GetFullPath($LogDirectory)
if (-not $LogDirectory.StartsWith($qaLogRoot, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $LogDirectory)) {
    throw 'Virtual audio diagnostics require a new directory under this checkout out\qa.'
}
New-Item -ItemType Directory -Path $LogDirectory | Out-Null

# VB-Audio permits evaluating VB-CABLE before payment. This opt-in experiment
# does not authorize repeated professional deployments or bundle the driver.
# https://vb-audio.com/Cable/
# https://vb-audio.com/Services/licensing.htm
# These hashes identify the bytes retrieved from the official Pack46 download;
# they are not a substitute for the Windows trust and kernel-policy checks.
$qaUrl = 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip'
$qaZipHash = 'bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90'
$qaZipBytes = 1324680
$qaSystemVolume = [IO.Path]::GetPathRoot($env:windir)
$qaStaging = Join-Path $qaSystemVolume "LGAED-VirtualAudioCI-$env:GITHUB_RUN_ID-$env:GITHUB_RUN_ATTEMPT"
if (Test-Path -LiteralPath $qaStaging) {
    throw "The system-drive evaluation destination must be new: $qaStaging"
}
New-Item -ItemType Directory -Path $qaStaging | Out-Null
$qaArchive = Join-Path $qaStaging 'VBCABLE_Driver_Pack46.zip'
$qaDriver = Join-Path $qaStaging 'driver'
try {
    Invoke-WebRequest -Uri $qaUrl -OutFile $qaArchive
    $qaActualHash = (Get-FileHash -LiteralPath $qaArchive -Algorithm SHA256).Hash
    if ((Get-Item -LiteralPath $qaArchive).Length -ne $qaZipBytes -or $qaActualHash -ne $qaZipHash) {
        throw 'The official VB-CABLE Pack46 download changed; reverify it before execution.'
    }
    [PSCustomObject]@{
        Source = $qaUrl
        Bytes = $qaZipBytes
        SHA256 = $qaActualHash.ToLowerInvariant()
        WindowsVersion = [Environment]::OSVersion.Version.ToString()
        SignTool = $SignToolPath
        SignToolVersion = (Get-Item -LiteralPath $SignToolPath).VersionInfo.FileVersion
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'package.json') -Encoding utf8

    # Inspect every member before expansion, even though the complete ZIP is pinned.
    $qaZip = [IO.Compression.ZipFile]::OpenRead($qaArchive)
    try {
        $qaExtractionRoot = [IO.Path]::GetFullPath($qaDriver) + [IO.Path]::DirectorySeparatorChar
        foreach ($qaEntry in $qaZip.Entries) {
            $qaMember = $qaEntry.FullName.Replace('/', '\')
            if ([IO.Path]::IsPathRooted($qaMember) -or $qaMember.Contains(':') -or
                (($qaEntry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) {
                throw "Unsafe archive member: $($qaEntry.FullName)"
            }
            $qaDestination = [IO.Path]::GetFullPath((Join-Path $qaDriver $qaMember))
            if (-not $qaDestination.StartsWith($qaExtractionRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive member escapes its new destination: $($qaEntry.FullName)"
            }
        }
    } finally {
        $qaZip.Dispose()
    }
    Expand-Archive -LiteralPath $qaArchive -DestinationPath $qaDriver
    $qaInf = Join-Path $qaDriver 'vbMmeCable64_win10.inf'
    $qaSys = Join-Path $qaDriver 'vbaudio_cable64_win10.sys'
    $qaCat = Join-Path $qaDriver 'vbaudio_cable64_win10.cat'
    $qaSignature = Get-AuthenticodeSignature -LiteralPath $qaCat
    [PSCustomObject]@{
        Status = $qaSignature.Status.ToString()
        StatusMessage = $qaSignature.StatusMessage
        Subject = $qaSignature.SignerCertificate.Subject
        Issuer = $qaSignature.SignerCertificate.Issuer
        Thumbprint = $qaSignature.SignerCertificate.Thumbprint
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'catalog-signature.json') -Encoding utf8
    if ($qaSignature.Status -ne 'Valid' -or
        $qaSignature.SignerCertificate.Subject -notmatch '(^|,\s*)CN=Microsoft Windows Hardware Compatibility Publisher(,|$)') {
        throw 'The driver catalog lacks a valid Microsoft hardware compatibility publisher signature.'
    }
    $qaOs = [Environment]::OSVersion.Version
    $qaPolicyVersion = "2:$($qaOs.Major).$($qaOs.Minor).$($qaOs.Build)"
    foreach ($qaMemberPath in @($qaInf, $qaSys)) {
        $qaMemberName = [IO.Path]::GetFileName($qaMemberPath)
        & $SignToolPath verify /kp /v /o $qaPolicyVersion /c $qaCat $qaMemberPath 2>&1 |
            Tee-Object -FilePath (Join-Path $LogDirectory "$qaMemberName-signature.log") | Write-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Windows kernel trust or catalog membership failed: $qaMemberName"
        }
    }

    # The vendor's interactive procedure recommends a reboot. This bounded
    # Microsoft-API evaluation does not run that EXE or claim its reboot occurred.
    # Any Windows reboot/restart request is a failure, not an automatic restart.
    & $PythonPath -B (Join-Path $PSScriptRoot 'install_ci_audio_device.py') `
        --driver-directory $qaDriver 2>&1 |
        Tee-Object -FilePath (Join-Path $LogDirectory 'device-install.log') | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'The signed virtual device installation did not complete without restart.' }

    $qaDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $qaEndpoints = @(Get-PnpDevice -Class AudioEndpoint -PresentOnly -ErrorAction SilentlyContinue)
        $qaRender = @($qaEndpoints | Where-Object { $_.Status -eq 'OK' -and $_.FriendlyName -like 'CABLE Input*' })
        if ($qaRender.Count -gt 0) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $qaDeadline)
    $qaEndpoints | Select-Object FriendlyName, Status, InstanceId |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'audio-endpoints.json') -Encoding utf8
    Get-CimInstance -ClassName Win32_PnPSignedDriver |
        Where-Object { $_.DeviceID -like '*VBAUDIO*' -or $_.DriverProviderName -like '*VB-Audio*' } |
        Select-Object DeviceName, DeviceID, DriverVersion, DriverProviderName, InfName, IsSigned |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'installed-driver.json') -Encoding utf8
    if ($qaRender.Count -eq 0) { throw 'No healthy CABLE Input endpoint appeared after installation.' }
    Write-Host 'A virtual render endpoint is present. The unchanged MediaPlayer tests must now verify actual playback.'
} finally {
    $qaSetupLog = Join-Path $env:windir 'INF\setupapi.dev.log'
    if (Test-Path -LiteralPath $qaSetupLog -PathType Leaf) {
        Get-Content -LiteralPath $qaSetupLog -Tail 240 |
            Set-Content -LiteralPath (Join-Path $LogDirectory 'setupapi-tail.log') -Encoding utf8
    }
}
