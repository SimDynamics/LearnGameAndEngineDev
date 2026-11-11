[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $ArchivePath,
    [string] $ProjectPath = 'E:\Projects\LGAEDProjects\02_construccion_depuracion_y_practica_profesional\AssetCheck',
    [switch] $SyntheticFixture,
    [string] $SyntheticExpectedSha256 = '',
    [int] $SyntheticExpectedFiles = 0
)

# READ-ONLY: verify the complete ZIP using the pinned preflight, then parse
# the embedded Integrar-AssetCheck.ps1 as INERT TEXT. Never invoke, dot-source,
# extract or install it. Literal path mentions are only clues for review and
# are NOT an installation allowlist or 31-file authorization.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$inspector = Join-Path $PSScriptRoot 'Inspect-AssetCheckArchive.ps1'
$options = @{
    ArchivePath = $ArchivePath
    ProjectPath = $ProjectPath
}
if ($SyntheticFixture) {
    $options.SyntheticFixture = $true
    $options.SyntheticExpectedSha256 = $SyntheticExpectedSha256
    $options.SyntheticExpectedFiles = $SyntheticExpectedFiles
}
elseif ($SyntheticExpectedSha256 -or $SyntheticExpectedFiles) {
    throw 'The pinned production ZIP cannot have its digest or count overridden.'
}

$report = (& $inspector @options | Out-String) | ConvertFrom-Json
if (@($report.memberDigests).Count -ne $report.archiveFiles -or
    $report.fullyDecodedAndCrcVerified -ne $report.archiveFiles -or
    $report.installationPerformed -or -not $report.inventoryPathsAreNotInstallInstructions) {
    throw 'The ZIP did not pass a complete read-only integrity inspection.'
}

$archive = [IO.Path]::GetFullPath($ArchivePath)
$knownSha = [string]$report.archiveSha256
Add-Type -AssemblyName System.IO.Compression
$file = [IO.File]::OpenRead($archive)
$integratorName = $null
$integratorSha = $null
$parameters = @()
$literalReferences = @()
$sourceBytes = 0
$parseErrorCount = 0
try {
    $zip = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Read, $true)
    try {
        # Only these canonical relative locations are examined. Never try to
        # locate an arbitrary similarly named script and treat it as trusted.
        $scripts = @($zip.Entries | Where-Object {
            $_.FullName -ceq 'scripts/Integrar-AssetCheck.ps1' -or
            $_.FullName -ceq 'Integrar-AssetCheck.ps1'
        })
        if ($scripts.Count -gt 1) {
            throw 'Two competing integration scripts exist in the ZIP.'
        }
        if ($scripts.Count -eq 1) {
            $entry = $scripts[0]
            $integratorName = $entry.FullName
            $sourceBytes = [long]$entry.Length
            if ($sourceBytes -gt 1048576) {
                throw 'Embedded integration script exceeds the 1 MiB review limit.'
            }
            $bytes = [byte[]]::new([int]$sourceBytes)
            $input = $entry.Open()
            try {
                $offset = 0
                while ($offset -lt $bytes.Length) {
                    $read = $input.Read($bytes, $offset, $bytes.Length - $offset)
                    if ($read -eq 0) {
                        throw 'Embedded integration script was unexpectedly truncated.'
                    }
                    $offset += $read
                }
            }
            finally {
                $input.Dispose()
            }
            $sha = [Security.Cryptography.SHA256]::HashData($bytes)
            $integratorSha = [Convert]::ToHexString($sha).ToLowerInvariant()
            $inventoryMatch = @($report.memberDigests | Where-Object {
                $_.path -ceq $integratorName -and $_.sha256 -ceq $integratorSha
            })
            if ($inventoryMatch.Count -ne 1) {
                throw 'Embedded integration script does not match the preflight digest.'
            }
            # Strict decoding detects malformed UTF-8. The PowerShell parser
            # builds an AST only; it does not execute statements or commands.
            $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
            $tokens = $null
            $parseErrors = $null
            $ast = [Management.Automation.Language.Parser]::ParseInput(
                $text, [ref]$tokens, [ref]$parseErrors
            )
            $parseErrorCount = @($parseErrors).Count
            if ($parseErrorCount -gt 0) {
                throw "Embedded integration script has $parseErrorCount syntax errors; review manually."
            }
            if ($null -ne $ast.ParamBlock) {
                $parameters = @($ast.ParamBlock.Parameters | ForEach-Object {
                    $_.Name.VariablePath.UserPath
                })
            }
            $zipNames = [Collections.Generic.HashSet[string]]::new(
                [StringComparer]::Ordinal
            )
            foreach ($member in $report.memberDigests) {
                [void]$zipNames.Add([string]$member.path)
            }
            $references = [Collections.Generic.HashSet[string]]::new(
                [StringComparer]::Ordinal
            )
            $literals = $ast.FindAll({
                param($node)
                $node -is [Management.Automation.Language.StringConstantExpressionAst]
            }, $true)
            foreach ($literal in $literals) {
                $candidate = [string]$literal.Value
                # A literal that matches a complete ZIP path is a *hint*,
                # not evidence that it is selected for installation.
                if ($zipNames.Contains($candidate)) {
                    [void] $references.Add($candidate)
                }
            }
            $literalReferences = @($references | Sort-Object -CaseSensitive)
        }
    }
    finally {
        $zip.Dispose()
    }
}
finally {
    $file.Dispose()
}
$finalSha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($finalSha -cne $knownSha) {
    throw 'ZIP changed during the read-only integration-plan audit.'
}

[ordered]@{
    scope = 'Read-only ZIP integration-script AST inventory, not installation'
    archiveSha256 = $knownSha
    authenticatedRelease = [bool]$report.authenticatedReference
    archiveFilesVerified = $report.fullyDecodedAndCrcVerified
    expectedInstallableCount = $(if ($SyntheticFixture) { $null } else { 31 })
    embeddedScriptFound = ($null -ne $integratorName)
    embeddedScriptPath = $integratorName
    embeddedScriptSha256 = $integratorSha
    embeddedScriptBytes = $sourceBytes
    embeddedScriptParseErrors = $parseErrorCount
    parameterNames = @($parameters)
    literalInventoryPathMentions = @($literalReferences)
    literalMentionsAreNotAnAllowlist = $true
    actual31PathSelectionKnown = $false
    excludedFourPathsVerified = $false
    installationAuthorized = $false
    bytesWrittenToStudentProject = 0
} | ConvertTo-Json -Depth 8
