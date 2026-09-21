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
    # F4SE's own script sources (it ships the vanilla scripts it EXTENDS, whole).
    # First on the import path, so ObjectReference.GetVoiceType and friends resolve.
    # Rapport requires F4SE at runtime anyway; compiling against its surface is honest.
    [string] $F4SE     = 'D:\GOGGames\Fallout 4 GOTY\Data\Scripts\Source',
    [string] $Compiler = 'D:\GOGGames\Fallout 4 GOTY\Papyrus Compiler\PapyrusCompiler.exe',
    [switch] $Quiet
)

$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$sources = Join-Path $root 'papyrus'
$out     = Join-Path $root 'build\papyrus'
# Import-only declarations the reconstructed base lacks (Topic, TopicInfo,
# VoiceType). An import path, never a source: compiled, they would ship .pex
# files that shadow the game's own types.
$stubs   = Join-Path $root 'papyrus-stubs'

if (-not (Test-Path $Compiler)) {
    throw "No Papyrus compiler at $Compiler."
}
if (-not (Test-Path (Join-Path $F4SE 'F4SE.psc'))) {
    throw "No F4SE.psc in $F4SE. Point -F4SE at F4SE's Data\Scripts\Source (it ships with F4SE)."
}
# ONLY ObjectReference.psc from F4SE, copied fresh into build/ every run (never
# committed, never shipped). Taking F4SE's whole folder also took its ScriptObject,
# whose RegisterForCustomEvent rejects the mangled AAF event names the bridge must
# register with - see Bridge.psc Connect.
$f4seImports = Join-Path $root 'build\f4se-imports'
New-Item -ItemType Directory -Force -Path $f4seImports | Out-Null
Copy-Item (Join-Path $F4SE 'ObjectReference.psc') $f4seImports -Force

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

# Batch mode, not file by file. A namespaced script (Rapport:Bridge) compiled by
# path fails with "filename does not match script name": the namespace has to come
# from the import paths, which -all does and a single file path cannot.
$output = & $Compiler $sources -all -f="Institute_Papyrus_Flags.flg" -i="$f4seImports;$Base;$sources;$stubs" -o="$out" 2>&1

# Print everything the compiler said. An earlier version filtered this to lines
# matching "error", which hid the only message that explained a failure.
$output | ForEach-Object { Write-Host "  $_" }

$built = Get-ChildItem -Recurse -Filter *.pex $out -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "$($built.Count) .pex in $out"
if ($output -match 'compilation failed' -or $output -match '0 succeeded') { exit 1 }
