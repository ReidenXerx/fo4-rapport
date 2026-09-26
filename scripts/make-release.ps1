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
    [string] $OutDir  = '',
    # Rebuilding a zip whose version already exists destroyed the only local copy
    # of what was published, and filled a zip named 0.1.1 with unreleased code.
    [switch] $Force
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

$existing = Join-Path $OutDir "Rapport-$version.zip"
if ((Test-Path $existing) -and -not $Force) {
    throw "Rapport-$version.zip already exists. Bump the version in CMakeLists.txt, or pass -Force to rebuild this exact version on purpose."
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
# The staged copies only: no .pex in a stranger's download names this machine's
# folders, user or computer (dev-vs-release check, 2026-09-26). The game never reads
# those header fields. The dll's paths are trimmed in CMakeLists.txt instead.
& python (Join-Path $PSScriptRoot 'strip-pex.py') (Join-Path $stage 'Scripts') 'Rapport'
if ($LASTEXITCODE -ne 0) { throw 'strip-pex.py failed - nothing packaged.' }

# The plugins. Rapport_Moisturizer.esp is optional BY DESIGN: the script inside
# it names Commonwealth Moisturizer types, and a script naming a type nobody has
# installed is a reference the VM cannot resolve.
Copy-Into -From 'build\esp\Rapport.esp' -To '.' -Required | Out-Null
Copy-Into -From 'build\esp\Rapport_Moisturizer.esp' -To '.' | Out-Null

# AAF settings files. AAF merges these by priority; nothing in the AAF install is
# modified and these can be deleted at any time.
Copy-Into -From 'data\AAF' -To 'AAF' -Tree -Required | Out-Null

# DEV SWITCHES IN THE INI FILES (release review, 2026-09-25): the committed Rapport_settings.ini
# had debug_to_papyrus_log = true and Rapport.ini had Verbose = 1 -- both documented "ship off",
# neither guarded. Set in the STAGED copies, then read back, like debug.json above.
function Set-IniValues([string] $path, [hashtable] $want) {
    if (-not (Test-Path $path)) { throw "MISSING: $path -- cannot verify its dev switches are off." }
    $text = [System.IO.File]::ReadAllText($path)
    foreach ($key in $want.Keys) {
        $pattern = '(?im)^(\s*' + [regex]::Escape($key) + '\s*=\s*)[^\r\n;]*'
        if ($text -notmatch $pattern) { throw "$path has no '$key' line - refusing to guess what it ships with." }
        $text = [regex]::Replace($text, $pattern, '${1}' + $want[$key])
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, $text, $utf8NoBom)
    $back = [System.IO.File]::ReadAllText($path)
    foreach ($key in $want.Keys) {
        $m = [regex]::Match($back, '(?im)^\s*' + [regex]::Escape($key) + '\s*=\s*([^\r\n;]*)')
        if ($m.Groups[1].Value.Trim() -ne $want[$key]) { throw "$path still has $key = $($m.Groups[1].Value.Trim())" }
    }
    Write-Host "  $(Split-Path $path -Leaf): verified $(($want.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', ')"
}
Set-IniValues (Join-Path $stage 'AAF\Rapport_settings.ini') @{ 'debug_to_papyrus_log' = 'false' }
Set-IniValues (Join-Path $stage 'F4SE\Plugins\Rapport.ini') @{ 'Verbose' = '0'; 'DevMailbox' = '0'; 'DevConsole' = '0'; 'PanicClear' = '0' }

# The overlay assets -- the only art Rapport ships, and all of it generated.
Copy-Into -From 'data\F4SE\Plugins\F4EE' -To 'F4SE\Plugins\F4EE' -Tree -Required | Out-Null
Copy-Into -From 'data\Materials' -To 'Materials' -Tree -Required | Out-Null
Copy-Into -From 'data\Textures' -To 'Textures' -Tree -Required | Out-Null
# The MCM menu (generated by scripts/build-mcm.py). MCM itself stays optional.
Copy-Into -From 'data\MCM' -To 'MCM' -Tree -Required | Out-Null

# THE VOICES. Rapport.esp carries every bark as a Topic, and without these files
# each one plays as a subtitle over silence - the one failure dev testing never
# shows, because the dev staging folder already has them. package-voice.py copies
# exactly the lines barks.json names, for every voice type, and exits non-zero
# when any line was never rendered or a copied file does not match its source.
# From a COMMITTED bank only: the renders live in their own private repository at
# voice\out, and a release built from renders nobody committed cannot be rebuilt.
$bank = Join-Path $root 'voice\out'
if (-not (Test-Path (Join-Path $bank '.git'))) { throw "voice\out is not the voice repository - clone ReidenXerx/fo4-rapport-voice there." }
$dirty = git -C $bank status --porcelain
if ($dirty) { throw "voice\out has uncommitted renders - commit and push them to fo4-rapport-voice first." }
# ...and PUSHED: an unpushed commit cannot be rebuilt from a clean clone either (release review).
$ahead = git -C $bank rev-list --count '@{u}..HEAD' 2>$null
if ($LASTEXITCODE -ne 0) { throw "voice\out has no upstream to compare with - push it to fo4-rapport-voice first." }
if ([int]$ahead -gt 0) { throw "voice\out is $ahead commit(s) ahead of fo4-rapport-voice - push them first." }

python (Join-Path $root 'scripts\package-voice.py') $stage
if ($LASTEXITCODE -ne 0) {
    throw "Voice packaging failed (exit $LASTEXITCODE) - see its report above. A release without its audio is not a release."
}
$fuz = @(Get-ChildItem -Recurse -Filter *.fuz (Join-Path $stage 'Sound')).Count
if ($fuz -eq 0) { throw 'No .fuz files in the package - the voice step did nothing.' }
Write-Host "  voices: $fuz file(s) under Sound\Voice"

# Licence and readme travel with the files. Somebody who downloads a zip and
# never sees the repository should still know what they may do with it.
foreach ($doc in 'LICENSE', 'README.md') {
    $p = Join-Path $root $doc
    if (Test-Path $p) { Copy-Item $p $stage -Force }
}

$zip = $existing
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
