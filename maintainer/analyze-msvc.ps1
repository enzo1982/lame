<#
.SYNOPSIS
  Run the MSVC static analyzer (/analyze) over the MSBuild projects of the tree
  and compare what it finds with a baseline.

.DESCRIPTION
  Every project file compiles with /analyze- . This script turns the analyzer
  on for one run, without changing a project file: it builds each project
  with a property sheet forced in ahead of the C++ targets, and reads the
  analyzer's warnings (C6xxx and C28xxx) from the build log.

  Without -Project, the run covers every project and platform pair of the
  tree, Release. mp3x and the DirectShow filter need sources the tree does
  not ship; they are analysed when -GtkDir and -DShowBaseClassesDir name them,
  and reported as skipped otherwise.

  A finding is keyed by its file, relative to the tree, and its warning code.
  The key carries the number of lines with that code in that file, not the
  lines themselves, so an edit above a finding does not change the key, and
  one more finding of the same kind in the same file still counts as new.
  Findings in material the tree does not own (the Windows SDK, the bundled
  TinyXML, GTK, the DirectShow base classes) are counted apart and never
  compared.

  The script refuses to report rather than report a result it has not
  measured: a compile line without /analyze, a build that fails, a project
  without the platform asked for, and a build with no compile line at all each
  end the run with exit status 2.

.PARAMETER Tree
  The source tree: the directory that holds vc_solution. Default: the parent
  of the directory of this script.

.PARAMETER Project
  One project to analyse, for example vc_solution\vs_libmp3lame.vcxproj. With
  it, -Platform and -Property apply. Without it, the whole tree is analysed.

.PARAMETER Platform
  The platform of -Project: Win32 or x64. Default: Win32.

.PARAMETER Property
  MSBuild properties for -Project, as one string of name=value pairs separated
  by ";", for example "HaveGtk=true;Gtk4Path=C:\gtk". One string and not an
  array, because "powershell -File" passes only the first element of an array.

.PARAMETER GtkDir
  The GTK 4 install prefix, for mp3x (x64 only).

.PARAMETER DShowBaseClassesDir
  The folder with the DirectShow base class sources (streams.h), for the
  DirectShow filter (Win32 only).

.PARAMETER Baseline
  The baseline file. Default: analyze\baseline.txt beside this script. Each
  line holds a file, a code, a count and the reason the findings are kept;
  "#" starts a comment.

.PARAMETER UpdateBaseline
  Write the keys of this run to -Baseline instead of comparing. The reasons of
  keys already in the file are kept; a new key gets the reason "untriaged".

.PARAMETER LogDir
  Where the build logs go, one per project and platform. Default: a new
  folder in %TEMP%. Its path is printed in every outcome.

.PARAMETER MSBuild
  The MSBuild.exe to use. Default: the one of the newest Visual Studio that
  vswhere finds.

.NOTES
  Exit status: 0 when nothing is new against the baseline, 1 when something
  is, 2 when the run could not measure.

  Part of the LAME distribution. See doc/maintainer-analyze-msvc.md.
#>
[CmdletBinding()]
param(
	[string]$Tree = "",
	[string]$Project = "",
	[string]$Platform = "Win32",
	[string]$Property = "",
	[string]$GtkDir = "",
	[string]$DShowBaseClassesDir = "",
	[string]$Baseline = "",
	[switch]$UpdateBaseline,
	[string]$LogDir = "",
	[string]$MSBuild = ""
)

$ErrorActionPreference = "Stop"
if (-not $Tree) { $Tree = Split-Path -Parent $PSScriptRoot }
if (-not $Baseline) { $Baseline = Join-Path $PSScriptRoot "analyze\baseline.txt" }

function Find-MSBuild {
	if ($MSBuild) {
		if (Test-Path $MSBuild) { return $MSBuild }
		return $null
	}
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (-not (Test-Path $vswhere)) { return $null }
	$root = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
	if (-not $root) { return $null }
	foreach ($rel in @("MSBuild\Current\Bin\amd64\MSBuild.exe", "MSBuild\Current\Bin\MSBuild.exe")) {
		$c = Join-Path $root $rel
		if (Test-Path $c) { return $c }
	}
	return $null
}

function Stop-Run([string]$why) {
	Write-Output $why
	Write-Output "logs: $LogDir"
	exit 2
}

$mb = Find-MSBuild
if (-not $mb) { Stop-Run "no MSBuild found - install the C++ workload or pass -MSBuild" }
$Tree = (Resolve-Path $Tree).Path.TrimEnd('\')
$vcsol = Join-Path $Tree "vc_solution"
if (-not (Test-Path $vcsol)) { Stop-Run "no vc_solution folder in $Tree" }
if (-not $LogDir) {
	$LogDir = Join-Path ([System.IO.Path]::GetTempPath()) ("lame-analyze-" + $PID)
}
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogDir = (Resolve-Path $LogDir).Path

# The projects and platforms of the tree. A project builds the projects it
# references too, so the libraries are analysed under every program that links
# them, with the same findings each time.
$units = @(
	@{ Project = "vs_libmp3lame";       Platform = "Win32" }
	@{ Project = "vs_libmp3lame";       Platform = "x64" }
	@{ Project = "vs_libmp3lame_dll";   Platform = "Win32" }
	@{ Project = "vs_libmp3lame_dll";   Platform = "x64" }
	@{ Project = "vs_libmp3lame_nodec"; Platform = "Win32" }
	@{ Project = "vs_libmp3lame_nodec"; Platform = "x64" }
	@{ Project = "vs_lame_lame";        Platform = "Win32" }
	@{ Project = "vs_lame_lame";        Platform = "x64" }
	@{ Project = "vs_lame_mp3rtp";      Platform = "Win32" }
	@{ Project = "vs_lame_test";        Platform = "Win32" }
	@{ Project = "vs_lame_enc_dll";     Platform = "Win32" }
	@{ Project = "vs_lame_enc_dll";     Platform = "x64" }
	@{ Project = "vs_lame_blade_test";  Platform = "Win32" }
	@{ Project = "vs_lame_blade_test";  Platform = "x64" }
	@{ Project = "vs_lame_acm";         Platform = "Win32" }
	@{ Project = "vs_lame_acm_test";    Platform = "Win32" }
	@{ Project = "vs_lame_dshow_test";  Platform = "Win32" }
	@{ Project = "vs_lame_mp3x";        Platform = "x64";   Needs = "GtkDir" }
	@{ Project = "vs_lame_dshow";       Platform = "Win32"; Needs = "DShowBaseClassesDir" }
)
if ($Project) {
	$units = @(@{ Project = [System.IO.Path]::GetFileNameWithoutExtension($Project); Platform = $Platform
	              Properties = @($Property -split ';' | Where-Object { $_ }) })
}

# Forced in ahead of the C++ targets, so it overrides the project's /analyze-.
$props = Join-Path $LogDir "analyze.props"
@'
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup>
    <ClCompile>
      <EnablePREfast>true</EnablePREfast>
      <TreatWarningAsError>false</TreatWarningAsError>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
'@ | Set-Content $props -Encoding UTF8

# file(line[,column]): warning Cnnnn: text
$rx = '^\s*(?<path>(?:[A-Za-z]:\\|\.\.\\)[^(]+)\((?<line>\d+)[,)][^:]*: warning (?<code>C(?:6|28)\d+): (?<text>.*?)(?: \[[^\]]+\])?$'
$own = @{}
# Sources outside the tree are not its own; neither are the SDKs named on the
# command line, wherever they are.
$foreign = @(@($GtkDir, $DShowBaseClassesDir) | Where-Object { $_ } |
	ForEach-Object { [System.IO.Path]::GetFullPath($_).TrimEnd('\') + "\" })
$external = 0
$analysed = 0
$skipped = @()

foreach ($u in $units) {
	$name = "{0}|{1}" -f $u.Project, $u.Platform
	$extra = @()
	if ($u.Properties) { $extra = $u.Properties }
	if ($u.Needs -eq "GtkDir") {
		if (-not $GtkDir) { $skipped += "$name (no -GtkDir)"; continue }
		$extra = @("HaveGtk=true", "Gtk4Path=$($GtkDir.TrimEnd('\'))")
	}
	if ($u.Needs -eq "DShowBaseClassesDir") {
		if (-not $DShowBaseClassesDir) { $skipped += "$name (no -DShowBaseClassesDir)"; continue }
		$extra = @("HaveDShowBaseClasses=true", "DShowBaseClassesPath=$($DShowBaseClassesDir.TrimEnd('\'))")
	}
	$proj = Join-Path $vcsol ($u.Project + ".vcxproj")
	if (-not (Test-Path $proj)) { Stop-Run "no project $proj" }
	$log = Join-Path $LogDir ("{0}-{1}.log" -f $u.Project, $u.Platform)
	$msb = @($proj, "/nologo", "/t:Rebuild", "/p:Configuration=Release", "/p:Platform=$($u.Platform)",
	         "/p:ForceImportBeforeCppTargets=$props", "/noconsolelogger",
	         "/fl", "/flp:LogFile=$log;Encoding=UTF-8;Verbosity=normal")
	$msb += @($extra | ForEach-Object { "/p:$_" })
	& $mb @msb | Out-Null
	$rc = $LASTEXITCODE

	# The checks come before any counting, in the order a cause would show.
	if (Select-String -Path $log -Encoding UTF8 -Pattern 'error MSB8013' -Quiet) {
		Stop-Run "$name : the project has no $($u.Platform) platform - see $log"
	}
	if ($rc -ne 0) { Stop-Run "$name : the build failed (rc=$rc) - see $log" }
	$cls = @(Select-String -Path $log -Encoding UTF8 -Pattern '\\CL\.exe /c ')
	if ($cls.Count -eq 0) { Stop-Run "$name : no compile line in the log, nothing was compiled - see $log" }
	$off = @($cls | Where-Object { $_.Line -notmatch ' /analyze(?!-)' })
	if ($off.Count -gt 0) {
		Stop-Run "$name : $($off.Count) compile line(s) without /analyze, the analyzer did not run - see $log"
	}

	$n = 0
	foreach ($h in (Select-String -Path $log -Encoding UTF8 -Pattern $rx)) {
		$m = $h.Matches[0]
		$path = $m.Groups['path'].Value
		if ($path.StartsWith("..\")) { $path = Join-Path $vcsol $path }
		$path = [System.IO.Path]::GetFullPath($path)
		$rel = $null
		$sdk = @($foreign | Where-Object { $path.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase) })
		if ($sdk.Count -eq 0 -and $path.StartsWith($Tree + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
			$rel = $path.Substring($Tree.Length + 1).Replace('\', '/')
		}
		if (-not $rel -or $rel -match '^(vc_solution/(gtk4|mpg123|libsndfile)/|ACM/tinyxml/)') {
			$external++
			continue
		}
		$key = "$rel $($m.Groups['code'].Value)"
		if (-not $own.ContainsKey($key)) { $own[$key] = @{} }
		$own[$key][$m.Groups['line'].Value] = $m.Groups['text'].Value
		$n++
	}
	$analysed++
	Write-Output ("{0,-30} {1} warning line(s) in the log" -f $name, $n)
}

Remove-Item $props -ErrorAction SilentlyContinue
foreach ($s in $skipped) { Write-Output "skipped: $s" }
Write-Output ""
Write-Output ("{0} build(s) analysed; {1} finding key(s) in the tree, {2} line(s) in material the tree does not own" -f `
	$analysed, $own.Count, $external)
Write-Output "logs: $LogDir"

# A baseline line: file code count reason...
function Read-Baseline([string]$file) {
	$b = @{}
	foreach ($l in (Get-Content $file)) {
		$t = $l.Trim()
		if (-not $t -or $t.StartsWith("#")) { continue }
		$f = $t -split '\s+', 4
		if ($f.Count -lt 3 -or $f[2] -notmatch '^\d+$') { Stop-Run "malformed baseline line: $l" }
		$why = "untriaged"
		if ($f.Count -eq 4) { $why = $f[3] }
		$b["$($f[0]) $($f[1])"] = @{ Count = [int]$f[2]; Reason = $why }
	}
	return $b
}

if ($UpdateBaseline) {
	$old = @{}
	if (Test-Path $Baseline) { $old = Read-Baseline $Baseline }
	$out = @("# maintainer/analyze-msvc.ps1 baseline: file, code, number of lines, reason")
	foreach ($k in ($own.Keys | Sort-Object)) {
		$why = "untriaged"
		if ($old.ContainsKey($k)) { $why = $old[$k].Reason }
		$out += ("{0} {1} {2}" -f $k, $own[$k].Count, $why)
	}
	$out | Set-Content $Baseline -Encoding ASCII
	Write-Output "baseline written: $Baseline ($($own.Count) keys)"
	exit 0
}

if (-not (Test-Path $Baseline)) { Stop-Run "no baseline at $Baseline" }
$base = Read-Baseline $Baseline
$new = 0
foreach ($k in ($own.Keys | Sort-Object)) {
	$have = $own[$k].Count
	$was = 0
	if ($base.ContainsKey($k)) { $was = $base[$k].Count }
	if ($have -gt $was) {
		$new++
		Write-Output ("NEW  {0}: {1} line(s), the baseline has {2}" -f $k, $have, $was)
		foreach ($line in ($own[$k].Keys | Sort-Object { [int]$_ })) {
			Write-Output ("       line {0}: {1}" -f $line, $own[$k][$line])
		}
	}
}
# One project builds only some of the files of the baseline.
foreach ($k in ($base.Keys | Sort-Object | Where-Object { -not $Project })) {
	$have = 0
	if ($own.ContainsKey($k)) { $have = $own[$k].Count }
	if ($have -lt $base[$k].Count) {
		Write-Output ("GONE {0}: {1} line(s), the baseline has {2}" -f $k, $have, $base[$k].Count)
	}
}
if ($skipped.Count -gt 0) {
	Write-Output "a GONE line may only mean that this run skipped the build of the file"
}
if ($new -gt 0) {
	Write-Output "$new key(s) above the baseline"
	exit 1
}
Write-Output "nothing above the baseline"
exit 0
