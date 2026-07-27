<#
.SYNOPSIS
  Validate a LAME distribution tarball on native Windows.

.DESCRIPTION
  The Windows half of check-dist.sh. It extracts the tarball, prints the
  version under test, and reports each step as a named check with a one-line
  description and a PASS / FAIL / SKIP / N/A verdict, continuing through every
  check and exiting non-zero only at the end.

  What it covers is the native Windows build - nmake against Makefile.MSVC and
  the MSBuild solution - through gen-build-matrix.ps1. What it does not cover
  is everything that only exists in the autotools build: there is no configure,
  so no `make distcheck`, and the CMocka unit tests are not part of the native
  Windows build at all. Those checks report N/A here rather than SKIP, because
  no prerequisite would make them runnable on this path - run check-dist.sh
  under MSYS2, WSL or a POSIX host for them.

  See doc\maintainer-check-dist.md for the full guide.

.PARAMETER Tarball
  The distribution tarball to validate (.tar.gz).
.PARAMETER TargetDir
  A directory to work in; created if missing. The extracted source, the build
  matrix and the log all go inside it.
.PARAMETER Quick
  Build one configuration instead of the full matrix.
.PARAMETER VsPath
  Visual Studio installation to use (passed through to gen-build-matrix.ps1).
.PARAMETER AudioDir
  Encode every file in this directory through a built cell and require success
  and a non-empty MP3. A robustness smoke test, not a quality measurement.

.EXAMPLE
  .\check-dist.ps1 lame-4.1.tar.gz C:\temp\distcheck
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true, Position = 0)][string]$Tarball,
	[Parameter(Mandatory = $true, Position = 1)][string]$TargetDir,
	[switch]$Quick,
	[string]$VsPath,
	[string]$AudioDir
)

$ErrorActionPreference = "Continue"

# --- check bookkeeping ------------------------------------------------------

$script:Results = @()

function Start-Check($name, $what) {
	Write-Output ""
	Write-Output "[ CHECK ] $name"
	Write-Output "          $what"
}

function End-Check($verdict, $name, $detail) {
	$script:Results += [pscustomobject]@{ Name = $name; Verdict = $verdict }
	if ($detail) {
		Write-Output ("[ {0,-4} ] {1} - {2}" -f $verdict, $name, $detail)
	} else {
		Write-Output ("[ {0,-4} ] {1}" -f $verdict, $name)
	}
}

# --- paths ------------------------------------------------------------------

if (-not (Test-Path -LiteralPath $Tarball -PathType Leaf)) {
	Write-Error "check-dist: '$Tarball' is not a file"
	exit 2
}
$Tarball = (Resolve-Path -LiteralPath $Tarball).Path
if (-not (Test-Path -LiteralPath $TargetDir)) {
	New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}
$TargetDir = (Resolve-Path -LiteralPath $TargetDir).Path

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $TargetDir "check-dist-windows-$stamp.log"
Start-Transcript -Path $log -Force | Out-Null

Write-Output "============================================================"
Write-Output " LAME distribution check (native Windows)"
Write-Output "   tarball : $Tarball"
Write-Output "   target  : $TargetDir"
Write-Output "   system  : $([System.Environment]::OSVersion.VersionString) $($env:PROCESSOR_ARCHITECTURE)"
Write-Output "   started : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
Write-Output "============================================================"

# --- check: extract ---------------------------------------------------------

$srcParent = Join-Path $TargetDir "src"
Start-Check "extract" "the tarball unpacks, and into exactly one top-level directory"
if (Test-Path -LiteralPath $srcParent) { Remove-Item -Recurse -Force $srcParent }
New-Item -ItemType Directory -Path $srcParent -Force | Out-Null
$srcDir = $null
# tar.exe has shipped with Windows since 1803; nothing else here needs a
# third-party archiver, so a missing tar is a hard stop rather than a fallback.
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
	End-Check "FAIL" "extract" "tar.exe not found (Windows 10 1803 or newer has it)"
} else {
	& tar -x -f $Tarball -C $srcParent 2>&1 | Out-Null
	$tops = @(Get-ChildItem -LiteralPath $srcParent)
	if ($LASTEXITCODE -ne 0) {
		End-Check "FAIL" "extract" "tar exited $LASTEXITCODE"
	} elseif ($tops.Count -ne 1) {
		End-Check "FAIL" "extract" "$($tops.Count) top-level entries, expected 1 (a tarbomb)"
	} else {
		$srcDir = $tops[0].FullName
		End-Check "PASS" "extract" "unpacked into $($tops[0].Name)"
	}
}

if (-not $srcDir) {
	Write-Output ""
	Write-Output "cannot continue without an extracted source tree."
	Write-Output "============================================================"
	Write-Output " summary: 0 PASS, 1 FAIL, 0 SKIP"
	Write-Output "============================================================"
	Stop-Transcript | Out-Null
	exit 1
}

# --- version banner ---------------------------------------------------------

$vh = Join-Path $srcDir "libmp3lame\version.h"
function Get-VersionDefine($name) {
	if (-not (Test-Path -LiteralPath $vh)) { return $null }
	$m = Select-String -LiteralPath $vh -Pattern "^#\s*define\s+$name\s+(\d+)" |
		Select-Object -First 1
	if ($m) { return [int]$m.Matches[0].Groups[1].Value }
	return $null
}
$vMajor = Get-VersionDefine "LAME_MAJOR_VERSION"
$vMinor = Get-VersionDefine "LAME_MINOR_VERSION"
$vType  = Get-VersionDefine "LAME_TYPE_VERSION"
$vPatch = Get-VersionDefine "LAME_PATCH_VERSION"
$vKind = switch ($vType) { 0 { "alpha" } 1 { "beta" } 2 { "release" } default { "unknown type '$vType'" } }
$version = "$vMajor.$vMinor"

Write-Output ""
Write-Output "------------------------------------------------------------"
Write-Output " version under test: LAME $version ($vKind, patch level $vPatch)"
Write-Output "------------------------------------------------------------"

# --- check: version-consistency ---------------------------------------------

Start-Check "version-consistency" "the tarball name, configure's package version and version.h agree"
$cfgAc = Join-Path $srcDir "configure.ac"
$cfgVersion = $null
if (Test-Path -LiteralPath $cfgAc) {
	$m = Select-String -LiteralPath $cfgAc -Pattern '^AC_INIT\(\[?lame\]?,\s*\[?([^\],\)]+)' |
		Select-Object -First 1
	if ($m) { $cfgVersion = $m.Matches[0].Groups[1].Value.Trim() }
}
$tarVersion = [System.IO.Path]::GetFileName($Tarball) -replace '^lame-', '' -replace '\.tar\.(gz|bz2|xz)$', '' -replace '\.tgz$', ''
Write-Output "          tarball name : $tarVersion"
Write-Output "          configure    : $(if ($cfgVersion) { $cfgVersion } else { '<not found>' })"
Write-Output "          version.h    : $version"
if (-not $vMajor -and $vMajor -ne 0) {
	End-Check "FAIL" "version-consistency" "could not read version.h"
} elseif (-not $cfgVersion) {
	End-Check "FAIL" "version-consistency" "could not read a package version from configure.ac"
} elseif ($cfgVersion -ne $version) {
	End-Check "FAIL" "version-consistency" "configure says $cfgVersion, version.h says $version"
} elseif ($tarVersion -ne $version) {
	End-Check "FAIL" "version-consistency" "tarball is named $tarVersion but contains $version"
} else {
	End-Check "PASS" "version-consistency" "all three say $version"
}

# --- build the matrix -------------------------------------------------------

$matrix = Join-Path $TargetDir "build"
if (Test-Path -LiteralPath $matrix) { Remove-Item -Recurse -Force $matrix }
$gen = Join-Path $srcDir "maintainer\gen-build-matrix.ps1"

Start-Check "matrix-generate" "the shipped Windows build harness can lay out this tarball's configurations"
$cells = @()
if (-not (Test-Path -LiteralPath $gen)) {
	End-Check "FAIL" "matrix-generate" "$gen is missing from the tarball"
} else {
	$genArgs = @{ Dir = $matrix; SrcDir = $srcDir }
	if ($VsPath) { $genArgs.VsPath = $VsPath }
	if ($Quick)  { $genArgs.Config = "Release"; $genArgs.Arch = "x64" }
	$genLog = Join-Path $TargetDir "matrix-gen.log"
	try {
		& $gen @genArgs *>&1 | Tee-Object -FilePath $genLog | Out-Null
		$cells = @(Get-ChildItem -LiteralPath $matrix -Recurse -Filter build.cmd -ErrorAction SilentlyContinue)
		if ($cells.Count -eq 0) {
			End-Check "FAIL" "matrix-generate" "the harness generated no cells at all (see $genLog)"
		} else {
			End-Check "PASS" "matrix-generate" "$($cells.Count) cell(s)"
		}
	} catch {
		End-Check "FAIL" "matrix-generate" "the harness failed: $_"
	}
}

# --- per-cell: build, then unit tests ---------------------------------------

$firstBuilt = $null
foreach ($cell in $cells) {
	$dir = $cell.Directory.FullName
	$tag = $dir.Substring($matrix.Length).Trim('\') -replace '\\', '-'

	Start-Check "build[$tag]" "this configuration compiles and links from the extracted tarball"
	$buildLog = Join-Path $dir "build.log"
	Push-Location $dir
	# By full path on purpose. This shell runs with the current directory
	# excluded from the executable search path, so `cmd /c build.cmd` reports
	# "is not recognized as an internal or external command" even though the
	# file is right there - which reads as a build failure rather than as a
	# command that never started.
	& cmd.exe /c "`"$($cell.FullName)`"" *>&1 | Tee-Object -FilePath $buildLog | Out-Null
	$rc = $LASTEXITCODE
	Pop-Location
	if ($rc -eq 0) {
		End-Check "PASS" "build[$tag]"
		if (-not $firstBuilt) { $firstBuilt = $dir }
	} else {
		End-Check "FAIL" "build[$tag]" "exit $rc, see $buildLog"
	}

	Start-Check "unit-tests[$tag]" "the CMocka suite passes in this configuration"
	End-Check "N/A" "unit-tests[$tag]" "the native Windows build has no unit-test target; run check-dist.sh for those"
}

# --- checks that need the autotools build -----------------------------------

Start-Check "distcheck" "make distcheck: a VPATH build, install, installcheck, uninstall and re-dist"
End-Check "N/A" "distcheck" "no configure in the native Windows build; run check-dist.sh for this"

Start-Check "abi" "the built library's exported interface still matches the committed contract"
$abicheck = Join-Path $srcDir "maintainer\abicheck.sh"
if (-not (Test-Path -LiteralPath $abicheck)) {
	End-Check "N/A" "abi" "this tarball does not carry the ABI check"
} else {
	End-Check "N/A" "abi" "the ABI check is a POSIX shell script; run it from MSYS2 or check-dist.sh"
}

# --- check: audio smoke (opt-in) --------------------------------------------

if ($AudioDir) {
	Start-Check "audio-smoke" "every file in the supplied directory encodes to a non-empty MP3"
	$lameExe = $null
	if ($firstBuilt) {
		$lameExe = Get-ChildItem -LiteralPath $firstBuilt -Recurse -Filter lame.exe -ErrorAction SilentlyContinue |
			Select-Object -First 1
	}
	if (-not $lameExe) {
		End-Check "SKIP" "audio-smoke" "no built lame.exe to encode with"
	} elseif (-not (Test-Path -LiteralPath $AudioDir)) {
		End-Check "SKIP" "audio-smoke" "-AudioDir '$AudioDir' is not a directory"
	} else {
		$out = Join-Path $TargetDir "audio-smoke"
		if (Test-Path -LiteralPath $out) { Remove-Item -Recurse -Force $out }
		New-Item -ItemType Directory -Path $out -Force | Out-Null
		$bad = 0; $total = 0
		foreach ($f in Get-ChildItem -LiteralPath $AudioDir -File) {
			$total++
			$o = Join-Path $out "$($f.Name).mp3"
			& $lameExe.FullName --quiet $f.FullName $o *>&1 |
				Out-File -Append (Join-Path $out "encode.log")
			if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $o) -or (Get-Item -LiteralPath $o).Length -eq 0) {
				$bad++
				"  failed: $($f.FullName)" | Out-File -Append (Join-Path $out "encode.log")
			}
		}
		if ($total -eq 0) {
			End-Check "SKIP" "audio-smoke" "no files in $AudioDir"
		} elseif ($bad -eq 0) {
			End-Check "PASS" "audio-smoke" "$total file(s)"
		} else {
			End-Check "FAIL" "audio-smoke" "$bad of $total failed"
		}
	}
}

# --- summary ----------------------------------------------------------------

$np = @($script:Results | Where-Object Verdict -eq "PASS").Count
$nf = @($script:Results | Where-Object Verdict -eq "FAIL").Count
$ns = @($script:Results | Where-Object Verdict -eq "SKIP").Count
$nn = @($script:Results | Where-Object Verdict -eq "N/A").Count

Write-Output ""
Write-Output "============================================================"
Write-Output " LAME $version ($vKind, patch level $vPatch)"
Write-Output " $($script:Results.Count) check(s): $np PASS, $nf FAIL, $ns SKIP, $nn N/A"
if ($nf -gt 0) {
	Write-Output ""
	Write-Output " failed:"
	$script:Results | Where-Object Verdict -eq "FAIL" | ForEach-Object { Write-Output "   $($_.Name)" }
}
if ($ns -gt 0) {
	Write-Output ""
	Write-Output " skipped (a prerequisite is missing on this machine):"
	$script:Results | Where-Object Verdict -eq "SKIP" | ForEach-Object { Write-Output "   $($_.Name)" }
}
if ($nn -gt 0) {
	Write-Output ""
	Write-Output " not applicable to the native Windows build:"
	$script:Results | Where-Object Verdict -eq "N/A" | ForEach-Object { Write-Output "   $($_.Name)" }
}
Write-Output " finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
Write-Output "============================================================"

Stop-Transcript | Out-Null

# The log is named after the version, as in check-dist.sh. It is not known until
# the tarball has been extracted and the first check has already printed, so the
# transcript is written under a timestamped name and renamed once it is closed.
if ($null -ne $vMajor -and $null -ne $vMinor) {
	$named = Join-Path $TargetDir "check-dist-windows-$version-$stamp.log"
	try {
		Move-Item -LiteralPath $log -Destination $named -Force -ErrorAction Stop
		$log = $named
	} catch {
		Write-Output "note: keeping the log under its timestamped name: $_"
	}
}

Write-Output ""
Write-Output "log: $log"
if ($nf -gt 0) { exit 1 }
exit 0
