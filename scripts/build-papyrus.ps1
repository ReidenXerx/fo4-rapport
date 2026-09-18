<#
.SYNOPSIS
  Compiles this project's Papyrus scripts.

.DESCRIPTION
  Compiles papyrus/ into build/papyrus/ against the reconstructed base sources.
  Those sources live outside the game folder deliberately: the compiler takes import
  paths, so Vortex never sees them and nothing gets deployed that should not be.

  NEVER compile into Data/Scripts. That directory is Vortex-deployed.

  ASCII only. Windows PowerShell 5.1 reads a BOM-less UTF-8 script as ANSI, and one
  non-ASCII character in a string is enough to turn this into a parse error that
  still exits 0.

  See docs/papyrus-toolchain.md, especially the part about default arguments: the
  decompiled base sources have none, so every argument must be passed explicitly.
#>
[CmdletBinding()]
param(
    [string] $Base     = 'D:\F4CustomMods\PapyrusBase\Source\Base',
    [string] $Compiler = 'D:\GOGGames\Fallout 4 GOTY\Papyrus Compiler\PapyrusCompiler.exe',
    [switch] $Quiet
)

$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$sources = Join-Path $root 'papyrus'
$out     = Join-Path $root 'build\papyrus'

if (-not (Test-Path $Compiler)) {
    throw "No Papyrus compiler at $Compiler."
}
if (-not (Test-Path (Join-Path $Base 'Institute_Papyrus_Flags.flg'))) {
    throw "No Institute_Papyrus_Flags.flg in $Base. Run tools/papyrus_setup.py first."
}
if (-not (Test-Path $sources)) {
    Write-Host "No papyrus/ directory yet - nothing to compile."
    exit 0
}

New-Item -ItemType Directory -Force -Path $out | Out-Null

$scripts = Get-ChildItem -Recurse -Filter *.psc $sources
if ($scripts.Count -eq 0) {
    Write-Host "No .psc files under $sources - nothing to compile."
    exit 0
}

Write-Host "Compiling $($scripts.Count) script(s) against $Base"

$failed = 0
foreach ($script in $scripts) {
    $output = & $Compiler $script.FullName -f="Institute_Papyrus_Flags.flg" -i="$Base;$sources" -o="$out" 2>&1
    if ($LASTEXITCODE -ne 0 -or ($output -match 'compilation failed')) {
        $failed++
        Write-Host "FAILED: $($script.Name)"
        $output | Where-Object { $_ -match 'error|failed|not specified' } | ForEach-Object {
            Write-Host "    $_"
        }
    } elseif (-not $Quiet) {
        Write-Host "  ok: $($script.Name)"
    }
}

# Report what actually exists, not what the loop believes it wrote.
$built = Get-ChildItem -Recurse -Filter *.pex $out -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "$($built.Count) .pex in $out, $failed failed"
if ($failed -gt 0) { exit 1 }
