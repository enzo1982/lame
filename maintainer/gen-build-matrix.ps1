<#
.SYNOPSIS
  Generate a native-Windows build-configuration test matrix for LAME.

.DESCRIPTION
  Creates, under a master directory, one build directory per configuration for
  the two native Windows builds:

    * nmake  (Makefile.MSVC)  - built out-of-tree in the cell directory using
      the Makefile's srcdir= support; 32-bit (Makefile.MSVC's native target).
    * MSBuild (vc_solution)   - the solution built per Configuration x Platform;
      MSBuild isolates its output by Configuration|Platform automatically.

  Each cell gets a build.cmd recording its exact command. A build-all.ps1
  driver builds every cell, never stops on failure, tees each log to logs\,
  prints a summary of the failed builds with their log paths, and exits
  non-zero if any failed.

  Visual Studio is located without an exhaustive disk crawl: -VsPath, else
  vswhere.exe, else an already-initialized developer environment.

  libsndfile is intentionally not part of the Windows matrix. The decoder-on
  cells are generated only when -Mpg123Dir is supplied.

  See doc\maintainer-build-matrix.md for prerequisites and the POSIX
  counterpart (gen-build-matrix.sh).

.PARAMETER Dir
  Master directory to create (default .\build-matrix).
.PARAMETER SrcDir
  LAME source directory (default: the parent of this script's directory).
.PARAMETER VsPath
  Visual Studio installation to use (overrides autodetection).
.PARAMETER Mpg123Dir
  Folder with mpg123.h and libmpg123-0.lib (ending backslash optional);
  enables the decoder-on cells.
.PARAMETER Config
  MSBuild configurations, comma-separated (default "Release,Debug").
.PARAMETER Arch
  MSBuild platforms, comma-separated (default "x64"; x86 maps to Win32).
#>
[CmdletBinding()]
param(
	[string]$Dir       = ".\build-matrix",
	[string]$SrcDir,
	[string]$VsPath,
	[string]$Mpg123Dir,
	[string]$Config    = "Release,Debug",
	[string]$Arch      = "x64"
)

$ErrorActionPreference = "Stop"

function Fail($msg) { Write-Error $msg; exit 1 }

# --- source directory -------------------------------------------------------

if (-not $SrcDir) { $SrcDir = Split-Path -Parent $PSScriptRoot }
$SrcDir = (Resolve-Path $SrcDir).Path
if (-not (Test-Path (Join-Path $SrcDir "Makefile.MSVC"))) {
	Fail "Makefile.MSVC not found under '$SrcDir' - pass -SrcDir."
}

# --- locate Visual Studio ---------------------------------------------------

function Find-VsInstall {
	if ($VsPath) {
		if (Test-Path (Join-Path $VsPath "VC\Auxiliary\Build\vcvarsall.bat")) { return $VsPath }
		Fail "-VsPath '$VsPath' has no VC\Auxiliary\Build\vcvarsall.bat."
	}
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vswhere) {
		$p = & $vswhere -latest -products * `
			-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
			-property installationPath 2>$null | Select-Object -First 1
		if ($p) { return $p }
	}
	if ($env:VCINSTALLDIR) {
		# VCINSTALLDIR is <install>\VC\ ; go up two levels
		return (Split-Path -Parent (Split-Path -Parent $env:VCINSTALLDIR.TrimEnd('\')))
	}
	Fail @"
Could not locate Visual Studio. Provide one of:
  -VsPath <install path>                 (e.g. E:\VisualStudio\2026\BuildTools)
  a machine with vswhere.exe installed   (standard with VS 2017+)
  a Developer Command Prompt (VCINSTALLDIR set)
"@
}

$vs       = Find-VsInstall
$vcvars   = Join-Path $vs "VC\Auxiliary\Build\vcvarsall.bat"
$msbuild  = Join-Path $vs "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
	$msbuild = Get-ChildItem -Path $vs -Recurse -Filter MSBuild.exe -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match "\\Bin\\" } | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $msbuild) { Fail "MSBuild.exe not found under '$vs'." }

# main solution (rename-robust: the vc_solution .sln that is not the clients one)
$sln = Get-ChildItem (Join-Path $SrcDir "vc_solution\*.sln") -ErrorAction SilentlyContinue |
	Where-Object { $_.Name -notmatch 'client' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $sln) { Fail "No main solution found under vc_solution\." }

# Probe the decoder prerequisites, so that only locally buildable cells are
# emitted. The MSBuild cell generates the import library from the shipped .def
# on its own; the nmake build expects it to exist already.
$mpg123 = ""
$mpg123HasLib = $false
if ($Mpg123Dir) {
	$mpg123 = (Resolve-Path $Mpg123Dir).Path
	if (-not (Test-Path (Join-Path $mpg123 "mpg123.h"))) {
		Write-Warning "no mpg123.h in '$mpg123' - decoder-on cells will be skipped."
		$mpg123 = ""
	} else {
		$mpg123HasLib = Test-Path (Join-Path $mpg123 "libmpg123-0.lib")
		if (-not $mpg123HasLib) {
			Write-Warning "no libmpg123-0.lib in '$mpg123' - the nmake decoder cell will be"
			Write-Warning "skipped. Generate it once with: lib /def:libmpg123-0.def /machine:x86"
		}
	}
}

# --- master directory -------------------------------------------------------

if (-not (Test-Path $Dir)) { New-Item -ItemType Directory -Force $Dir | Out-Null }
$master = (Resolve-Path $Dir).Path
New-Item -ItemType Directory -Force (Join-Path $master "logs") | Out-Null

$info = Join-Path $master "matrix-info.txt"
@(
	"# LAME native-Windows build-configuration test matrix"
	"# generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') by gen-build-matrix.ps1"
	"srcdir   = $SrcDir"
	"vs       = $vs"
	"msbuild  = $msbuild"
	"solution = $sln"
	"mpg123   = $(if ($mpg123) { $mpg123 } else { '(none - decoder-on cells skipped)' })"
	"config   = $Config"
	"arch     = $Arch"
	"cells:"
) | Set-Content -Encoding ASCII $info

$cells = @()   # each: @{ Name; Cmd }

# --- nmake cells (out-of-tree, 32-bit) --------------------------------------
#
# Makefile.MSVC links with /machine:I386, so the nmake build is always 32-bit
# and -Arch does not apply to it; -Arch selects the platform for the MSBuild
# cells only. A 64-bit nmake build would need Makefile.MSVC to learn a target
# platform of its own.

$nmakeCommon = "call `"$vcvars`" x86`r`ncd /d `"%~dp0`"`r`nnmake /nologo /f `"$SrcDir\Makefile.MSVC`" srcdir=`"$SrcDir`" ASM=NO SNDFILE=NO"

$cells += @{ Name = "nmake-default"; Cmd = "$nmakeCommon all" }
if ($mpg123 -and $mpg123HasLib) {
	$cells += @{ Name = "nmake-mpg123"; Cmd = "$nmakeCommon MPG123=YES MPG123_DIR=`"$mpg123`" all" }
}

# --- MSBuild cells (Configuration x Platform) -------------------------------

function To-Platform($a) {
	if ($a -ieq "x86") {
		return "Win32"
	}
	return $a
}

# @() keeps a one-element result an array: a bare pipeline yielding a single
# value returns that value, and indexing a string gives its first character.
$configs = @($Config.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$archs   = @($Arch.Split(",")   | ForEach-Object { $_.Trim() } | Where-Object { $_ })

foreach ($c in $configs) {
	foreach ($a in $archs) {
		$plat = To-Platform $a
		$cells += @{ Name = "msbuild-$c-$a"
			Cmd = "cd /d `"%~dp0`"`r`n`"$msbuild`" `"$sln`" /nologo /m /t:Rebuild /p:Configuration=$c /p:Platform=$plat /p:OutDirBase=%~dp0bin\ /p:IntDirBase=%~dp0obj\" }
	}
}
# one decoder-on MSBuild flip on the base config/arch, if mpg123 is available
if ($mpg123) {
	$c0 = $configs[0]; $a0 = $archs[0]; $p0 = To-Platform $a0
	$cells += @{ Name = "msbuild-$c0-$a0-mpg123"
		Cmd = "cd /d `"%~dp0`"`r`n`"$msbuild`" `"$sln`" /nologo /m /t:Rebuild /p:Configuration=$c0 /p:Platform=$p0 /p:HaveMpg123=true /p:Mpg123Path=`"$mpg123`" /p:OutDirBase=%~dp0bin\ /p:IntDirBase=%~dp0obj\" }
}

# --- emit cell build.cmd files ----------------------------------------------

foreach ($cell in $cells) {
	$cdir = Join-Path $master $cell.Name
	New-Item -ItemType Directory -Force $cdir | Out-Null
	$bc = Join-Path $cdir "build.cmd"
	@(
		"@echo off"
		"rem Auto-generated by gen-build-matrix.ps1 - DO NOT EDIT BY HAND."
		"rem cell: $($cell.Name)"
		$cell.Cmd
	) -join "`r`n" | Set-Content -Encoding ASCII $bc
	Add-Content -Encoding ASCII $info "  [gen]  $($cell.Name)"
}

# --- driver -----------------------------------------------------------------

$driver = Join-Path $master "build-all.ps1"
@'
# build-all.ps1 - build every cell of the native-Windows build matrix.
# Auto-generated by gen-build-matrix.ps1 - DO NOT EDIT BY HAND.
# Never stops on failure; tees each log to logs\<cell>.log; prints a summary
# of the failed builds and exits non-zero if any failed.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
New-Item -ItemType Directory -Force (Join-Path $here "logs") | Out-Null
$failed = @()
$total  = 0
Get-ChildItem -Directory $here | Where-Object { Test-Path (Join-Path $_.FullName "build.cmd") } | ForEach-Object {
	$name = $_.Name
	$log  = Join-Path $here "logs\$name.log"
	$total++
	Write-Host "`n==== building $name ===="
	& cmd /c "`"$($_.FullName)\build.cmd`"" 2>&1 | Tee-Object -FilePath $log
	if ($LASTEXITCODE -eq 0) {
		Write-Host "     PASS  $name"
	} else {
		Write-Host "     FAIL  $name  (exit $LASTEXITCODE)"
		$failed += [pscustomobject]@{ Cell = $name; Log = $log }
	}
}
Write-Host "`n==================== build-matrix summary ===================="
Write-Host "total builds: $total"
if ($failed.Count -gt 0) {
	Write-Host "FAILED builds:"
	$failed | ForEach-Object { Write-Host ("  {0,-28} log: {1}" -f $_.Cell, $_.Log) }
	Write-Host "============================================================="
	exit 1
}
Write-Host "all builds passed"
Write-Host "============================================================="
exit 0
'@ | Set-Content -Encoding ASCII $driver

# --- report -----------------------------------------------------------------

Write-Host "Generated $($cells.Count) build cell(s) under: $master"
Write-Host "Driver: $driver"
Write-Host "Matrix info: $info"
if (-not $mpg123) {
	Write-Host ""
	Write-Host "NOTE: -Mpg123Dir not given - decoder-on cells were skipped."
}
Write-Host ""
Write-Host "To run the matrix:"
Write-Host "  pwsh $driver"
