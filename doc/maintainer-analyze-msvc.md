# MSVC static analysis {#maintainer_analyze_msvc}

`maintainer/analyze-msvc.ps1` runs the static analyzer of the Microsoft C/C++
compiler (`/analyze`) over the MSBuild projects in `vc_solution`. It compares
what the analyzer finds with a baseline in the source tree, and fails when
there is more.

The project files compile with `/analyze-`. The script turns the analyzer on
for its own run only.
It builds each project with a property sheet that MSBuild reads before the C++
targets, and changes no file in the tree.

```
powershell -File maintainer\analyze-msvc.ps1
powershell -File maintainer\analyze-msvc.ps1 -GtkDir C:\gtk -DShowBaseClassesDir C:\baseclasses
```

It needs Visual Studio or the Build Tools with the C++ workload. It runs in
Windows PowerShell 5.1 and in PowerShell 7.

## What it builds

Without `-Project`, the script builds every project of the tree on every
platform that the project has, in the Release configuration. A run takes about
ten minutes. Two projects need sources that the tree does not contain:

| Project       | Platform | Needs                                            |
|---------------|----------|--------------------------------------------------|
| mp3x          | x64      | `-GtkDir`, the GTK 4 install prefix              |
| DirectShow filter | Win32 | `-DShowBaseClassesDir`, the folder with `streams.h` |

Without the option, the project is reported as skipped, and the files that
only it compiles are not analysed.

With `-Project`, the script builds that one project, on `-Platform` (default
Win32). `-Property` passes MSBuild properties to it, as one string such as
`"HaveMpg123=true;Gtk4Path=C:\gtk"`. It is one string and not an array, because
`powershell -File` passes only the first element of an array.

## What it reports

The script reads the analyzer's warnings (C6000 to C6999 and C28000 to C28999)
from the build logs. It counts a finding once, even when several projects
compile its file. A finding in code that the tree does not own (the Windows
SDK, GTK, the DirectShow base classes, TinyXML in `ACM/tinyxml`) is counted
apart and not compared.

The script stops with exit status 2, and reports no result, when

- a project does not have the platform that was asked for,
- a build fails,
- a log has no compile line, so nothing was compiled, or
- a compile line does not carry `/analyze`, so the analyzer did not run.

Otherwise the exit status is 1 if a finding is above the baseline, and 0 if
none is. The build logs stay in `-LogDir` (default: a new folder in `%TEMP%`),
and every outcome prints their path.

## The baseline

`maintainer/analyze/baseline.txt` lists the findings that are known and kept.
A line holds a file, relative to the source tree, a warning code, the number
of lines in the file with that code, and the reason the findings stay:

```
frontend/get_audio.c C6001 3 each read is of an entry that the loop before wrote
```

The line numbers are not part of it, so a change elsewhere in the file does not
change the baseline. One more line with the same code in the same file is above
the baseline, and the script prints the line numbers and the analyzer's text of
every line of that key.

A new finding is either a defect or not:

- A defect is fixed, and the finding goes away.
- A finding that is not a defect goes into the baseline, with the reason. That
  is a false positive, where the code already ensures what the analyzer cannot
  see, or a construct that is intended, such as a large array on the stack.

`-UpdateBaseline` writes the keys of the run to the baseline file. It keeps the
reason of each key that is already there, and writes "untriaged" for a new key.
So after an update, every "untriaged" line still needs its reason.

A key that has fewer lines than the baseline is reported as `GONE`. Lower the
count in the baseline then, or remove the line, so that a new finding of the
same kind is not hidden by the old number. When a project was skipped, a
`GONE` line can also mean that no build compiled the file.
