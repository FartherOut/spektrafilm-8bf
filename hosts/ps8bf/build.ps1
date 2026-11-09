# SPDX-License-Identifier: GPL-3.0-or-later
# build.ps1 — build the shipping spektrafilm .8bf with MSVC (x64), then stage
# dist/spektrafilm.8bf.
#
# Prereqs: Visual Studio 2022 or 2026 (any edition - Community is fine) with
# the "Desktop development with C++" workload, CMake (4.2+ for the
# "Visual Studio 18 2026" generator), and Python 3 on PATH. Run from a normal
# PowerShell; this script locates vcvars64.bat itself, so a "Native Tools"
# prompt is not required.
#
#   .\build.ps1                     # Release, newest installed VS (2026 > 2022)
#   .\build.ps1 -Generator "Visual Studio 17 2022"   # pin VS 2022
#   .\build.ps1 -Generator Ninja -Configuration Debug
#
# Produces: hosts\ps8bf\dist\spektrafilm.8bf  (imports kernel32/user32/gdi32,
# statically linked CRT, PiPL + RCDATA pack embedded).
#
# Output policy: the standard report of every tool, in full, on the console.
# Nothing is swallowed (no -Quiet, no captured stderr, no /nologo-style
# summarising) and nothing is suppressed at the compiler (no /wd<code>, see
# CMakeLists.txt), so what you see is what the toolchain actually reported. The
# tools are invoked directly rather than piped through PowerShell: piping makes
# MSBuild detect a non-console and drop its colouring, and it turns native stderr
# into PowerShell error records. Instead the MSVC build is asked to write its own
# complete log (build\msbuild.log) via MSBuild's file logger, and the run ends
# with a report of what went in and what came out.
[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    # "" = auto-detect the newest installed Visual Studio with the C++ tools
    # (VS 2026 -> "Visual Studio 18 2026", VS 2022 -> "Visual Studio 17 2022").
    [string]$Generator     = "",
    [string]$BuildDir      = "build",
    [string]$DistDir       = "dist"
)

$ErrorActionPreference = "Stop"
# PowerShell 7.3+ can turn a native tool's non-zero exit into a terminating
# error. This script judges every native call by $LASTEXITCODE and throws with
# its own message, so that behaviour is off where it exists (it does not in
# Windows PowerShell 5.1).
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

function Find-VsInstall {
    # Newest VS instance with the x64 C++ tools, any edition (Community/Pro/
    # Enterprise/Build Tools all satisfy the component id; -products * covers
    # them all). Returns @{ Path; Version } or $null.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $vs = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vs) { return $null }
    $ver = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationVersion
    return [pscustomobject]@{ Path = $vs; Version = "$ver" }
}

function Get-VsGenerator([string]$version) {
    # Map the VS install version (vswhere installationVersion) to the matching
    # CMake generator. "Visual Studio 18 2026" needs CMake >= 4.2.
    $major = 0
    if ($version -match '^(\d+)') { $major = [int]$matches[1] }
    if ($major -ge 18) { return "Visual Studio 18 2026" }
    if ($major -eq 17) { return "Visual Studio 17 2022" }
    return $null
}

# --- toolchain environment --------------------------------------------------
$vsInstall = Find-VsInstall

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not $vsInstall) {
        throw "MSVC not found. Install Visual Studio 2022/2026 (Community is fine) with the 'Desktop development with C++' workload, or run from a Native Tools prompt."
    }
    $vcvars = Join-Path $vsInstall.Path "VC\Auxiliary\Build\vcvars64.bat"
    Write-Host "Importing $vcvars" -ForegroundColor Cyan
    cmd /c "`"$vcvars`" && set" | ForEach-Object {
        if ($_ -match "^(.*?)=(.*)$") { Set-Item -Force -Path "env:$($matches[1])" -Value $matches[2] }
    }
}

if (-not $Generator) {
    if ($vsInstall) { $Generator = Get-VsGenerator $vsInstall.Version }
    if (-not $Generator) {
        throw "No supported Visual Studio (2022/2026) found for generator auto-detection. Pass -Generator explicitly (e.g. `"Visual Studio 18 2026`", `"Visual Studio 17 2022`", or `"Ninja`")."
    }
    Write-Host "Auto-detected generator: $Generator (VS $($vsInstall.Version))" -ForegroundColor Cyan
}

if (-not (Get-Command python -ErrorAction SilentlyContinue) -and
    -not (Get-Command python3 -ErrorAction SilentlyContinue)) {
    throw "Python 3 not found on PATH (needed to generate the embedded pack)."
}

# --- configure + build ------------------------------------------------------
$repo = (Resolve-Path (Join-Path $here "..\..")).Path
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$build = (Resolve-Path $BuildDir).Path

# The embedded pack is derived, never hand-edited: the pinned darktable-
# spektrafilm submodule pack plus the archived Hanatos2025 bandpass from the OFX
# tree. sync_pack.py is the only writer of spektra-core\data\pack, and it fails
# the build when an upstream tree is missing instead of falling back to the
# pristine pack - that fallback built fine and silently changed how the 20 film
# profiles that ship a bandpass render. It runs without --quiet, so the console
# says which pack was derived and what was merged into it.
$py = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { "python3" }
& $py (Join-Path $repo "tools\sync_pack.py")
if ($LASTEXITCODE -ne 0) { throw "pack sync failed (tools\sync_pack.py)" }

$genArgs = @("-S", ".", "-B", $BuildDir, "-G", $Generator,
             "-DCMAKE_BUILD_TYPE=$Configuration")
$msbuildLog = $null
if ($Generator -like "Visual Studio*") {
    # multi-config generators ignore CMAKE_BUILD_TYPE
    $genArgs = @("-S", ".", "-B", $BuildDir, "-G", $Generator, "-A", "x64")
    # MSBuild writes this itself, so the console stays MSBuild's own (colour and
    # progress included) instead of a re-rendered pipe. "normal" verbosity: every
    # diagnostic, plus the command lines of each tool invocation.
    $msbuildLog = Join-Path $build "msbuild.log"
}

Write-Host "cmake $($genArgs -join ' ')" -ForegroundColor Cyan
& cmake @genArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

$buildArgs = @("--build", $BuildDir, "--config", $Configuration)
if ($msbuildLog) {
    $buildArgs += @("--", "/flp:logfile=$msbuildLog;verbosity=normal")
}
Write-Host "cmake $($buildArgs -join ' ')" -ForegroundColor Cyan
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) {
    $where = if ($msbuildLog) { " (full log: $msbuildLog)" } else { "" }
    throw "build failed$where"
}

# --- stage ------------------------------------------------------------------
# Deterministic path: the CMake target/output name is fixed, so the artifact is
# at one known location per generator. Do NOT glob recursively - a stale
# build-mingw.sh output under build\ would otherwise be staged by mistake
# (that was the earlier bug: the mingw DLL, importing api-ms-win-crt-*, got
# copied into dist\ and loaded instead of the MSVC build).
if ($Generator -like "Visual Studio*") {
    $dll = Join-Path $BuildDir "$Configuration\spektrafilm.8bf"
} else {
    $dll = Join-Path $BuildDir "spektrafilm.8bf"
}
if (-not (Test-Path -LiteralPath $dll)) {
    throw "spektrafilm.8bf not found at expected path: $dll (generator '$Generator')"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$distFull = (Resolve-Path $DistDir).Path
$out = Join-Path $DistDir "spektrafilm.8bf"
Copy-Item -LiteralPath $dll -Destination $out -Force

# The shipping build must be CRT-static (/MT) and import only the standard Win32
# DLLs (kernel32/user32/gdi32). Anything mentioning the UCRT or the VC++ runtime
# means the wrong DLL was staged (a mingw validation build, or /MD), so fail
# loudly.
$deps = & dumpbin /dependents $out
$bad = $deps | Select-String -Pattern "api-ms-win-crt|ucrtbase|vcruntime|msvcp|msvcr|libstdc\+\+|libgcc|libwinpthread" -CaseSensitive:$false
if ($bad) {
    throw "staged DLL is not CRT-static (unexpected runtime imports):`n$($bad -join "`n")"
}

# Decision D8: the GPU stack (d3d11/dxgi/d3dcompiler_47) is LoadLibrary()'d at
# runtime, so it must NOT appear as a static import. A stray import means the
# dynamic-load policy was bypassed somewhere.
$gpu = $deps | Select-String -Pattern "d3d11|dxgi|d3dcompiler" -CaseSensitive:$false
if ($gpu) {
    throw "static GPU import detected (D8 violation; expected dynamic loading):`n$($gpu -join "`n")"
}

# Positive allow-list: nothing outside the expected Win32 DLLs may be imported.
$expected = @("KERNEL32.dll","USER32.dll","GDI32.dll")
$imports = @($deps | Select-String -Pattern "\.dll" | ForEach-Object { $_.Line.Trim() })
$unexpected = @($imports | Where-Object { $expected -notcontains $_ })
if ($unexpected.Count -gt 0) {
    throw "unexpected import(s) in the shipping .8bf (expected only $($expected -join '/')):`n$($unexpected -join "`n")"
}

# --- notices ----------------------------------------------------------------
# Two of the licences in play ask that notices travel with the copy: GPLv3
# section 5 (keep notices intact, ship the licence text) and the data pack's
# CC BY-SA 4.0 attribution clause, which names "an app's About screen" as one of
# the places the credit belongs. The About dialog carries the short form; these
# two files are the long form, so the folder someone installs from documents
# itself. Missing here means the artifact was built from a tree that lost them.
$notices = @()
foreach ($doc in @("LICENSE", "THIRD-PARTY-NOTICES.md")) {
    $src = Join-Path $repo $doc
    if (-not (Test-Path -LiteralPath $src)) {
        throw "required notice file missing from the repository root: $src"
    }
    Copy-Item -LiteralPath $src -Destination (Join-Path $distFull $doc) -Force
    $notices += $doc
}

# --- report -----------------------------------------------------------------
# What the build consumed and what it produced, so a run can be compared against
# the last one without reading the whole log. The diagnostic counts are scraped
# from the MSBuild log rather than trusted from its summary line, and broken out
# per code so a new class of warning cannot hide among old ones.
#
# sha256 names *this file*, not this run: the linker stamps a TimeDateStamp into
# the PE header, so two builds of identical sources hash differently. Compare the
# counts and sizes between runs, the hash between two copies of one artifact.
$warnLine = "(not counted: no MSBuild log for generator '$Generator' - read the console above)"
$errCount = "(not counted)"
if ($msbuildLog -and (Test-Path -LiteralPath $msbuildLog)) {
    # MSBuild's file log repeats every diagnostic in its end-of-build summary,
    # so count only the live event stream and stop at the "Build succeeded."
    # banner. Deduplicating instead would also collapse genuinely repeated
    # diagnostics -- the identical C4068s the vendored grain.h earns in every
    # translation unit that includes it (five today) read alike once the "N>"
    # event prefix is stripped, and would count as one.
    $lines = @(Get-Content -LiteralPath $msbuildLog)
    $cut = @($lines | Select-String -Pattern '^Build (succeeded|failed|stopped)' |
        Select-Object -First 1)
    if ($cut.Count -gt 0) {
        $n = $cut[0].LineNumber - 1 # lines before the banner
        $lines = if ($n -gt 0) { @($lines[0..($n - 1)]) } else { @() }
    }
    $diag = @($lines |
        Select-String -Pattern "(warning|error) [A-Z]{1,3}\d{3,4}" |
        ForEach-Object { ($_.Line -replace '^\s*(?:\d+(?::\d+)*(?:\.\d+)*)?>\s*', '').Trim() })
    $warns = @($diag | Where-Object { $_ -match "warning [A-Z]{1,3}\d{3,4}" })
    $errs  = @($diag | Where-Object { $_ -match "error [A-Z]{1,3}\d{3,4}" })
    $errCount = $errs.Count
    $warnLine = "$($warns.Count)"
    if ($warns.Count -gt 0) {
        $codes = $warns | ForEach-Object { [regex]::Match($_, "warning [A-Z]{1,3}\d{3,4}").Value }
        $warnLine += "  (" + (($codes | Group-Object | Sort-Object Count -Descending |
            ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", ") + ")"
    }
}

# Provenance of the embedded pack, from the stamp sync_pack.py wrote.
$stampPath = Join-Path $repo "spektra-core\data\pack.stamp"
$packLine = "(no pack.stamp found)"
if (Test-Path -LiteralPath $stampPath) {
    $s = Get-Content -Raw -LiteralPath $stampPath | ConvertFrom-Json
    $packLine = "$($s.src_files) files, $($s.band_profiles) bandpass profiles, " +
                "src pack $($s.src.Substring(0,12)), bands $($s.bands.Substring(0,12))"
}

$clVersion = if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
    (Get-Item (Get-Command cl.exe).Source).VersionInfo.FileVersion
} else { "(cl.exe not on PATH)" }
$cmakeVersion = (& cmake --version | Select-Object -First 1)
$pyVersion    = & $py -c "import sys; print('.'.join(map(str, sys.version_info[:3])))"
$artifact     = Get-Item -LiteralPath $out

Write-Host ""
Write-Host "== build report ==" -ForegroundColor Green
Write-Host ("  {0,-12} {1}" -f "generator", "$Generator$(if ($vsInstall) { " (VS $($vsInstall.Version))" })")
Write-Host ("  {0,-12} {1}" -f "config",    "$Configuration -> $build")
Write-Host ("  {0,-12} {1}" -f "cmake",     "$cmakeVersion")
Write-Host ("  {0,-12} {1}" -f "compiler",  "cl.exe $clVersion")
Write-Host ("  {0,-12} {1}" -f "python",    "$pyVersion ($py)")
Write-Host ("  {0,-12} {1}" -f "pack",      $packLine)
$warnColor = if ($warnLine -like "0*") { "Green" } else { "Yellow" }
Write-Host ("  {0,-12} {1}" -f "warnings",  $warnLine) -ForegroundColor $warnColor
Write-Host ("  {0,-12} {1}" -f "errors",    $errCount)
Write-Host ("  {0,-12} {1} ({2:N1} MiB)" -f "artifact", $artifact.FullName, `
            ($artifact.Length / 1MB))
Write-Host ("  {0,-12} {1}" -f "sha256 file", (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash.ToLower())
Write-Host ("  {0,-12} {1}" -f "imports",   ($imports -join " "))
Write-Host ("  {0,-12} {1} ({2})" -f "notices", ($notices -join " "), $distFull)
if ($msbuildLog) { Write-Host ("  {0,-12} {1}" -f "log", $msbuildLog) }
