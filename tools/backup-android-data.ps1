# Read-only Android snapshot before instrumentation. Never clears or stops the app.
[CmdletBinding()]
param(
    [string]$Adb = 'C:\Users\GameDev\AppData\Local\Android\Sdk\platform-tools\adb.exe',
    [ValidatePattern('^[A-Za-z0-9_.:-]+$')][string]$Serial = 'ARGK6R3417001970',
    [string]$DestinationRoot = (Join-Path $PSScriptRoot '..\build\android-data-backups')
)
$ErrorActionPreference = 'Stop'
$package = 'com.learngraphicsprogramming.app'
$adbPath = (Resolve-Path -LiteralPath $Adb).Path
$backupDirectory = Join-Path ([IO.Path]::GetFullPath($DestinationRoot)) ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupDirectory | Out-Null

function Read-Device([string[]]$Arguments) {
    $result = & $adbPath -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ADB read failed: $($Arguments -join ' '): $($result -join ' ')" }
    return $result
}

function Save-BinaryArchive([string[]]$Arguments, [string]$Name) {
    # PowerShell 5.1 text redirection corrupts binary tar streams: copy raw bytes.
    $archivePath = Join-Path $backupDirectory $Name
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $adbPath
    # Arguments contain only the validated serial and fixed package/paths.
    $start.Arguments = (@('-s', $Serial) + $Arguments) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    $stream = $null
    try {
        if (-not $process.Start()) { throw 'Could not start the backup reader.' }
        $stderr = $process.StandardError.ReadToEndAsync()
        $stream = [IO.File]::Open($archivePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $process.StandardOutput.BaseStream.CopyTo($stream)
        $stream.Dispose(); $stream = $null
        $process.WaitForExit()
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Backup incomplete ($($process.ExitCode)): $errorText; partial archive retained at $archivePath" }
        $entries = & tar -tf $archivePath 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Backup archive verification failed: $archivePath" }
        $entries | Set-Content -LiteralPath (Join-Path $backupDirectory "$Name.entries.txt") -Encoding UTF8
        return [pscustomobject]@{ Name=$Name; Bytes=(Get-Item -LiteralPath $archivePath).Length; Sha256=(Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash; Entries=@($entries).Count }
    } finally {
        if ($stream) { $stream.Dispose() }
        $process.Dispose()
    }
}

Read-Device @('get-state') | Out-Null
$installed = Read-Device @('shell', 'pm', 'path', $package)
$testInstalled = Read-Device @('shell', 'pm', 'path', "$package.test")
$uidBefore = (Read-Device @('shell', 'run-as', $package, 'id') -join ' ').Trim()
$inventory = Read-Device @('shell', 'run-as', $package, 'find', '.', '-type', 'f')
$inventory | Set-Content -LiteralPath (Join-Path $backupDirectory 'private-files.txt') -Encoding UTF8
$archives = @(Save-BinaryArchive @('exec-out', 'run-as', $package, 'tar', '-cf', '-', '.') 'private-data.tar')
$external = "/sdcard/Android/data/$package/files"
# A failed `test -d` alone cannot distinguish absence, denied access or an ADB
# failure. Accept absence only after walking readable/searchable parent folders.
# Existing readable app storage does not require listing its restricted ancestors.
$externalProbe = @'
readable_directory() {
    [ -r "$1" ] && [ -x "$1" ] && ls -A "$1" >/dev/null
}
if [ -d '__EXTERNAL__' ]; then
    if readable_directory '__EXTERNAL__'; then
        echo PRESENT
        exit 0
    fi
    echo 'External app storage is not readable/searchable' >&2
    exit 65
fi
for candidate in /sdcard /sdcard/Android /sdcard/Android/data /sdcard/Android/data/__PACKAGE__ __EXTERNAL__; do
    if [ -d "$candidate" ]; then
        if ! readable_directory "$candidate"; then
            echo "Cannot verify external storage parent: $candidate" >&2
            exit 65
        fi
    elif [ -e "$candidate" ] || [ -L "$candidate" ]; then
        echo "External storage path is not a directory: $candidate" >&2
        exit 66
    else
        if [ "$candidate" = /sdcard ]; then
            echo 'External storage root is unavailable; absence is not verified' >&2
            exit 68
        fi
        echo "ABSENT:$candidate"
        exit 0
    fi
done
echo 'Unexpected external storage probe state' >&2
exit 67
'@
$externalProbe = $externalProbe.Replace('__EXTERNAL__', $external).Replace('__PACKAGE__', $package).Replace("`r`n", "`n")
$externalPresence = @((Read-Device @('shell', $externalProbe)) | ForEach-Object { "$($_)".Trim() })
if ($externalPresence.Count -ne 1 -or
    ($externalPresence[0] -ne 'PRESENT' -and $externalPresence[0] -notmatch '^ABSENT:/sdcard/Android(?:/data(?:/com\.learngraphicsprogramming\.app(?:/files)?)?)?$')) {
    throw "External storage probe did not return a verified state: $($externalPresence -join ' ')"
}
if ($externalPresence[0] -eq 'PRESENT') {
    $externalInventory = Read-Device @('shell', 'find', $external, '-type', 'f')
    $externalInventory | Set-Content -LiteralPath (Join-Path $backupDirectory 'external-files.txt') -Encoding UTF8
    $archives += Save-BinaryArchive @('exec-out', 'tar', '-cf', '-', '-C', $external, '.') 'external-data.tar'
}
$uidAfter = (Read-Device @('shell', 'run-as', $package, 'id') -join ' ').Trim()
if ($uidAfter -ne $uidBefore) { throw 'Package identity changed during backup; do not run tests against this snapshot.' }
[pscustomobject]@{
    CreatedUtc=[DateTime]::UtcNow.ToString('o'); Serial=$Serial; Package=$package; Identity=$uidBefore
    InstalledApks=@($installed); InstalledTestApks=@($testInstalled); Archives=$archives
    ExternalStorage=$externalPresence[0]
    PrivateWavs=@($inventory | Where-Object { $_ -match '\.wav$' }).Count
    PrivateOpus=@($inventory | Where-Object { $_ -match '\.ogg$' }).Count
    Note='Live read-only snapshot; app writes may continue. No app data was restored or removed.'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backupDirectory 'manifest.json') -Encoding UTF8
Write-Host "Verified Android data backup: $backupDirectory"
$archives | Format-Table Name,Bytes,Entries,Sha256 -AutoSize
