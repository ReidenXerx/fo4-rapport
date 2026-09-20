<#
.SYNOPSIS
  Builds the distributable archive: a Data-rooted zip a mod manager can install.

.DESCRIPTION
  deploy-dev.ps1 puts the same files into Vortex staging for testing. This makes
  the thing a stranger downloads, and the two must not drift -- so the file list
  below is the same list, in the same order, and a change to one belongs in both.

  Everything here is BUILT, not committed: the dll, the pex, the esp and the
  overlay textures are all generated. A release assembled from a stale working
  tree is the classic way to ship a version that never existed, so this refuses
  rather than shipping something it cannot find.

  ASCII only, deliberately -- see deploy-dev.ps1 for why.
#>
[CmdletBinding()]
param(
    [string] $Config  = 'Release',
    [string] $OutDir  = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'build\release' }

# Version comes from the plugin, which is the only place that declares one. A
# hand-typed version in a packaging script is a version that will be wrong.
$versionSource = Join-Path $root 'src\Version.h'
$version = 'dev'
if (Test-Path $versionSource) {
    $m = Select-String -Path $versionSource -Pattern 'VERSION\s*"?([0-9]+\.[0-9]+\.[0-9]+)' |
         Select-Object -First 1
    if ($m) { $version = $m.Matches[0].Groups[1].Value }
}
if ($version -eq 'dev') {
    $cmake = Join-Path $root 'CMakeLists.txt'
    if (Test-Path $cmake) {
        $m = Select-String -Path $cmake -Pattern 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
             Select-Object -First 1
        if ($m) { $version = $m.Matches[0].Groups[1].Value }
    }
}

$stage = Join-Path $OutDir "Rapport-$version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

function Copy-Into {
    param([string] $From, [string] $To, [switch] $Required, [switch] $Tree)
    $src = Join-Path $root $From
    if (-not (Test-Path $src)) {
        if ($Required) { throw "MISSING: $From -- build it before making a release." }
        Write-Host "  skipped (absent): $From"
        return 0
    }
    $dst = Join-Path $stage $To
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    if ($Tree) {
        Copy-Item (Join-Path $src '*') $dst -Recurse -Force
    } else {
        Copy-Item $src $dst -Force
    }
    return @(Get-ChildItem $dst -Recurse -File).Count
}

Write-Host "Assembling Rapport $version"

# The plugin and its configuration.
Copy-Into -From "build\$Config\Rapport.dll" -To 'F4SE\Plugins' -Required | Out-Null
Copy-Into -From 'data\F4SE\Plugins\Rapport.ini' -To 'F4SE\Plugins' -Required | Out-Null
Copy-Into -From 'data\F4SE\Plugins\Rapport' -To 'F4SE\Plugins\Rapport' -Tree -Required | Out-Null

# DIAGNOSTICS OFF IN A RELEASE, forced here rather than trusted.
#
# debug.json ships with active="debug" because that is the right default on a
# DEVELOPMENT machine, and the tree copy above takes it verbatim. Papyrus
# tracing slows the script engine -- the exact cost this mod exists not to add --
# and the debug profile also writes to the player's Fallout4Custom.ini. Shipping
# it on would do both to every stranger who installs this.
#
# Rewriting the STAGED copy, never the repo's, so a developer's working tree
# keeps its own default and a release cannot inherit it.
$debugJson = Join-Path $stage 'F4SE\Plugins\Rapport\debug.json'
if (Test-Path $debugJson) {
    $cfg = Get-Content $debugJson -Raw | ConvertFrom-Json
    if ($cfg.active -ne 'off') {
        Write-Host "  debug.json: active '$($cfg.active)' -> 'off' for release"
        $cfg.active = 'off'
        # NO BOM. Set-Content -Encoding UTF8 writes one on Windows PowerShell 5.1,
        # and a byte-order mark at the front of a JSON file is not JSON to most
        # parsers -- including the one in this plugin. Caught by reading it back.
        $utf8NoBom = New-Object System.Text.UTF8Encoding $false
        [System.IO.File]::WriteAllText($debugJson, ($cfg | ConvertTo-Json -Depth 24), $utf8NoBom)
    }
    # READ IT BACK. A packaging step that reports a change it did not make is
    # the failure this whole project keeps finding.
    $bytes = [System.IO.File]::ReadAllBytes($debugJson)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw "debug.json was written with a BOM - the plugin cannot parse that."
    }
    $check = (Get-Content $debugJson -Raw | ConvertFrom-Json).active
    if ($check -ne 'off') { throw "debug.json still says '$check' - refusing to ship diagnostics on." }
    Write-Host "  debug.json: verified active=off"
} else {
    throw "MISSING: $debugJson -- cannot verify diagnostics are off."
}

# Compiled Papyrus. Without these the plugin has no bridge and does nothing at
# all, which is why it is Required rather than best-effort.
Copy-Into -From 'build\papyrus\Rapport' -To 'Scripts\Rapport' -Tree -Required | Out-Null

# The plugins. Rapport_Moisturizer.esp is optional BY DESIGN: the script inside
# it names Commonwealth Moisturizer types, and a script naming a type nobody has
# installed is a reference the VM cannot resolve.
Copy-Into -From 'build\esp\Rapport.esp' -To '.' -Required | Out-Null
Copy-Into -From 'build\esp\Rapport_Moisturizer.esp' -To '.' | Out-Null

# AAF settings files. AAF merges these by priority; nothing in the AAF install is
# modified and these can be deleted at any time.
Copy-Into -From 'data\AAF' -To 'AAF' -Tree -Required | Out-Null

# The overlay assets -- the only art Rapport ships, and all of it generated.
Copy-Into -From 'data\F4SE\Plugins\F4EE' -To 'F4SE\Plugins\F4EE' -Tree -Required | Out-Null
Copy-Into -From 'data\Materials' -To 'Materials' -Tree -Required | Out-Null
Copy-Into -From 'data\Textures' -To 'Textures' -Tree -Required | Out-Null

# Licence and readme travel with the files. Somebody who downloads a zip and
# never sees the repository should still know what they may do with it.
foreach ($doc in 'LICENSE', 'README.md') {
    $p = Join-Path $root $doc
    if (Test-Path $p) { Copy-Item $p $stage -Force }
}

$zip = Join-Path $OutDir "Rapport-$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal

# Print the manifest. A release nobody inspected is a release nobody verified,
# and the failure mode -- a missing pex, a stale dll -- is silent on install and
# only shows up as "the mod does nothing" in somebody else's game.
Write-Host ''
Write-Host "Contents:"
Get-ChildItem $stage -Recurse -File |
    ForEach-Object { $_.FullName.Substring($stage.Length + 1) } |
    Sort-Object |
    ForEach-Object { Write-Host "    $_" }

$item = Get-Item $zip
Write-Host ''
Write-Host ("  {0}" -f $item.FullName)
Write-Host ("  {0:N0} bytes, {1} file(s)" -f $item.Length, @(Get-ChildItem $stage -Recurse -File).Count)
Write-Host ''
Write-Host "Install: extract into Fallout 4's Data folder, or let a mod manager do it."
