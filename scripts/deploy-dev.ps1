<#
.SYNOPSIS
  Copies the built plugin and its data into the Vortex staging folder for this mod.

.DESCRIPTION
  Vortex owns Data/, so the dev build is installed as an ordinary Vortex mod rather
  than dropped loose into the game folder. Run this after a build, then deploy in
  Vortex. Refuses to run while Vortex is open: writing into the staging pool behind
  a running Vortex is how staging folders get half-written.
#>
[CmdletBinding()]
param(
	[string] $Staging = 'D:\Vortex\fallout4\mods\AutonomyFramework-dev',
	[string] $Config  = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$vortex = Get-Process -Name 'Vortex' -ErrorAction SilentlyContinue
if ($vortex) {
	throw "Vortex is running (PID $($vortex.Id -join ', ')). Close it before deploying the dev build."
}

$dll = Join-Path $root "build\$Config\AutonomyFramework.dll"
if (-not (Test-Path $dll)) {
	throw "No build at $dll — build first."
}

$plugins = Join-Path $Staging 'F4SE\Plugins'
New-Item -ItemType Directory -Force -Path (Join-Path $plugins 'AutonomyFramework') | Out-Null

Copy-Item $dll (Join-Path $plugins 'AutonomyFramework.dll') -Force
Copy-Item (Join-Path $root 'data\F4SE\Plugins\AutonomyFramework.ini') $plugins -Force
Copy-Item (Join-Path $root 'data\F4SE\Plugins\AutonomyFramework\*.json') (Join-Path $plugins 'AutonomyFramework') -Force

$built = (Get-Item $dll).LastWriteTime
Write-Host "Deployed to $Staging (dll built $built)."
Write-Host "Now open Vortex, enable 'AutonomyFramework-dev' and deploy."
Write-Host "The log lands in Documents\My Games\Fallout4\F4SE\AutonomyFramework.log."
