[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string] $ArchivePath,
    [string] $ProjectPath = 'E:\Projects\LGAEDProjects\02_construccion_depuracion_y_practica_profesional\AssetCheck',
    [switch] $SyntheticFixture,
    [string] $SyntheticExpectedSha256 = '',
    [int] $SyntheticExpectedFiles = 0
)

# READ-ONLY preflight. This never extracts, overwrites, creates backups, resets
# Git, deletes caches, signs a result, or edits the student's project.
# The real release identity is deliberately fixed.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$releaseSha256 = 'b8e6c9abc2d611bfad391facf36f3da0e71993322c18087991ad9f314c51a589'
$releaseFileCount = 35

if ($SyntheticFixture) {
    if ($env:GITHUB_ACTIONS -ne 'true' -or
        $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
        throw 'SyntheticFixture is restricted to disposable GitHub-hosted CI tests.'
    }
    if ($SyntheticExpectedSha256 -cnotmatch '^[a-f0-9]{64}$' -or
        $SyntheticExpectedFiles -lt 1) {
        throw 'Synthetic tests must supply a valid checksum and positive file count.'
    }
    $expectedHash = $SyntheticExpectedSha256
    $expectedFiles = $SyntheticExpectedFiles
}
else {
    if ($SyntheticExpectedSha256 -or $SyntheticExpectedFiles) {
        throw 'Production cannot override the pinned archive identity.'
    }
    $expectedHash = $releaseSha256
    $expectedFiles = $releaseFileCount
}

if (-not [IO.Path]::IsPathFullyQualified($ArchivePath)) {
    throw 'Use an absolute ZIP path. The current directory is not assumed.'
}
if (-not [IO.Path]::IsPathFullyQualified($ProjectPath)) {
    throw 'Use an absolute project path. The current directory is not assumed.'
}
if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw 'The reference archive is not present at the supplied path.'
}
if (-not (Test-Path -LiteralPath $ProjectPath -PathType Container)) {
    throw 'The existing project directory is missing: no integration may proceed.'
}

$archive = [IO.Path]::GetFullPath($ArchivePath)
$project = [IO.Path]::GetFullPath($ProjectPath).TrimEnd('\', '/')
$projectRoot = $project + [IO.Path]::DirectorySeparatorChar
if (((Get-Item -LiteralPath $project).Attributes -band
        [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'The project root is a reparse point and must be inspected manually.'
}

$actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -cne $expectedHash) {
    throw "Archive SHA-256 mismatch. Expected $expectedHash, found $actualHash. No files inspected or installed."
}
Add-Type -AssemblyName System.IO.Compression
# Earlier .NET runtimes do not necessarily verify a ZIP member's CRC-32.
# Stream every member, including files absent from the local project, and
# verify its declared size, standard ZIP CRC-32 and SHA-256 without writing.
if (-not ('AssetCheckZipEntryDigest' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

public static class AssetCheckZipEntryDigest {
    private static readonly uint[] CrcTable = BuildCrcTable();

    private static uint[] BuildCrcTable() {
        uint[] table = new uint[256];
        for (uint i = 0; i < 256; ++i) {
            uint v = i;
            for (int k = 0; k < 8; ++k)
                v = (v & 1U) != 0 ? (v >> 1) ^ 0xEDB88320U : v >> 1;
            table[i] = v;
        }
        return table;
    }

    public static string VerifiedSha256(ZipArchiveEntry entry) {
        using (Stream source = entry.Open())
        using (SHA256 sha = SHA256.Create()) {
            byte[] buffer = new byte[65536];
            long count = 0;
            uint crc = 0xFFFFFFFFU;
            int read;
            while ((read = source.Read(buffer, 0, buffer.Length)) != 0) {
                count = checked(count + read);
                if (count > entry.Length || count > 134217728L)
                    throw new InvalidDataException(
                        "ZIP entry expands beyond its declared length: " + entry.FullName
                    );
                sha.TransformBlock(buffer, 0, read, buffer, 0);
                for (int i = 0; i < read; ++i)
                    crc = CrcTable[(int)((crc ^ buffer[i]) & 0xFFU)] ^ (crc >> 8);
            }
            sha.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
            if (count != entry.Length)
                throw new InvalidDataException(
                    "ZIP uncompressed length mismatch: " + entry.FullName
                );
            crc ^= 0xFFFFFFFFU;
            if (crc != entry.Crc32)
                throw new InvalidDataException(
                    "ZIP CRC-32 mismatch: " + entry.FullName
                );
            return Convert.ToHexString(sha.Hash).ToLowerInvariant();
        }
    }
}
'@
}
$seen = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
# Explicit and implicit directories both matter. Windows cannot extract a
# file named "a" and another file named "a/b" into the same tree.
$archiveFiles = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
$archiveFolders = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
$conflicts = [Collections.Generic.List[string]]::new()
$identical = [Collections.Generic.List[string]]::new()
$new = [Collections.Generic.List[string]]::new()
$blocked = [Collections.Generic.List[string]]::new()
$memberDigests = [Collections.Generic.List[object]]::new()
$directories = 0
$fileCount = 0
$uncompressed = [long]0

$stream = [IO.File]::OpenRead($archive)
try {
    $zip = [IO.Compression.ZipArchive]::new(
        $stream, [IO.Compression.ZipArchiveMode]::Read, $true
    )
    try {
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName
            if ([string]::IsNullOrWhiteSpace($name) -or
                $name.Contains('\') -or
                $name.StartsWith('/') -or
                $name.StartsWith('\')) {
                throw "Unsafe or noncanonical ZIP entry: $name"
            }
            $directory = $name.EndsWith('/')
            $trimmed = if ($directory) { $name.TrimEnd('/') } else { $name }
            if (-not $trimmed -or -not $seen.Add($trimmed)) {
                throw "Empty or duplicate ZIP entry, including case-folded aliases: $name"
            }
            $segments = $trimmed.Split('/')
            if ($segments.Count -gt 60) {
                throw "ZIP entry is unreasonably deeply nested: $name"
            }
            foreach ($segment in $segments) {
                # Never admit package files aimed at Git internals, generated
                # build caches or credentials. This script only reads the ZIP.
                if ($segment -match '^(?i:\.git|\.vs|CMakeFiles|CMakeCache\.txt|build\.ninja|\.env)$' -or
                    ($segment -eq $segments[0] -and
                        $segment -match '^(?i:build(?:-.+)?|out)$')) {
                    throw "ZIP member targets Git metadata, a cache or a secret: $name"
                }
                if ($segment -eq '.' -or $segment -eq '..' -or
                    $segment.Length -eq 0 -or
                    $segment -match '[<>:"|?*]' -or
                    $segment -match '[\x00-\x1f]' -or
                    $segment.EndsWith('.') -or $segment.EndsWith(' ') -or
                    $segment.Split('.')[0] -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
                    throw "Unsafe Windows ZIP component in: $name"
                }
            }
            # Reject internal file/directory collisions before comparing with
            # the local project. Check both archive entry orders, and include
            # implicit parent folders that have no explicit directory entry.
            $parentInArchive = ''
            for ($index = 0; $index -lt $segments.Length - 1; ++$index) {
                $parentInArchive = if ($parentInArchive) {
                    $parentInArchive + '/' + $segments[$index]
                } else {
                    $segments[$index]
                }
                if ($archiveFiles.Contains($parentInArchive)) {
                    throw "ZIP file is also an ancestor directory: $name"
                }
                [void] $archiveFolders.Add($parentInArchive)
            }
            if ($directory) {
                if ($archiveFiles.Contains($trimmed)) {
                    throw "ZIP directory collides with a file: $name"
                }
                [void] $archiveFolders.Add($trimmed)
            } else {
                if ($archiveFolders.Contains($trimmed)) {
                    throw "ZIP file collides with a directory: $name"
                }
                [void] $archiveFiles.Add($trimmed)
            }

            # The high 16 bits of ZIP ExternalAttributes can encode Unix modes.
            # Reject links and special file types. Normal DOS-mode entries use 0.
            $unixKind = ($entry.ExternalAttributes -shr 16) -band 0xF000
            if ($unixKind -ne 0 -and $unixKind -ne 0x8000 -and
                $unixKind -ne 0x4000) {
                throw "ZIP contains a symbolic link or special file: $name"
            }
            if (($directory -and $unixKind -eq 0x8000) -or
                (-not $directory -and $unixKind -eq 0x4000)) {
                throw "ZIP file/directory attributes disagree: $name"
            }
            if ($directory) {
                ++$directories
            }
            else {
                ++$fileCount
                if ($entry.Length -gt 134217728) {
                    throw "An archive member is larger than 128 MiB: $name"
                }
                $uncompressed += $entry.Length
                if ($uncompressed -gt 268435456) {
                    throw 'The ZIP would expand beyond 256 MiB.'
                }
            }

            # Validate every file even when its destination is absent,
            # blocked by a reparse point, or already contains different work.
            if (-not $directory) {
                $memberHash = [AssetCheckZipEntryDigest]::VerifiedSha256($entry)
                [void] $memberDigests.Add([ordered]@{
                    path = $trimmed
                    sha256 = $memberHash
                    bytes = $entry.Length
                    crc32 = ('{0:x8}' -f $entry.Crc32)
                })
            }

            $relative = [IO.Path]::Combine([string[]] $segments)
            $target = [IO.Path]::GetFullPath((Join-Path $project $relative))
            if (-not $target.StartsWith($projectRoot,
                [StringComparison]::OrdinalIgnoreCase)) {
                throw "ZIP member escapes the project root: $name"
            }

            $cursor = $project
            $reparse = $false
            $parentIsFile = $false
            $directoryIsFile = $false
            for ($index = 0; $index -lt $segments.Length; ++$index) {
                $cursor = Join-Path $cursor $segments[$index]
                # Get-Item is needed even for dangling links. Test-Path may
                # incorrectly report such an existing link as missing.
                $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
                if ($null -ne $item) {
                    if (($item.Attributes -band
                            [IO.FileAttributes]::ReparsePoint) -ne 0) {
                        $reparse = $true
                        break
                    }
                    if (-not $item.PSIsContainer) {
                        if ($index -lt $segments.Length - 1) {
                            $parentIsFile = $true
                        }
                        elseif ($directory) {
                            $directoryIsFile = $true
                        }
                        if ($parentIsFile -or $directoryIsFile) {
                            break
                        }
                    }
                }
            }
            if ($reparse) {
                [void] $blocked.Add($trimmed)
                continue
            }
            if ($parentIsFile) {
                [void] $conflicts.Add(
                    $trimmed + ' (an existing parent path is a file)'
                )
                continue
            }
            if ($directory) {
                if ($directoryIsFile) {
                    [void] $conflicts.Add(
                        $trimmed + ' (an existing file occupies this ZIP directory)'
                    )
                }
                # Directory entries have no file content, but their complete
                # local path still must be audited for reparse points.
                continue
            }
            if (Test-Path -LiteralPath $target -PathType Container) {
                [void] $conflicts.Add($trimmed + ' (a directory exists at this file path)')
                continue
            }
            if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
                [void] $new.Add($trimmed)
                continue
            }

            # The archive member was fully verified before destination
            # checks; only the existing local file needs hashing here.
            $localHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($memberHash -ceq $localHash) {
                [void] $identical.Add($trimmed)
            }
            else {
                [void] $conflicts.Add($trimmed)
            }
        }
    }
    finally {
        $zip.Dispose()
    }
}
finally {
    $stream.Dispose()
}

if ($fileCount -ne $expectedFiles) {
    throw "Expected $expectedFiles files in the ZIP, found $fileCount. No installation performed."
}

# Inspect Git state if possible. A non-repository is explicitly reported,
# never silently treated as clean; no Git commands change state.
$gitState = 'git not found'
$git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($git) {
    $insideGit = & $git.Source --no-optional-locks -C $project rev-parse --is-inside-work-tree 2>$null
    if ($LASTEXITCODE -eq 0 -and $insideGit -eq 'true') {
        $status = @(& $git.Source --no-optional-locks -C $project status --porcelain=v1 --untracked-files=normal 2>$null)
        if ($LASTEXITCODE -ne 0) {
            $gitState = 'git status failed; manual review required'
        }
        elseif ($status.Count -eq 0) {
            $gitState = 'clean'
        }
        else {
            $gitState = "dirty or untracked ($($status.Count) status lines); manual review required"
        }
    }
    else {
        $gitState = 'not a Git worktree; manual review required'
    }
}

$manualReviewOnly = (
    $conflicts.Count -gt 0 -or $blocked.Count -gt 0 -or $gitState -ne 'clean'
)
$result = [ordered]@{
    scope = 'Read-only AssetCheck package preflight, NOT installation or Windows build'
    archiveSha256 = $actualHash
    authenticatedReference = (-not $SyntheticFixture)
    archiveFiles = $fileCount
    archiveDirectories = $directories
    expandedBytes = $uncompressed
    fullyDecodedAndCrcVerified = $memberDigests.Count
    memberDigests = $memberDigests.ToArray()
    projectPath = $project
    existingIdentical = $identical.Count
    newFiles = $new.Count
    conflicts = $conflicts.Count
    unsafeExistingReparsePaths = $blocked.Count
    gitState = $gitState
    requiresManualReview = $manualReviewOnly
    noFilesWereWritten = $true
    installationPerformed = $false
    identicalPaths = $identical.ToArray()
    newPaths = $new.ToArray()
    conflictPaths = $conflicts.ToArray()
    blockedPaths = $blocked.ToArray()
}
$result | ConvertTo-Json -Depth 6
