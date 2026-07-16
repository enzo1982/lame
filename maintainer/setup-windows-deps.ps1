<#
.SYNOPSIS
  Lay out the optional Windows build dependencies where the Visual Studio and
  nmake builds look for them, from archives you have already downloaded.

.DESCRIPTION
  libsndfile, mpg123 and GTK1 are each distributed as a zip that unpacks into a
  versioned wrapper folder. The build instead looks in a fixed place per
  dependency and platform (vc_solution\libsndfile\<Platform>,
  vc_solution\mpg123\<Platform>, vc_solution\WinGtk). This script extracts each
  archive it is given into that place, dropping the wrapper.

  Nothing is downloaded. Pass the folder holding the archives you fetched; for
  any dependency whose archive is not there, the script prints where to get it.
  All three destinations are git-ignored and never committed.

  The per-platform folders are named Win32 and x64 to match the build's
  $(Platform); the casing is only for readability, since the Windows file
  system is case-insensitive.

.PARAMETER From
  Folder holding the downloaded archives. Default: the current directory.

.PARAMETER Dest
  The vc_solution folder. Default: the vc_solution beside this script's parent.
#>
[CmdletBinding()]
param(
	[string]$From = ".",
	[string]$Dest
)

$ErrorActionPreference = "Stop"

if (-not $Dest) { $Dest = Join-Path (Split-Path -Parent $PSScriptRoot) "vc_solution" }
$Dest = (Resolve-Path $Dest).Path
$From = (Resolve-Path $From).Path

# name | download URL | archive glob in $From | destination under $Dest | flatten
#   flatten strips the single wrapper folder the archive unpacks into; GTK is
#   laid down whole because the build path includes its wrapper (gtk\).
$deps = @(
	@{ Name = "libsndfile (32-bit)"; Url = "https://libsndfile.github.io/libsndfile/"
	   Glob = "libsndfile-*-win32.zip"; To = "libsndfile\Win32"; Flatten = $true }
	@{ Name = "libsndfile (64-bit)"; Url = "https://libsndfile.github.io/libsndfile/"
	   Glob = "libsndfile-*-win64.zip"; To = "libsndfile\x64";   Flatten = $true }
	@{ Name = "mpg123 (32-bit)";     Url = "https://mpg123.de/download/win32/"
	   Glob = "mpg123-*-x86.zip";     To = "mpg123\Win32";       Flatten = $true }
	@{ Name = "mpg123 (64-bit)";     Url = "https://mpg123.de/download/win32/"
	   Glob = "mpg123-*-x86-64.zip";  To = "mpg123\x64";         Flatten = $true }
	@{ Name = "GTK1 for Windows";    Url = "https://sourceforge.net/projects/gtk1-win/"
	   Glob = "gtkwin-*.zip";         To = "WinGtk";             Flatten = $false }
)

function Place-Dep($dep) {
	$archive = Get-ChildItem (Join-Path $From $dep.Glob) -ErrorAction SilentlyContinue |
		Select-Object -First 1
	if (-not $archive) {
		Write-Host ("  skip  {0,-20} no {1} in '{2}' - get it from {3}" -f `
			$dep.Name, $dep.Glob, $From, $dep.Url)
		return
	}
	$target = Join-Path $Dest $dep.To
	if (Test-Path $target) { Remove-Item -Recurse -Force $target }
	New-Item -ItemType Directory -Force $target | Out-Null

	$tmp = Join-Path ([IO.Path]::GetTempPath()) ("lamedep_" + [guid]::NewGuid())
	Expand-Archive -Path $archive.FullName -DestinationPath $tmp -Force
	try {
		if ($dep.Flatten) {
			# One wrapper folder inside the archive; move its contents up.
			$roots = @(Get-ChildItem $tmp)
			$src = if ($roots.Count -eq 1 -and $roots[0].PSIsContainer) { $roots[0].FullName } else { $tmp }
			Get-ChildItem $src -Force | Move-Item -Destination $target
		} else {
			Get-ChildItem $tmp -Force | Move-Item -Destination $target
		}
	} finally {
		Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
	}
	Write-Host ("  done  {0,-20} -> {1}" -f $dep.Name, $target)
}

Write-Host "Laying out Windows build dependencies"
Write-Host "  from: $From"
Write-Host "  into: $Dest"
Write-Host ""
foreach ($dep in $deps) { Place-Dep $dep }
Write-Host ""
Write-Host "Enable each dependency in its .props file (HaveLibsndfile, HaveMpg123,"
Write-Host "HaveGtk) or pass it to gen-build-matrix.ps1; see README.vs.txt."
