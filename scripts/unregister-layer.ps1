$ErrorActionPreference = 'Stop'
# This helper detects Wine and removes its prefix-local HKLM registration too.
& (Join-Path $PSScriptRoot 'TF2VRSeatedControlsSetup.exe') unregister
if ($LASTEXITCODE -ne 0) { throw "Seated Controls unregistration failed ($LASTEXITCODE)" }
