<#
.SYNOPSIS
  Copies the built plugin and its data into the Vortex staging folder for this mod.

.DESCRIPTION
  Vortex owns Data/, so the dev build is installed as an ordinary Vortex mod rather
  than dropped loose into the game folder. Run this after a build; Vortex deploys by
  hardlink, so an existing deployment updates in place and needs no redeploy.

  Refuses to run while Vortex or the game is open: the first would half-write the
  staging folder, the second holds the DLL open so the copy silently does nothing.

  ASCII only, deliberately. This file gets run by Windows PowerShell 5.1 as well as
  pwsh 7, and 5.1 reads a BOM-less UTF-8 script as ANSI -- one em dash in a string
  was enough to turn it into a parse error that still exited 0.
#>
[CmdletBinding()]
param(
    [string] $Staging = 'D:\Vortex\fallout4\mods\AutonomyFramework-dev',
    [string] $Config  = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# The game is a hard stop: it holds the DLL open, so the copy would silently do
# nothing. Vortex is only a warning -- it is the normal working state, and a new
# file needs its Deploy button anyway.
$game = Get-Process -Name 'Fallout4' -ErrorAction SilentlyContinue
if ($game) {
    throw "Fallout4 is running (PID $($game.Id -join ', ')). Close it before deploying."
}

$vortex = Get-Process -Name 'Vortex' -ErrorAction SilentlyContinue

$dll = Join-Path $root "build\$Config\AutonomyFramework.dll"
if (-not (Test-Path $dll)) {
    throw "No build at $dll. Build first."
}

$plugins = Join-Path $Staging 'F4SE\Plugins'
New-Item -ItemType Directory -Force -Path (Join-Path $plugins 'AutonomyFramework') | Out-Null

Copy-Item $dll (Join-Path $plugins 'AutonomyFramework.dll') -Force
Copy-Item (Join-Path $root 'data\F4SE\Plugins\AutonomyFramework.ini') $plugins -Force
Copy-Item (Join-Path $root 'data\F4SE\Plugins\AutonomyFramework\*.json') (Join-Path $plugins 'AutonomyFramework') -Force

# Report what actually landed in both places. A deploy that silently did nothing
# looks exactly like a deploy that worked, so print the evidence.
foreach ($path in (Join-Path $plugins 'AutonomyFramework.dll'),
                  (Join-Path 'D:\GOGGames\Fallout 4 GOTY\Data\F4SE\Plugins' 'AutonomyFramework.dll')) {
    if (Test-Path $path) {
        $item = Get-Item $path
        Write-Host ("  {0}  {1} bytes  {2:HH:mm:ss}" -f $item.FullName, $item.Length, $item.LastWriteTime)
    } else {
        Write-Host "  MISSING: $path"
    }
}
if ($vortex) {
    Write-Host "Vortex is open. Existing files updated in place through their hardlinks; press Deploy if you added a new one."
}
Write-Host "Deployed. Launch through F4SE; the log is Documents\My Games\Fallout4\F4SE\AutonomyFramework.log"
