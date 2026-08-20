---
name: t850-crash-debugging
description: "Use when T850 crashes, asserts, exits with a native exception code, shows Abort/Retry/Ignore, needs a CDB call stack, requires dump analysis, or must run unattended under WinDbg/CDB."
argument-hint: "State executable, arguments, configuration/platform, and whether reproducing live or analyzing a dump."
---

# T850 Native Crash Debugging with CDB

Use this workflow for Windows native crashes and Debug CRT assertions. Capture the stack before changing code.

## 1. Establish Paths

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
$Output = Join-Path $SourceRoot 'bin\x64\Debug'
$Exe = Join-Path $Output 'DayScene.exe'
```

Run from the executable output directory so runtime assets and DLLs resolve.

## 2. Install and Locate CDB

```powershell
winget install --id Microsoft.WinDbg --exact `
  --accept-package-agreements --accept-source-agreements --silent

$Package = Get-AppxPackage Microsoft.WinDbg
$Cdb = Join-Path $Package.InstallLocation 'amd64\cdb.exe'
if (-not (Test-Path $Cdb)) { throw 'x64 CDB was not found' }
& $Cdb -version
```

Use `x86\cdb.exe` or `arm64\cdb.exe` only for a matching target architecture.

## 3. Prevent the CRT Assertion Dialog

DayScene and T8ditor call `t850::InstallUnattendedCrtReportHook()` at the first line of `main` in Windows Debug builds.

The hook is implemented in:

```text
Framework/include/debug/CrashDiagnostics.h
Framework/src/debug/CrashDiagnostics.cpp
```

Its CRT report hook handles `_CRT_ASSERT`, sets `*returnValue = 1` (Retry), and returns `TRUE`. This suppresses Abort/Retry/Ignore while preserving `_CrtDbgBreak()`, so CDB receives `0x80000003` immediately.

For another executable, call this before configuration, logging, threads, windows, or renderer initialization:

```cpp
int main(int argc, char** argv) {
  t850::InstallUnattendedCrtReportHook();
  // ...
}
```

Do not automate clicking Retry. Desktop-dialog automation is focus-sensitive and can hang unattended runs. Fix the process startup instead.

## 4. Reproduce and Capture the Stack

Build Debug first:

```powershell
Set-Location $SourceRoot
.\scripts\build.ps1 -Config Debug -Platform x64
```

Launch under CDB:

```powershell
Set-Location $Output
$SymbolCache = Join-Path $RepoRoot '.symbols'
$Symbols = "srv*$SymbolCache*https://msdl.microsoft.com/download/symbols;$Output;$(Join-Path $SourceRoot 'Lib\Debug\x64')"
$CdbLog = Join-Path $Output 'logs\crash-cdb.log'
$ProgramArgs = @(
  '--api','d3d11',
  '--scene','5',
  '--width','1280','--height','720',
  '--logLevel','debug',
  '--logFile','logs\crash-engine.log'
)
$ExceptionHandler = '.echo ===== APPLICATION EXCEPTION =====; .exr -1; .ecxr; r; kv; !analyze -v; q'
$CdbCommands = 'sxe -c "' + $ExceptionHandler + '" 80000003; ' +
               'sxe -c "' + $ExceptionHandler + '" av; g'

& $Cdb -g -G -lines -y $Symbols -logo $CdbLog `
  -c $CdbCommands `
  $Exe @ProgramArgs
```

`-g` skips CDB's loader breakpoint and `-G` ignores the normal process-termination breakpoint. The `sxe -c` handlers emit analysis only for a real CRT breakpoint or access violation. Do not append analysis commands after a plain `g`: a normal timed `exit(0)` would otherwise be mislabeled as an application exception.

Require these before diagnosing:

- `ExceptionCode` or assertion text;
- first application-owned frame;
- source file and line;
- at least five caller frames;
- engine log tail from the same process.

Ignore unrelated DLL loader messages unless they appear in the faulting stack.

## 5. Read the Important Stack Lines

```powershell
Select-String $CdbLog -Pattern `
  'APPLICATION EXCEPTION|ExceptionCode|Assertion|Access violation|STACK_TEXT|DayScene!|T8ditor!|FAILURE_BUCKET' `
  -Context 0,3
```

Classify common native exit codes:

| Code | Meaning |
|---|---|
| `0x80000003` | breakpoint, commonly Debug CRT/STL assertion |
| `0xC0000005` | access violation |
| `0xC0000409` | stack buffer overrun/fail-fast |
| `0xE06D7363` | unhandled MSVC C++ exception |

A missing frame dump after a timed capture is not itself a diagnosis. Check the native exit code and CDB stack.

## 6. Analyze an Existing Dump

```powershell
$Dump = 'C:\path\DayScene.dmp'
& $Cdb -z $Dump -lines -y $Symbols `
  -c '.reload; !analyze -v; .exr -1; .ecxr; r; kv; q'
```

If symbols do not resolve:

```text
.sympath
!sym noisy
.reload /f DayScene.exe
lmvm DayScene
```

Verify that the PDB timestamp/GUID matches the executable that produced the dump.

## 7. Fix and Prove It

1. Fix the first invalid engine assumption, not the STL/CRT assertion site.
2. Add a defensive boundary check when malformed runtime state could recur.
3. Rebuild the same Debug target.
4. Rerun the identical CDB command without interaction.
5. Require normal process exit or the expected timed dump.
6. Search the new CDB log for assertion/exception markers.
7. Run the focused test and then the broader gate appropriate to the touched subsystem.

A successful unattended timed-dump proof looks like:

```text
CDB exit 0
RT_Dump_BackBuffer.ppm exists
snapshot.json exists
no Assertion / 80000003 / Access violation in CDB log
```

Check only the explicit marker for automated runs:

```powershell
$Exceptions = @(Select-String $CdbLog -Pattern '===== APPLICATION EXCEPTION =====')
if ($Exceptions.Count) { throw "CDB captured $($Exceptions.Count) application exception(s)" }
```

## 8. Reporting

Report:

- exception code;
- exact top application stack frames;
- root invalid assumption;
- files changed;
- identical CDB rerun result;
- focused build/test/dump result;
- any remaining backend-specific risk.

Related documentation:

- `documentation/debug/diagnostics.md`
- `documentation/debug/visual-regression.md`
- `documentation/testing/verification.md`
