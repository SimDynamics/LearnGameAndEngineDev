#requires -Version 5.1
# No Pester dependency. Run on Windows PowerShell 5.1 or PowerShell 7.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Red-Resumen.ps1')
$count = 0
foreach ($case in @(
    @('2001:db8:ab:cd:1234:5678:9abc:def0', 128, '2001:db8:ab:cd::/64'),
    @('2001:db8:ab:cd:1234:5678:9abc:def0', 64, '2001:db8:ab:cd::/64'),
    @('2001:db8:ab:cd:1234:5678:9abc:def0', 56, '2001:db8:ab::/56'),
    @('2001:db8:1:2:3:4:5:6', 120, '2001:db8:1:2::/64'),
    @('fe80::1234:5678:9abc:def0%14', 128, 'fe80::/64'),
    @('192.168.1.123', 32, '192.168.1.0/24'),
    @('192.168.1.123', 24, '192.168.1.0/24'),
    @('10.1.2.3', 16, '10.1.0.0/16')
)) {
    $raw = [PSCustomObject]@{
        InterfaceAlias='Ethernet'; IPAddress=$case[0]; PrefixLength=$case[1]
        PrefixOrigin='RouterAdvertisement'; SuffixOrigin='Random'
        SecretValue='NEVER_PRINT'; MacAddress='NEVER_PRINT'
    }
    $summary = ConvertTo-ServerTecShareableRow $raw
    if ($summary.Referencia -ne $case[2]) { throw 'Enmascaramiento de prefijo incorrecto.' }
    if ($summary.BitsAsignados -ne $case[1]) { throw 'Se perdio la longitud asignada real.' }
    if ($summary.PSObject.Properties.Name -contains 'IPAddress') { throw 'Direccion completa expuesta.' }
    $json = $summary | ConvertTo-Json -Compress
    if ($json.Contains('NEVER_PRINT') -or $json.Contains($case[0])) { throw 'Datos privados en resumen.' }
    $count++
}
$failed = $false
try { Get-ServerTecNetworkPrefix '192.168.1.123' 33 | Out-Null } catch { $failed = $true }
if (-not $failed) { throw 'Una longitud IPv4 invalida fue aceptada.' }
$count++
Write-Host ("Resumen de red: {0}/{0} pruebas aprobadas; sin consultar adaptadores." -f $count)
