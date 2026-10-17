#requires -Version 5.1
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $env:LOCALAPPDATA 'ServerTec\reports'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-NetworkPrefix([string]$Address, [int]$Bits) {
    $bytes = [System.Net.IPAddress]::Parse($Address).GetAddressBytes()
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $remaining = $Bits - 8 * $i
        if ($remaining -le 0) { $bytes[$i] = 0 }
        elseif ($remaining -lt 8) { $bytes[$i] = $bytes[$i] -band (255 -shl (8 - $remaining)) }
    }
    return ([System.Net.IPAddress]::new($bytes)).ToString() + '/' + $Bits
}

$rows = @()
foreach ($ip in @(Get-NetIPAddress -AddressState Preferred)) {
    $parsed = [System.Net.IPAddress]::Parse($ip.IPAddress)
    if ([System.Net.IPAddress]::IsLoopback($parsed)) { continue }
    $bytes = $parsed.GetAddressBytes()
    $globalV6 = $bytes.Length -eq 16 -and (($bytes[0] -band 224) -eq 32)
    $rows += [PSCustomObject]@{
        Interface = $ip.InterfaceAlias
        AddressFamily = [string]$ip.AddressFamily
        Address = $ip.IPAddress
        Prefix = Get-NetworkPrefix $ip.IPAddress $ip.PrefixLength
        PrefixOrigin = [string]$ip.PrefixOrigin
        SuffixOrigin = [string]$ip.SuffixOrigin
        SkipAsSource = $ip.SkipAsSource
        GlobalIPv6 = $globalV6
    }
}
$defaultRoutes = @(Get-NetRoute | Where-Object { $_.DestinationPrefix -in @('0.0.0.0/0', '::/0') } |
    Select-Object InterfaceAlias, DestinationPrefix, NextHop, RouteMetric)
$pythonCommand = Get-Command py, python -ErrorAction SilentlyContinue | Select-Object -First 1
$adb = Get-Command adb -ErrorAction SilentlyContinue
$devices = @()
if ($adb) { $devices = @(& $adb.Source devices -l 2>&1 | ForEach-Object { [string]$_ }) }
$firewall = @()
try { $firewall = @(Get-NetFirewallProfile | Select-Object Name, Enabled, DefaultInboundAction) }
catch { $firewall = @('No se pudo leer el perfil; no se modifico nada.') }
$report = [ordered]@{
    GeneratedUtc = [DateTime]::UtcNow.ToString('o')
    Addresses = $rows
    DefaultRoutes = $defaultRoutes
    Firewall = $firewall
    PythonFound = [bool]$pythonCommand
    AdbDevices = $devices
    WanIPv4 = 'No determinada: consultar la direccion WAN en el router, no la IPv4 local de Windows.'
    PublicReachability = 'NO comprobada. Una IPv6 global no prueba que acepte conexiones entrantes.'
    ChangesMade = 'Ninguno: no se cambiaron DNS, DHCP, firewall, puertos ni dispositivos.'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$path = Join-Path $OutputDirectory ('network-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding UTF8
Write-Host '=== RESUMEN PARA COMPARTIR ==='
$rows | Select-Object Interface, AddressFamily, Prefix, PrefixOrigin, SuffixOrigin, GlobalIPv6 | Format-Table -AutoSize
Write-Host ('Python encontrado: ' + [bool]$pythonCommand)
Write-Host ('ADB encontrado: ' + [bool]$adb)
Write-Host 'La direccion WAN IPv4 y el acceso entrante aun no estan comprobados.'
Write-Host 'No se modifico el sistema ni se consulto ningun servicio de IP externo.'
Write-Host '=== FIN DEL RESUMEN ==='
Write-Host ('Informe completo PRIVADO: ' + $path)
Write-Host 'No subas ese JSON a GitHub; comparte solo el resumen y el modelo del router.'
