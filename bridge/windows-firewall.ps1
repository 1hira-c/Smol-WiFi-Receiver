# SPDX-License-Identifier: MIT OR Apache-2.0
param()

$ErrorActionPreference = "Stop"
$displayName = "Smol Receiver Bridge UDP 6970 incoming"
$existing = Get-NetFirewallRule -DisplayName $displayName -ErrorAction SilentlyContinue
if (-not $existing) {
    New-NetFirewallRule `
        -DisplayName $displayName `
        -Direction Inbound `
        -Action Allow `
        -Protocol UDP `
        -LocalPort 6970 `
        -RemoteAddress LocalSubnet `
        -Profile Private | Out-Null
}

$rule = Get-NetFirewallRule -DisplayName $displayName
$port = $rule | Get-NetFirewallPortFilter
$address = $rule | Get-NetFirewallAddressFilter
[pscustomobject]@{
    DisplayName = $rule.DisplayName
    Enabled = $rule.Enabled
    Profile = $rule.Profile
    Direction = $rule.Direction
    Action = $rule.Action
    Protocol = $port.Protocol
    LocalPort = $port.LocalPort
    RemoteAddress = $address.RemoteAddress -join ","
} | Format-List
