<#
.SYNOPSIS
    Installs (or removes) ForgedLCE in a Legacy Console Edition source tree.

.DESCRIPTION
    1. Copies loader/*.cpp, loader/*.h and include/forgedlce/* into
       <LceRoot>\Minecraft.World\.
    2. Generates the built-in mod files (FLCE_StaticMods.cpp and the
       FLCE_StaticMod00-15.cpp slots) from -StaticMods.
    3. Applies patches/*.patch (hooks and project entries) with `git apply`.

    Every patch is checked before anything is changed. Re-running is safe:
    loader files and built-in mods are refreshed and patches that are already
    applied are skipped. -Uninstall reverses everything.

    Built-in ("static") mods are compiled into the game, so they work on every
    platform. Consoles can only use built-in mods. Windows64 can additionally
    load DLL mods from <exe dir>\mods\ at runtime.

    Commit or back up your LCE tree first.

.PARAMETER StaticMods
    Mod folders to compile into the game. Each folder needs a forgedlce.json:
        { "modId": "examplemod", "source": "ExampleMod.cpp" }
    `source` is the one .cpp to compile (use a unity .cpp that includes the
    rest for multi-file mods). Always pass the full list: mods left out are
    removed from the build.

.EXAMPLE
    .\scripts\apply.ps1 -LceRoot D:\MCLCESRC -StaticMods .\examples\ExampleMod
.EXAMPLE
    .\scripts\apply.ps1 -LceRoot D:\MCLCESRC -Uninstall
#>
param(
    [Parameter(Mandatory = $true)][string]$LceRoot,
    [string[]]$StaticMods = @(),
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$SlotCount = 16 # must match the FLCE_StaticModNN.cpp entries in patches/0001

if (-not (Test-Path (Join-Path $LceRoot 'MinecraftConsoles.sln'))) {
    throw "$LceRoot does not look like the LCE source (MinecraftConsoles.sln not found)."
}
$LceRoot = (Resolve-Path $LceRoot).Path
$world = Join-Path $LceRoot 'Minecraft.World'
$modsOut = Join-Path $world 'FLCE_Mods'

# git: PATH first, then GitHub Desktop's bundled copy.
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $git) {
    $git = Get-ChildItem "$env:LOCALAPPDATA\GitHubDesktop\app-*\resources\app\git\cmd\git.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $git) { throw 'git was not found. Install Git for Windows or GitHub Desktop.' }

$patches = @(Get-ChildItem (Join-Path $repo 'patches') -Filter '*.patch' | Sort-Object Name)
$headerFiles = @(Get-ChildItem (Join-Path $repo 'include\forgedlce') -File)
$loaderFiles = @(Get-ChildItem (Join-Path $repo 'loader') -File | Where-Object { $_.Extension -in '.cpp', '.h' }) + $headerFiles
$generated = @('FLCE_StaticMods.cpp') + (0..($SlotCount - 1) | ForEach-Object { 'FLCE_StaticMod{0:D2}.cpp' -f $_ })

function Invoke-GitApply([string[]]$extra, [string]$patch) {
    # --check runs are probes ("is this already applied?"); hide their errors.
    $quiet = $extra -contains '--check'
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    # --ignore-whitespace: the patches carry CRLF, but a git checkout of the
    # LCE source may have LF line endings (e.g. on CI).
    try {
        if ($quiet) { & $git -C $LceRoot apply --whitespace=nowarn --ignore-whitespace @extra $patch 2>$null | Out-Null }
        else        { & $git -C $LceRoot apply --whitespace=nowarn --ignore-whitespace @extra $patch }
    } finally {
        $ErrorActionPreference = $eap
    }
    return $LASTEXITCODE -eq 0
}

function Write-Source([string]$name, [string[]]$lines) {
    $text = ($lines -join "`r`n") + "`r`n"
    [IO.File]::WriteAllText((Join-Path $world $name), $text, (New-Object Text.UTF8Encoding($false)))
}

# ---------------------------------------------------------------- uninstall
if ($Uninstall) {
    foreach ($p in $patches) {
        if (-not (Invoke-GitApply @('-R', '--check') $p.FullName)) {
            throw "$($p.Name) cannot be reversed (not applied, or the file changed since). Nothing was changed."
        }
    }
    [array]::Reverse($patches)
    foreach ($p in $patches) {
        if (-not (Invoke-GitApply @('-R') $p.FullName)) { throw "Reversing $($p.Name) failed." }
        Write-Host "reversed $($p.Name)"
    }
    foreach ($name in @($loaderFiles | ForEach-Object { $_.Name }) + $generated) {
        $dst = Join-Path $world $name
        if (Test-Path $dst) { Remove-Item $dst; Write-Host "removed  Minecraft.World\$name" }
    }
    if (Test-Path $modsOut) { Remove-Item $modsOut -Recurse -Force; Write-Host 'removed  Minecraft.World\FLCE_Mods' }
    Write-Host 'ForgedLCE removed.'
    return
}

# ------------------------------------------------------------ validate input
$mods = @()
foreach ($dir in $StaticMods) {
    $dir = (Resolve-Path $dir).Path
    $manifestPath = Join-Path $dir 'forgedlce.json'
    if (-not (Test-Path $manifestPath)) { throw "$dir has no forgedlce.json." }
    $m = Get-Content $manifestPath -Raw | ConvertFrom-Json
    if ($m.modId -notmatch '^[a-z0-9_]+$') { throw "$manifestPath`: modId must be lowercase a-z, 0-9, _." }
    if (-not $m.source -or -not (Test-Path (Join-Path $dir $m.source))) { throw "$manifestPath`: source file '$($m.source)' not found." }
    if ($mods | Where-Object { $_.modId -eq $m.modId }) { throw "Duplicate modId $($m.modId)." }
    $mods += [pscustomobject]@{ modId = $m.modId; source = ($m.source -replace '\\', '/'); dir = $dir }
}
if ($mods.Count -gt $SlotCount) {
    throw "At most $SlotCount built-in mods are supported (add more FLCE_StaticModNN.cpp slots to patches/0001 and `$SlotCount)."
}

# Check every patch before changing anything.
$alreadyApplied = $true
foreach ($p in $patches) {
    if (Invoke-GitApply @('--check') $p.FullName) { $alreadyApplied = $false; continue }
    if (-not (Invoke-GitApply @('-R', '--check') $p.FullName)) {
        throw "$($p.Name) does not apply to $LceRoot. Nothing was changed."
    }
}

# -------------------------------------------------------------- loader files
foreach ($f in $loaderFiles) {
    Copy-Item $f.FullName (Join-Path $world $f.Name) -Force
    Write-Host "copied   Minecraft.World\$($f.Name)"
}

# ------------------------------------------------------------ built-in mods
if (Test-Path $modsOut) { Remove-Item $modsOut -Recurse -Force }
foreach ($m in $mods) {
    $dst = Join-Path $modsOut $m.modId
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item (Join-Path $m.dir '*') $dst -Recurse -Force
    # Mods include "forgedlce/ForgedLCE.hpp"; give each mod its own copy next
    # to it, because GCC-style console compilers only search the including
    # file's folder for quoted includes.
    $inc = Join-Path $dst 'forgedlce'
    New-Item -ItemType Directory -Force $inc | Out-Null
    foreach ($h in $headerFiles) { Copy-Item $h.FullName $inc -Force }
    Write-Host "built-in mod $($m.modId)"
}

$header = '// Generated by ForgedLCE scripts/apply.ps1. Do not edit; re-run the script.'
for ($i = 0; $i -lt $SlotCount; $i++) {
    $lines = @($header, '#include "stdafx.h"')
    if ($i -lt $mods.Count) {
        $m = $mods[$i]
        $lines += "#define FLCE_STATIC_MOD_ID $($m.modId)"
        $lines += "#include `"FLCE_Mods/$($m.modId)/$($m.source)`""
    } else {
        $lines += '// (empty slot)'
    }
    Write-Source ('FLCE_StaticMod{0:D2}.cpp' -f $i) $lines
}

$table = @($header, '#include "stdafx.h"', '#include "FLCE_Internal.h"', '')
foreach ($m in $mods) {
    $table += "extern `"C`" int FLCE_CALL flce_mod_entry_$($m.modId)(unsigned int apiVersion, FLCE_ModInfo *info);"
}
$table += @('', 'namespace ForgedLCE {', '', 'const StaticMod g_staticMods[] = {')
foreach ($m in $mods) { $table += "    { L`"$($m.modId)`", flce_mod_entry_$($m.modId) }," }
$table += @('    { NULL, NULL }', '};', '', '} // namespace ForgedLCE')
Write-Source 'FLCE_StaticMods.cpp' $table
Write-Host "generated FLCE_StaticMods.cpp and $SlotCount slot files ($($mods.Count) built-in mod(s))"

# ------------------------------------------------------------------ patches
if ($alreadyApplied) {
    Write-Host 'Patches were already applied; loader files and built-in mods refreshed.'
    return
}
foreach ($p in $patches) {
    if (Invoke-GitApply @('--check') $p.FullName) {
        if (-not (Invoke-GitApply @() $p.FullName)) { throw "Applying $($p.Name) failed." }
        Write-Host "applied  $($p.Name)"
    } else {
        Write-Host "skipped  $($p.Name) (already applied)"
    }
}
Write-Host 'ForgedLCE installed. Build any platform configuration of MinecraftConsoles.sln.'
