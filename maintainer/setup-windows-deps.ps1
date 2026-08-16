<#
.SYNOPSIS
  Lay out the optional Windows build dependencies where the Visual Studio and
  nmake builds look for them, from archives you have already downloaded.

.DESCRIPTION
  libsndfile, mpg123, GTK 4 and the DirectShow base classes each arrive as a
  zip. The build looks in a fixed place per dependency and platform
  (vc_solution\libsndfile\<Platform>, vc_solution\mpg123\<Platform>,
  vc_solution\gtk4\<Platform>, vc_solution\baseclasses). This script extracts
  each archive it is given into that place, dropping the wrapper folder where
  the archive has one.

  GTK 4 is published for x64 only, which is why mp3x is built for x64 only.

  Nothing is downloaded. Pass the folder holding the archives you fetched; for
  any dependency whose archive is not there, the script prints where to get it.
  All destinations are git-ignored and never committed.

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
#   flatten strips the single wrapper folder the archive unpacks into; the
#   gvsbuild archive needs none, its root already is the install prefix.
$deps = @(
	@{ Name = "libsndfile (32-bit)"; Url = "https://libsndfile.github.io/libsndfile/"
	   Glob = "libsndfile-*-win32.zip"; To = "libsndfile\Win32"; Flatten = $true }
	@{ Name = "libsndfile (64-bit)"; Url = "https://libsndfile.github.io/libsndfile/"
	   Glob = "libsndfile-*-win64.zip"; To = "libsndfile\x64";   Flatten = $true }
	@{ Name = "mpg123 (32-bit)";     Url = "https://mpg123.de/download/win32/"
	   Glob = "mpg123-*-x86.zip";     To = "mpg123\Win32";       Flatten = $true }
	@{ Name = "mpg123 (64-bit)";     Url = "https://mpg123.de/download/win32/"
	   Glob = "mpg123-*-x86-64.zip";  To = "mpg123\x64";         Flatten = $true }
	@{ Name = "GTK 4 (64-bit)";      Url = "https://github.com/wingtk/gvsbuild/releases"
	   Glob = "GTK4_Gvsbuild_*_x64.zip"; To = "gtk4\x64";        Flatten = $false }
	# The DirectShow base classes are a folder inside a sample repository rather
	# than a release archive, so the archive can be that whole repository or just
	# the folder taken out of it. Find decides between them by looking for a file
	# only the base classes have, which also rejects an archive that happens to
	# match the glob and holds something else.
	@{ Name = "DirectShow base classes"
	   Url = "https://github.com/microsoft/Windows-classic-samples (Samples\Win7Samples\multimedia\directshow\baseclasses)"
	   Glob = "*baseclasses*.zip", "Windows-classic-samples*.zip"
	   To = "baseclasses"; Flatten = $false; Find = "streams.h" }
)

function Place-Dep($dep) {
	$globs = @($dep.Glob)
	$archive = $globs | ForEach-Object { Get-ChildItem (Join-Path $From $_) -ErrorAction SilentlyContinue } |
		Select-Object -First 1
	if (-not $archive) {
		Write-Host ("  skip  {0,-20} no {1} in '{2}' - get it from {3}" -f `
			$dep.Name, ($globs -join " or "), $From, $dep.Url)
		return
	}
	$target = Join-Path $Dest $dep.To
	if (Test-Path $target) { Remove-Item -Recurse -Force $target }
	New-Item -ItemType Directory -Force $target | Out-Null

	$tmp = Join-Path ([IO.Path]::GetTempPath()) ("lamedep_" + [guid]::NewGuid())
	Expand-Archive -Path $archive.FullName -DestinationPath $tmp -Force
	try {
		if ($dep.Find) {
			# The wanted folder is somewhere inside; find it by a file only it
			# has, and take that folder. Refusing here beats placing a wrong
			# tree that fails much later as a missing include.
			$marker = Get-ChildItem $tmp -Recurse -Filter $dep.Find -ErrorAction SilentlyContinue |
				Select-Object -First 1
			if (-not $marker) {
				Write-Host ("  FAIL  {0,-20} no {1} anywhere in {2}" -f $dep.Name, $dep.Find, $archive.Name)
				return
			}
			Get-ChildItem $marker.DirectoryName -Force | Move-Item -Destination $target
		} elseif ($dep.Flatten) {
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
Write-Host "HaveGtk, HaveDShowBaseClasses) or pass it to gen-build-matrix.ps1;"
Write-Host "see README.vs.txt."
