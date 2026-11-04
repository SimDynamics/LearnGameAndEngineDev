[CmdletBinding()]
param(
    [string]$ManagerPath = 'E:\PersonalFiles\Projects\LearnGameAndEngineDev-Playground\Gestion-Ramas-LGAED.ps1'
)

# Import only the update implementation, never the manager's action dispatcher.
# Every merge and commit below belongs to a newly initialized synthetic QA repo.
# Fetch and publication are replaced at their boundaries; no remote is configured.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$checkout = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:QaRoot = Join-Path $checkout ('out/qa/branch-buttons-{0}-{1}' -f $PID, [Guid]::NewGuid().ToString('N'))
$script:Checks = 0
$script:Cases = 0
$script:RemoteRefs = @{}
$script:Publications = [System.Collections.Generic.List[string]]::new()
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Assert-Qa([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw $Description }
    $script:Checks++
}
function Assert-QaPath([string]$Repo) {
    $absolute = [IO.Path]::GetFullPath($Repo)
    $prefix = [IO.Path]::GetFullPath($script:QaRoot).TrimEnd('\') + '\'
    Assert-Qa ($absolute.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) 'A mutating test operation escaped its generated QA root'
    Assert-Qa (Test-Path -LiteralPath (Join-Path $absolute '.git') -PathType Container) 'Synthetic QA repository metadata is missing'
}
function Invoke-QaGit([string]$Repo, [string[]]$Arguments) {
    Assert-QaPath $Repo
    $output = @(& git --no-optional-locks -C $Repo @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Synthetic QA Git failed: $($Arguments -join ' ')" }
    return $output
}
function Write-QaFile([string]$Repo, [string]$Relative, [string]$Content) {
    Assert-QaPath $Repo
    $path = Join-Path $Repo $Relative
    $prefix = [IO.Path]::GetFullPath($Repo).TrimEnd('\') + '\'
    Assert-Qa ([IO.Path]::GetFullPath($path).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) 'A QA fixture file escaped its repository'
    [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
    [IO.File]::WriteAllText($path, $Content, $utf8)
}
function Read-QaFile([string]$Repo, [string]$Relative) {
    return [IO.File]::ReadAllText((Join-Path $Repo $Relative), [Text.Encoding]::UTF8)
}
function Commit-Qa([string]$Repo, [string]$Message) {
    Invoke-QaGit $Repo @('add', '-A', '--', '.') | Out-Null
    Invoke-QaGit $Repo @('commit', '-m', $Message) | Out-Null
    return @(Invoke-QaGit $Repo @('rev-parse', 'HEAD'))[0]
}
function New-QaRepository([string]$Name) {
    $repo = Join-Path $script:QaRoot $Name
    Assert-Qa (-not (Test-Path -LiteralPath $repo)) 'QA repository must be new'
    [IO.Directory]::CreateDirectory($repo) | Out-Null
    & git init --quiet --initial-branch=main -- $repo
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the synthetic QA repository' }
    foreach ($setting in @(
        @('user.name', 'Branch Button QA'), @('user.email', 'branch-qa@example.invalid'),
        @('commit.gpgSign', 'false'), @('tag.gpgSign', 'false'), @('core.autocrlf', 'false'),
        @('core.hooksPath', (Join-Path $repo '.git/empty-hooks')), @('merge.autoStash', 'false')
    )) { Invoke-QaGit $repo @('config', '--local', $setting[0], $setting[1]) | Out-Null }
    [IO.Directory]::CreateDirectory((Join-Path $repo '.git/empty-hooks')) | Out-Null
    Write-QaFile $repo '.gitignore' "ignored.txt`nout/`n"
    Write-QaFile $repo 'common.txt' ((1..14 | ForEach-Object { "line $_" }) -join "`n")
    Write-QaFile $repo 'keep.txt' "original keep`n"
    $base = Commit-Qa $repo 'Synthetic root'
    Invoke-QaGit $repo @('branch', 'Codex', $base) | Out-Null
    Assert-Qa (@(Invoke-QaGit $repo @('remote')).Count -eq 0) 'Synthetic QA must have no configured remote'
    return $repo
}
function Set-QaLine([string]$Repo, [int]$Line, [string]$Value) {
    $lines = (Read-QaFile $Repo 'common.txt').Split([char]10)
    $lines[$Line - 1] = $Value
    Write-QaFile $Repo 'common.txt' ($lines -join "`n")
}
function New-QaDivergence([string]$Name, [switch]$Conflict) {
    $repo = New-QaRepository $Name
    Set-QaLine $repo 2 'Work publication'
    Write-QaFile $repo 'work.txt' "Work original`n"
    $work = Commit-Qa $repo 'Synthetic Work publication'
    Invoke-QaGit $repo @('switch', '--quiet', 'Codex') | Out-Null
    Set-QaLine $repo $(if ($Conflict) { 2 } else { 12 }) 'Codex publication'
    Write-QaFile $repo 'codex.txt' "Codex original`n"
    $codex = Commit-Qa $repo 'Synthetic Codex publication'
    return [pscustomobject]@{ Repo = $repo; Work = $work; Codex = $codex }
}
function New-QaCrissCross([string]$Name, [string]$Destination = 'Codex') {
    $fixture = New-QaDivergence $Name
    $repo = $fixture.Repo
    Invoke-QaGit $repo @('merge', '--no-ff', '--no-edit', '-m', 'Synthetic reciprocal merge into Codex', $fixture.Work) | Out-Null
    $codexMerge = @(Invoke-QaGit $repo @('rev-parse', 'HEAD'))[0]
    Invoke-QaGit $repo @('switch', '--quiet', 'main') | Out-Null
    Invoke-QaGit $repo @('merge', '--no-ff', '--no-edit', '-m', 'Synthetic reciprocal merge into Work', $fixture.Codex) | Out-Null
    $workMerge = @(Invoke-QaGit $repo @('rev-parse', 'HEAD'))[0]
    Assert-Qa (@(Invoke-QaGit $repo @('rev-parse', "${codexMerge}^{tree}"))[0] -eq @(Invoke-QaGit $repo @('rev-parse', "${workMerge}^{tree}"))[0]) 'Reciprocal fixture merges must have identical trees'
    Write-QaFile $repo 'work.txt' "Work new publication`n"
    Write-QaFile $repo 'incoming.txt' "Work incoming`n"
    $work = Commit-Qa $repo 'Synthetic later Work publication'
    Invoke-QaGit $repo @('switch', '--quiet', 'Codex') | Out-Null
    Write-QaFile $repo 'codex-new.txt' "Codex later publication`n"
    $codex = Commit-Qa $repo 'Synthetic later Codex publication'
    Assert-Qa (@(Invoke-QaGit $repo @('merge-base', '--all', $work, $codex)).Count -eq 2) 'The regression fixture must have two best common ancestors'
    Invoke-QaGit $repo @('switch', '--quiet', $Destination) | Out-Null
    return [pscustomobject]@{ Repo = $repo; Work = $work; Codex = $codex }
}
function Use-QaPublications($Fixture) {
    $script:RemoteRefs = @{ main = $Fixture.Work; Codex = $Fixture.Codex }
    $script:Publications.Clear()
}
function Get-QaSnapshot([string]$Repo) {
    Assert-QaPath $Repo
    $files = @(
        Get-ChildItem -LiteralPath $Repo -Recurse -Force -File | Where-Object { -not $_.FullName.StartsWith((Join-Path $Repo '.git') + '\', [StringComparison]::OrdinalIgnoreCase) } |
            Sort-Object FullName | ForEach-Object { $_.FullName.Substring($Repo.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    )
    return [pscustomobject]@{
        Head = @(Invoke-QaGit $Repo @('rev-parse', 'HEAD'))[0]
        Refs = @(Invoke-QaGit $Repo @('for-each-ref', '--format=%(refname) %(objectname)')) -join "`n"
        Index = (Get-FileHash -LiteralPath (Join-Path $Repo '.git/index') -Algorithm SHA256).Hash
        Status = @(Invoke-QaGit $Repo @('status', '--porcelain=v1', '--untracked-files=all', '--ignored')) -join "`n"
        Files = $files -join "`n"
    } | ConvertTo-Json -Compress
}
function Assert-Rejected([string]$Repo, [scriptblock]$Operation, [string]$ExpectedMessage, [string]$Description) {
    $before = Get-QaSnapshot $Repo
    $errorText = $null
    $messages = [System.Collections.Generic.List[string]]::new()
    try { & $Operation 6>&1 | ForEach-Object { $messages.Add([string]$_) } } catch { $errorText = $_.Exception.Message }
    $diagnosis = $errorText + "`n" + ($messages -join "`n")
    Assert-Qa ($null -ne $errorText -and $diagnosis -match $ExpectedMessage) "$Description did not reject for its expected reason: $diagnosis"
    Assert-Qa ((Get-QaSnapshot $Repo) -ceq $before) "$Description changed HEAD, refs, index or working files on rejection"
    Assert-Qa ($script:Publications.Count -eq 0) "$Description reached publication despite rejection"
    Assert-Qa (-not (Test-Path -LiteralPath (Join-Path $Repo '.git/MERGE_HEAD'))) "$Description left a merge in progress"
    $script:Cases++
    Write-Host "PASS $Description"
}
function Assert-Integrated($Fixture, [string]$Description) {
    $repo = $Fixture.Repo
    $head = @(Invoke-QaGit $repo @('rev-parse', 'HEAD'))[0]
    foreach ($tip in @($Fixture.Work, $Fixture.Codex)) {
        Assert-Qa (Git-Ancestro $repo $tip $head) "$Description lost one published history"
    }
    Assert-Qa ($script:Publications.Count -eq 1 -and $script:Publications[0] -eq $head) "$Description did not publish exactly its verified result through the mock"
    Assert-Qa ((Read-QaFile $repo 'common.txt').Contains('Work publication') -and (Read-QaFile $repo 'common.txt').Contains('Codex publication')) "$Description lost compatible edits within one file"
    $script:Cases++
    Write-Host "PASS $Description"
}
function Read-OriginalCheckout([string]$Repo) {
    $head = @(& git --no-optional-locks -C $Repo rev-parse HEAD)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the original checkout without changing it' }
    $index = @(& git --no-optional-locks -C $Repo rev-parse --path-format=absolute --git-path index)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot locate the original checkout index' }
    $status = @(& git --no-optional-locks -C $Repo status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the original checkout state' }
    $diff = @(& git --no-optional-locks -C $Repo diff --binary HEAD)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the original checkout diff' }
    return [pscustomobject]@{ Head = $head -join ''; Index = (Get-FileHash -LiteralPath $index[0] -Algorithm SHA256).Hash; Status = $status -join "`n"; Diff = $diff -join "`n" } | ConvertTo-Json -Compress
}

$parseErrors = $null; $tokens = $null
$manager = [Management.Automation.Language.Parser]::ParseFile($ManagerPath, [ref]$tokens, [ref]$parseErrors)
Assert-Qa ($parseErrors.Count -eq 0) 'The branch manager has PowerShell syntax errors'
$required = @('Ruta-Normal','Misma-Ruta','Dentro-De','Git-Lineas','Git-Ejecutar','Git-Ref','Git-Ancestro','Ruta-Generada',
    'Obtener-ArchivosCambio','Nuevo-Conjunto','Encontrar-Cruces','Mostrar-Detencion','Actualizar')
$definitions = @($manager.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -in $required })
Assert-Qa ($definitions.Count -eq $required.Count) 'The branch manager update functions changed; review the QA import boundary'
foreach ($definition in $definitions) { . ([scriptblock]::Create($definition.Extent.Text)) }

# These boundaries may only read the synthetic fixture; the body of Actualizar,
# its Git diff/merge-base/merge-tree/merge checks and final tree check stay real.
function Comprobar-Carpeta([string]$Repo, [string]$Rama) {
    Assert-QaPath $Repo
    Assert-Qa (@(Invoke-QaGit $Repo @('branch','--show-current'))[0] -ceq $Rama) 'Unexpected synthetic destination branch'
}
function Comprobar-Remoto([string]$Repo) {
    Assert-QaPath $Repo
    Assert-Qa (@(Invoke-QaGit $Repo @('remote')).Count -eq 0) 'The QA test must not contact any configured remote'
}
function Traer-Rama([string]$Repo, [string]$Rama) {
    Assert-QaPath $Repo
    return $script:RemoteRefs[$Rama]
}
function Publicar-Actualizacion([string]$Repo, [string]$Rama) {
    Comprobar-Carpeta $Repo $Rama
    $script:Publications.Add(@(Invoke-QaGit $Repo @('rev-parse','HEAD'))[0])
}
function Mostrar-Contenido([string]$Repo) { Assert-QaPath $Repo }

$originals = @{}
foreach ($owner in @('Work', 'Codex')) {
    $path = Join-Path (Split-Path -Parent $checkout) "LearnGameAndEngineDev-$owner"
    $originals[$path] = Read-OriginalCheckout $path
}
[IO.Directory]::CreateDirectory($script:QaRoot) | Out-Null
foreach ($parent in @($script:QaRoot, (Split-Path -Parent $script:QaRoot), $checkout)) {
    Assert-Qa (-not ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'The generated QA area must not use reparse points'
}
$savedGitEnvironment = @{}
$managerEnvironment = @{ GIT_OPTIONAL_LOCKS = '0'; GIT_MERGE_AUTOEDIT = 'no'; GIT_PAGER = 'cat' }
foreach ($name in $managerEnvironment.Keys) {
    $savedGitEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    foreach ($name in $managerEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $managerEnvironment[$name], 'Process')
    }
    $cross = New-QaCrissCross 'cross-to-codex'
    Use-QaPublications $cross
    # codex.txt occurs in one historical base diff, but is already integrated
    # into both tips. The planned result never touches this local modification.
    Write-QaFile $cross.Repo 'codex.txt' "local Codex draft preserved`n"
    Actualizar $cross.Repo 'Codex' 'main' 'Codex' 'Work'
    Assert-Integrated $cross 'criss-cross Work to Codex with compatible same-file edits'
    Assert-Qa ((Read-QaFile $cross.Repo 'codex.txt') -ceq "local Codex draft preserved`n") 'An already integrated file lost its independent local modification'
    Assert-Qa ((Read-QaFile $cross.Repo 'incoming.txt') -ceq "Work incoming`n") 'The criss-cross incoming file was not received'

    $reverse = New-QaCrissCross 'cross-to-work' 'main'
    Use-QaPublications $reverse
    Actualizar $reverse.Repo 'main' 'Codex' 'Work' 'Codex'
    Assert-Integrated $reverse 'criss-cross Codex to Work'
    Assert-Qa ((Read-QaFile $reverse.Repo 'codex-new.txt') -ceq "Codex later publication`n") 'The reverse update lost Codex publication'

    $fast = New-QaRepository 'fast-forward'
    $old = @(Invoke-QaGit $fast @('rev-parse','Codex'))[0]
    Write-QaFile $fast 'incoming.txt' "fast-forward publication`n"
    $tip = Commit-Qa $fast 'Synthetic forward publication'
    Invoke-QaGit $fast @('switch','--quiet','Codex') | Out-Null
    Use-QaPublications ([pscustomobject]@{ Work = $tip; Codex = $old })
    Actualizar $fast 'Codex' 'main' 'Codex' 'Work'
    Assert-Qa (@(Invoke-QaGit $fast @('rev-parse','HEAD'))[0] -eq $tip -and $script:Publications.Count -eq 1) 'Fast-forward created another commit or omitted publication'
    $script:Cases++; Write-Host 'PASS fast-forward without an extra integration commit'

    $compatible = New-QaDivergence 'single-base-compatible'
    Use-QaPublications $compatible
    Actualizar $compatible.Repo 'Codex' 'main' 'Codex' 'Work'
    Assert-Integrated $compatible 'single-base compatible edits within one file'

    $conflict = New-QaDivergence 'actual-conflict' -Conflict
    Use-QaPublications $conflict
    Assert-Rejected $conflict.Repo { Actualizar $conflict.Repo 'Codex' 'main' 'Codex' 'Work' } 'conflictos' 'actual content conflict before checkout or publication'

    $local = New-QaCrissCross 'local-incoming'
    Use-QaPublications $local
    Write-QaFile $local.Repo 'work.txt' "local work draft`n"
    Assert-Rejected $local.Repo { Actualizar $local.Repo 'Codex' 'main' 'Codex' 'Work' } 'modificaciones locales' 'local modification of an incoming tracked file'

    $ignored = New-QaDivergence 'ignored-collision'
    Invoke-QaGit $ignored.Repo @('switch','--quiet','main') | Out-Null
    Write-QaFile $ignored.Repo 'ignored.txt' "published ignored path`n"
    Invoke-QaGit $ignored.Repo @('add','-f','--','ignored.txt') | Out-Null
    Invoke-QaGit $ignored.Repo @('commit','-m','Synthetic explicitly tracked ignored path') | Out-Null
    $ignored.Work = @(Invoke-QaGit $ignored.Repo @('rev-parse','HEAD'))[0]
    Invoke-QaGit $ignored.Repo @('switch','--quiet','Codex') | Out-Null
    Write-QaFile $ignored.Repo 'ignored.txt' "local ignored original`n"
    Use-QaPublications $ignored
    Assert-Rejected $ignored.Repo { Actualizar $ignored.Repo 'Codex' 'main' 'Codex' 'Work' } 'chocan' 'existing ignored file at an incoming path'

    $staged = New-QaDivergence 'staged-divergence'
    Use-QaPublications $staged
    Write-QaFile $staged.Repo 'keep.txt' "staged local draft`n"
    Invoke-QaGit $staged.Repo @('add','--','keep.txt') | Out-Null
    Assert-Rejected $staged.Repo { Actualizar $staged.Repo 'Codex' 'main' 'Codex' 'Work' } 'cambios preparados' 'unrelated staged changes during divergent integration'

}
finally {
    try {
        foreach ($path in $originals.Keys) {
            Assert-Qa ((Read-OriginalCheckout $path) -ceq $originals[$path]) "The original checkout changed during isolated branch-button QA: $path"
        }
    }
    finally {
        foreach ($name in $savedGitEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedGitEnvironment[$name], 'Process')
        }
    }
}
Write-Host ("PASS branch button regression: {0} cases; original checkouts unchanged. Synthetic QA retained at {1}" -f $script:Cases, $script:QaRoot)
