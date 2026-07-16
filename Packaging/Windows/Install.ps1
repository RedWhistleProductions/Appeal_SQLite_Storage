param(
    [string]$AppealResources = "$HOME\Documents\Appeal\Resources"
)

$ErrorActionPreference = 'Stop'
$Package = $PSScriptRoot
$Manifest = Get-Content "$Package\plugin.json" -Raw | ConvertFrom-Json
$Artifact = Join-Path $Package $Manifest.artifacts.windows
$Destination = Join-Path $AppealResources "Modules\$($Manifest.module)\$($Manifest.name)"

if(-not (Test-Path $Artifact)) { throw "Missing release artifact: $Artifact" }
New-Item -ItemType Directory -Force $Destination | Out-Null
Copy-Item $Artifact $Destination -Force

$Runtime = Join-Path $Package 'runtime'
foreach($Dependency in $Manifest.runtime_dependencies.windows) {
    $Source = Join-Path $Runtime $Dependency
    if(-not (Test-Path $Source)) { throw "Missing downloaded runtime dependency: $Source" }
    Copy-Item $Source $Destination -Force
}

Write-Host "Installed $($Manifest.name) in $Destination"
