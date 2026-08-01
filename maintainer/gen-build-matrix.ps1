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

  The optional libraries (mpg123, libsndfile, GTK 4) each add a cell when they
  are present. They are looked for under vc_solution (where
  setup-windows-deps.ps1 lays them out), or wherever a -<name>Dir override
  points; a missing one just drops its cell.

  See doc\maintainer-build-matrix.md for prerequisites and the POSIX
  counterpart (gen-build-matrix.sh).

.PARAMETER Dir
  Master directory to create (default .\build-matrix).
.PARAMETER SrcDir
  LAME source directory (default: the parent of this script's directory).
.PARAMETER VsPath
  Visual Studio installation to use (overrides autodetection).
.PARAMETER Mpg123Dir
  mpg123 folder with mpg123.h and libmpg123-0.def, overriding the default
  vc_solution\mpg123\Win32; enables the decoder-on cells.
.PARAMETER LibsndfileDir
  libsndfile folder with lib\sndfile.lib, overriding the default
  vc_solution\libsndfile\Win32; enables the libsndfile cells.
.PARAMETER GtkDir
  GTK 4 install prefix with lib\gtk-4.lib (a gvsbuild tree), overriding the
  default vc_solution\gtk4\x64; enables the mp3x analyzer cell.
.PARAMETER Config
  MSBuild configurations, comma-separated (default "Release,Debug").
.PARAMETER Arch
  MSBuild platforms, comma-separated (default "x64"; x86 maps to Win32).
#>
[CmdletBinding()]
param(
	[string]$Dir           = ".\build-matrix",
	[string]$SrcDir,
	[string]$VsPath,
	[string]$Mpg123Dir,
	[string]$LibsndfileDir,
	[string]$GtkDir,
	[string]$Config        = "Release,Debug",
	[string]$Arch          = "x64"
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

# main solution: the vc_solution solution that is not the clients one. The glob
# matches both the .sln and the newer .slnx so either form is found.
$sln = Get-ChildItem (Join-Path $SrcDir "vc_solution\*.sln*") -ErrorAction SilentlyContinue |
	Where-Object { $_.Name -notmatch 'client' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $sln) { Fail "No main solution found under vc_solution\." }

# the mp3x analyzer project, built on its own because the solution leaves it
# unselected (it needs GTK 4, which is not shipped)
$mp3x = Join-Path $SrcDir "vc_solution\vs_lame_mp3x.vcxproj"

# Probe the optional libraries, so that only locally buildable cells are
# emitted. Each is taken from its -<name>Dir override, or the conventional
# vc_solution location setup-windows-deps.ps1 lays it out in. The nmake build is
# 32-bit, so it uses each library's Win32 build. A returned path means "present
# and usable"; empty means the cell is skipped.
$vcsol = Join-Path $SrcDir "vc_solution"
function Probe-Dep($override, $default, $marker) {
	$dir = if ($override) { $override } else { $default }
	if ($dir -and (Test-Path (Join-Path $dir $marker))) { return (Resolve-Path $dir).Path }
	return ""
}

# mpg123: both builds generate the import library from the shipped .def - the
# MSBuild projects into their own $(IntDir), nmake into its output directory -
# so a distribution folder needs the header and the .def and nothing else. The
# static distributions ship neither, which is what the second test is for.
$mpg123 = Probe-Dep $Mpg123Dir (Join-Path $vcsol "mpg123\Win32") "mpg123.h"
if ($mpg123 -and -not (Test-Path (Join-Path $mpg123 "libmpg123-0.def"))) {
	Write-Warning "no libmpg123-0.def in '$mpg123' - the decoder cells will be skipped."
	Write-Warning "A static mpg123 distribution ships none; use the regular one."
	$mpg123 = ""
}

$sndfile = Probe-Dep $LibsndfileDir (Join-Path $vcsol "libsndfile\Win32") "lib\sndfile.lib"
$gtk     = Probe-Dep $GtkDir        (Join-Path $vcsol "gtk4\x64")        "lib\gtk-4.lib"

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
	"mpg123     = $(if ($mpg123) { $mpg123 } else { '(none - decoder cells skipped)' })"
	"libsndfile = $(if ($sndfile) { $sndfile } else { '(none - libsndfile cells skipped)' })"
	"gtk4       = $(if ($gtk) { $gtk } else { '(none - mp3x cells skipped)' })"
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

$nmakeCommon = "call `"$vcvars`" x86`r`ncd /d `"%~dp0`"`r`nnmake /nologo /f `"$SrcDir\Makefile.MSVC`" srcdir=`"$SrcDir`" ASM=NO"

$cells += @{ Name = "nmake-default"; Cmd = "$nmakeCommon SNDFILE=NO all" }
if ($mpg123) {
	$cells += @{ Name = "nmake-mpg123"; Cmd = "$nmakeCommon SNDFILE=NO MPG123=YES MPG123_DIR=`"$mpg123`" all" }
}
if ($sndfile) {
	$cells += @{ Name = "nmake-sndfile"; Cmd = "$nmakeCommon SNDFILE=YES SNDFILE_DIR=`"$sndfile`" all" }
}
# No nmake mp3x cell: Makefile.MSVC is 32-bit and GTK 4 is published for x64
# only, so the analyzer is an MSBuild-only target on Windows.

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
# Optional-library MSBuild flips on the base config/arch. The .props files find
# each library per platform under vc_solution on their own, so these only flip
# the switch on; a matching library must be laid out for the base platform.
$c0 = $configs[0]; $a0 = $archs[0]; $p0 = To-Platform $a0
$msbBase = "cd /d `"%~dp0`"`r`n`"$msbuild`" `"$sln`" /nologo /m /t:Rebuild /p:Configuration=$c0 /p:Platform=$p0"
$msbDirs = "/p:OutDirBase=%~dp0bin\ /p:IntDirBase=%~dp0obj\"

# Only the switch, never the path: $mpg123 is the 32-bit library the nmake
# cells need, and handing it to an MSBuild cell built for another platform
# produces a link that cannot resolve a single decoder symbol. The .props file
# already resolves .\mpg123\$(Platform) per configuration, which is the right
# answer for every platform this cell can be generated for - exactly as the
# libsndfile cell below flips its switch and leaves its path alone.
# These gates ask about the platform being built; the probes above ask about the
# 32-bit one the nmake cells use. A machine can easily satisfy one and not the
# other, so a gate that does not fire has to say so - reporting the nmake probe
# alone would leave the run claiming a library it did not build a cell for.
$msbSkipped = @()
$mpg123Marker = Join-Path $vcsol "mpg123\$p0\libmpg123-0.def"
if ((Test-Path (Join-Path $vcsol "mpg123\$p0\mpg123.h")) -and (Test-Path $mpg123Marker)) {
	$cells += @{ Name = "msbuild-$c0-$a0-mpg123"
		Cmd = "$msbBase /p:HaveMpg123=true $msbDirs" }
} else {
	$msbSkipped += "mpg123 (decoder) for $p0 - no $mpg123Marker"
}
$sndfileMarker = Join-Path $vcsol "libsndfile\$p0\lib\sndfile.lib"
if (Test-Path $sndfileMarker) {
	$cells += @{ Name = "msbuild-$c0-$a0-sndfile"
		Cmd = "$msbBase /p:HaveLibsndfile=true $msbDirs" }
} else {
	$msbSkipped += "libsndfile for $p0 - no $sndfileMarker"
}
if ($gtk) {
	# The analyzer builds x64 only - the published GTK 4 for MSVC is x64 - and
	# is not selected in the solution, so build its project directly with GTK
	# enabled and its prefix passed through.
	$cells += @{ Name = "msbuild-Release-x64-mp3x"
		# No trailing separator on Gtk4Path: quoted, it would escape the
		# closing quote and MSBuild would receive no usable value at all.
		Cmd = "cd /d `"%~dp0`"`r`n`"$msbuild`" `"$mp3x`" /nologo /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:HaveGtk=true /p:Gtk4Path=`"$gtk`" $msbDirs" }
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

# The console notice scrolls away; this file is what anyone reads afterwards, so
# the cells that were NOT generated belong in it beside the ones that were.
if ($msbSkipped.Count -gt 0) {
	Add-Content -Encoding ASCII $info "skipped:"
	foreach ($s in $msbSkipped) { Add-Content -Encoding ASCII $info "  [skip] $s" }
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
$absent = @()
if (-not $mpg123)  { $absent += "mpg123 (decoder)" }
if (-not $sndfile) { $absent += "libsndfile" }
if (-not $gtk)     { $absent += "GTK4 (mp3x analyzer)" }
if ($absent.Count -gt 0) {
	Write-Host ""
	Write-Host "NOTE: not found for the 32-bit nmake cells, skipped: $($absent -join ', ')."
}
if ($msbSkipped.Count -gt 0) {
	Write-Host ""
	Write-Host "NOTE: MSBuild cells skipped, the library is not laid out for that platform:"
	foreach ($s in $msbSkipped) { Write-Host "        $s" }
}
if ($absent.Count -gt 0 -or $msbSkipped.Count -gt 0) {
	Write-Host "      Lay them out with setup-windows-deps.ps1 or pass -<name>Dir."
}
Write-Host ""
Write-Host "To run the matrix:"
Write-Host "  pwsh $driver"
