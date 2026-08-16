<#
.SYNOPSIS
  Smoke-test the built Windows client components: load each one and resolve its
  entry points.

.DESCRIPTION
  The ACM codec and the DirectShow filter are DLLs that Windows loads into
  another program's process. Either one loads there or it does not, and a build
  that succeeded says nothing about that: a DLL can link cleanly and still fail
  to load for a missing runtime, and it can load and still have skipped its own
  initialisation. So this checks the produced binary rather than the build.

  Per component:
    - the image loads at all (LoadLibrary, which runs its DllMain);
    - every entry point it exists to provide resolves;
    - it imports no redistributable runtime, read from the import table rather
      than from the project settings.

  Both components are built for Win32. A 64-bit PowerShell cannot load a 32-bit
  DLL - LoadLibrary fails with ERROR_BAD_EXE_FORMAT and says nothing about the
  DLL - so this script re-runs itself under the 32-bit PowerShell when it meets
  a 32-bit image. Without that, the check would report a failure that is a
  property of the checker.

.PARAMETER Path
  A folder to search for lameACM.acm and lame.ax, or one such file.

.PARAMETER Require
  Comma-separated component names that MUST be present: acm, dshow. A component
  that is absent is otherwise reported and skipped, since the DirectShow filter
  is only built where its base class sources are laid out.

  One string and not a string array, because this script hands its arguments to
  a second PowerShell (see above) and an array does not survive that crossing:
  only the first element binds, and the components the caller required stop
  being required without anything being reported.
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory=$true)][string]$Path,
	[string]$Require = "acm"
)

$ErrorActionPreference = "Stop"

$required = @($Require.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })

$components = @(
	@{ Key = "acm";   File = "lameACM.acm"; Exports = @("DriverProc") }
	@{ Key = "dshow"; File = "lame.ax";     Exports = @("DllGetClassObject", "DllCanUnloadNow",
	                                                    "DllRegisterServer", "DllUnregisterServer") }
)

# Where the machine word lives in a Windows image: the DOS stub carries the
# offset of the PE header, the PE header opens with its signature, and the
# machine word is the field straight after it.
$PeHeaderOffsetField  = 0x3C
$PeSignature          = 0x00004550   # "PE\0\0"
$ImageFileMachineI386 = 0x014C

function Get-ImageMachine($file) {
	$fs = [IO.File]::OpenRead($file)
	try {
		$br = New-Object IO.BinaryReader($fs)
		$fs.Position = $PeHeaderOffsetField
		$fs.Position = $br.ReadInt32()
		if ($br.ReadUInt32() -ne $PeSignature) { return 0 }
		return $br.ReadUInt16()
	} finally { $fs.Dispose() }
}

# --- 32-bit re-exec ---------------------------------------------------------

$targets = @()
if (Test-Path -LiteralPath $Path -PathType Leaf) {
	$targets += (Resolve-Path $Path).Path
} else {
	foreach ($c in $components) {
		$f = Get-ChildItem -Path $Path -Recurse -Filter $c.File -ErrorAction SilentlyContinue |
			Select-Object -First 1
		if ($f) { $targets += $f.FullName }
	}
}
if ($targets.Count -eq 0) { Write-Host "no client binaries found under '$Path'"; exit 2 }

$need32 = $targets | Where-Object { (Get-ImageMachine $_) -eq $ImageFileMachineI386 }
if ($need32 -and [Environment]::Is64BitProcess) {
	$ps32 = Join-Path $env:SystemRoot "SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
	if (-not (Test-Path $ps32)) {
		Write-Host "FAIL: 32-bit binaries, 64-bit PowerShell, and no $ps32 to hand off to"
		exit 2
	}
	& $ps32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Path $Path -Require $Require
	exit $LASTEXITCODE
}

# --- the checks -------------------------------------------------------------

Add-Type -Namespace LameSmoke -Name Native -MemberDefinition @"
[DllImport("kernel32", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern IntPtr LoadLibraryW(string lpFileName);
[DllImport("kernel32", SetLastError=true)]
public static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);
[DllImport("kernel32", SetLastError=true)]
public static extern bool FreeLibrary(IntPtr hModule);
"@

$dumpbin = $null
$vs = $env:VSINSTALLDIR
if (-not $vs) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vswhere) { $vs = & $vswhere -latest -products * -property installationPath }
}
if ($vs) {
	$dumpbin = Get-ChildItem -Path (Join-Path $vs "VC\Tools\MSVC") -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
		Select-Object -First 1 -ExpandProperty FullName
}

$fail = 0
function Fail($m) { Write-Host "  FAIL  $m"; $script:fail++ }
function Pass($m) { Write-Host "  ok    $m" }

Write-Host "Smoke-testing the Windows client components"
Write-Host ""

foreach ($c in $components) {
	$file = $targets | Where-Object { (Split-Path $_ -Leaf) -ieq $c.File } | Select-Object -First 1
	if (-not $file) {
		if ($required -contains $c.Key) { Fail "$($c.File) is required but was not built" }
		else { Write-Host "  skip  $($c.File) not built" }
		continue
	}
	Write-Host "$($c.File)"

	$h = [LameSmoke.Native]::LoadLibraryW($file)
	if ($h -eq [IntPtr]::Zero) {
		Fail "does not load: Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
		continue
	}
	Pass "loads"
	foreach ($e in $c.Exports) {
		if ([LameSmoke.Native]::GetProcAddress($h, $e) -eq [IntPtr]::Zero) { Fail "$e does not resolve" }
		else { Pass "$e resolves" }
	}
	[void][LameSmoke.Native]::FreeLibrary($h)

	if (-not $dumpbin) {
		Fail "no dumpbin - the import table was not read, so self-containment is unchecked"
	} else {
		$dlls = & $dumpbin /nologo /dependents $file |
			Select-String -Pattern '^\s+(\S+\.dll)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
		$redist = $dlls | Where-Object { $_ -match '^(MSVCP|VCRUNTIME|MSVCR|api-ms-win-crt)' }
		if ($redist) { Fail "imports the redistributable runtime: $($redist -join ' ')" }
		else         { Pass "imports system DLLs only" }
	}
	Write-Host ""
}

if ($fail -gt 0) { Write-Host "$fail check(s) failed"; exit 1 }
Write-Host "all client components passed"
exit 0
