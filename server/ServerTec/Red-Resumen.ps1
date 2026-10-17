#requires -Version 5.1
# Pure data transformation: no adapter, DNS, firewall, file or ADB operations.
Set-StrictMode -Version Latest

function Get-ServerTecNetworkPrefix {
    param([Parameter(Mandatory=$true)][string]$Address,
          [Parameter(Mandatory=$true)][int]$Bits)
    $parsed = [System.Net.IPAddress]::Parse($Address)
    $bytes = $parsed.GetAddressBytes()
    if ($Bits -lt 0 -or $Bits -gt ($bytes.Length * 8)) {
        throw 'Longitud de prefijo no valida.'
    }
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $remaining = $Bits - 8 * $i
        if ($remaining -le 0) { $bytes[$i] = 0 }
        elseif ($remaining -lt 8) {
            $mask = (255 -shl (8 - $remaining)) -band 255
            $bytes[$i] = $bytes[$i] -band $mask
        }
    }
    return ([System.Net.IPAddress]::new($bytes)).ToString() + '/' + $Bits
}

function ConvertTo-ServerTecShareableRow {
    param([Parameter(Mandatory=$true)]$Row)
    $address = [System.Net.IPAddress]::Parse([string]$Row.IPAddress)
    $ipv6 = $address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetworkV6
    $assignedBits = [int]$Row.PrefixLength
    if ($assignedBits -lt 0 -or $assignedBits -gt $(if ($ipv6) {128} else {32})) {
        throw 'Longitud de prefijo no valida.'
    }
    # Windows may report temporary addresses as /128. Mask at least the host
    # half even in that case. The result is a REFERENCE, not a claim that the
    # adapter was configured with /64. Do not select the private Prefix field.
    $referenceBits = [Math]::Min($assignedBits, $(if ($ipv6) {64} else {24}))
    $bytes = $address.GetAddressBytes()
    [PSCustomObject]@{
        Interfaz = [string]$Row.InterfaceAlias
        Familia = $(if ($ipv6) {'IPv6'} else {'IPv4'})
        Referencia = Get-ServerTecNetworkPrefix $Row.IPAddress $referenceBits
        BitsAsignados = $assignedBits
        OrigenPrefijo = [string]$Row.PrefixOrigin
        OrigenSufijo = [string]$Row.SuffixOrigin
        IPv6Global = $ipv6 -and (($bytes[0] -band 224) -eq 32)
    }
}
