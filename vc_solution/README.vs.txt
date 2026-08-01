# README for compiling LAME with Visual Studio

This document describes how to compile the LAME projects using Visual Studio.
Any edition will do, even the free Community edition. Be sure to install the
"Desktop development with C++" workload. The solutions use the .slnx format,
which Visual Studio 2022 17.14 (or the 17.13 Build Tools) and later understand;
the project files still open in older Visual Studio, which retargets them to an
installed toolset if prompted.

## Projects

There are two solution files in the "lame/vc_solution" folder that can be
opened. The solution "vs_lame.slnx" contains the following projects:

- lame: The lame.exe command line executable
- libmp3lame: The dynamic library libmp3lame.dll
- libmp3lame-static: The static library variant of the above
- mp3rtp: command line tool to stream mp3 via RTP protocol
- mp3x: mp3 frame analyzer tool using GTK 4 (see below)

The solution "vs_lame_clients.slnx" contains several more projects:

- ACM, ADbg, tinyxml: Ancient Windows "Audio Codec Manager"
- lame_DirectShow: DirectShow filter
- lame_test: Test program

In the two solutions there are several configurations that can be used to
compile different flavors of LAME libraries and executables:

- Debug: Builds without optimization, but debugging support
- Release: Optimization build

The vector routines are built into every configuration and selected at run
time from the CPU's own feature bits, so no separate configuration is needed
to get them, and a binary built with them still runs on a machine without
SSE2. "lame --verbose" reports what the machine it is running on actually got.
No assembler is needed to build any configuration.

The vs_lame.slnx solution has both Win32 and x64 platforms configured, in
order to compile lame.exe and the libmp3lame.dll for 32-bit or 64-bit target
platforms. The output folder also has separate folders for the two platforms.
Note that mp3rtp and mp3x are not compiled in x64.

## External libraries and tools

For some projects, external libraries or tools are necessary for successful
compilation. These can be configured using .props files or the Property Manager
window of Visual Studio (View > Other Windows > Property Manager). The props
files have a "User Macros" page where the variable values can be changed.

### libsndfile

LAME can be compiled with the libsndfile library for audio input. The Windows
builds are available here:
https://libsndfile.github.io/libsndfile/

Download the win32 or win64 zip archive and extract it into any folder.

Open the file "lame/vc_solution/vs_libsndfile_config.props" and edit the
following  two user macro parameters:

- The value of `HaveLibsndfile` can be set to false or true, and specifies if
  the libsndfile library is available and used in lame.exe
- `LibsndfilePath` specifies the path to the root folder of libsndfile, ending
  the path with a backslash. The folder should contain the `include`, `lib`
  and `bin` folders.

As described above, you can also use the Property Manager view to change the
values.

Note that when compiling for the x64 platform, you have to use the 64-bit
version of libsndfile. Alternatively you can use the `HaveLibsndfile` as is
(the default value is `.\libsndfile\$(Platform)\`) and extract the zip
archives for 32-bit and 64-bit into the "vc_solution\libsndfile\Win32\" and
"vc_solution\libsndfile\x64\" folders.

### mpg123

From LAME version 3.100.1 on, LAME supports decoding using the external mpg123
library, which is a mature fork of the internally used mpglib library. The
latest binaries for Win32 and x64 are available here:
https://mpg123.de/

Open the file "lame/vc_solution/vs_libmpg123_config.props" and edit the
following  two user macro parameters:

- The value of `HaveMpg123` can be set to false or true, and specifies if
  the libmpg123 library is available and used in lame.exe and libmp3lame.dll.
  When set to false, decoding is not available in LAME. This includes
  calculating accurate Replaygain by decoding the just encoded data on-the-fly.
- `Mpg123Path` specifies the path to the root folder of mpg123, ending
  the path with a backslash. The folder should contain the `mpg123.h`,
  `libmpg123-0.def` and `libmpg123-0.dll` files, among others. The build
  makes its own import library from the `.def`, so none is needed there and
  nothing is written into that folder.

As described above, you can also use the Property Manager view to change the
values.

Note that when compiling for the x64 platform, you have to use the 64-bit
version of libmpg123. Alternatively you can use the `HaveMpg123` as is
(the default value is `.\mpg123\$(Platform)\`) and extract the zip
archives for 32-bit and 64-bit into the "vc_solution\mpg123\Win32\" and
"vc_solution\mpg123\x64\" folders.

### GTK 4

The mp3x graphical frame analyzer uses GTK 4 for the user interface, and needs
version 4.10 or later. GTK 4 is not distributed as a ready-made MSVC SDK; the
practical way to get one is gvsbuild, which builds GTK and its dependencies
with MSVC and installs them under a single prefix:
https://github.com/wingtk/gvsbuild

Its releases page also publishes the finished build as an archive, whose root
already is such a prefix - `include`, `lib` and `bin` directly inside - so
unpacking it is the whole setup and no source build is needed.

Extract it into the "vc_solution\gtk4\x64\" folder, the same way as mpg123 and
libsndfile above. Then open the file "lame/vc_solution/vs_gtk_config.props" and
edit the following two user macro parameters:

- The value of `HaveGtk` can be set to false or true, and specifies whether
  GTK 4 is available and mp3x can be built. mp3x is not selected for building
  in the solution by default, since it needs this build.
- `Gtk4Path` specifies the install prefix (the default value is
  `.\gtk4\$(Platform)`) if you keep GTK 4 elsewhere. Do not end it with a
  backslash: the paths built from it supply their own separator, so that the
  value can be quoted against spaces.

The build copies the DLLs from the prefix's `bin` folder next to mp3x.exe, so
the analyzer runs without a separate PATH setting.

mp3x is built for x64 only. gvsbuild publishes GTK 4 for x64 and for no other
architecture, so a 32-bit mp3x would have nothing to link against. For the
same reason Makefile.MSVC, which targets 32-bit x86, does not build mp3x at
all; use this solution or the autotools build for the analyzer.

As described above, you can also use the Property Manager view to change the
values.

Note that compiling mp3x for 64-bit platforms is currently not available.

### Windows SDK 7.1

For the DirectShow filter, the Windows SDK 7.1 is needed, especially the
samples folder where a multimedia base class library must be compiled before.

Download the Windows SDK 7.1 installer from here:
https://www.microsoft.com/en-us/download/details.aspx?id=8279
(or search for "Microsoft Windows SDK for Windows 7 and .NET Framework 4",
version 7.1)

When starting the web setup, you can choose the installation options. Only the
"Samples" under "Windows Native Code Development" is actually necessary.

Open the file "lame/vc_solution/vs_win71sdk_config.props" and edit the
`Win71SdkPath` in the first few lines of the file, ending the path with a
backslash. As described above, you can also use the Property Manager view to
change the values.

In the Win71SdkPath path, locate the solution file
"Samples\multimedia\directshow\baseclasses\baseclasses.sln", convert it from
the old Visual Studio project format and compile the "Debug_MBCS" and
"Release_MBCS" configurations. The resulting files strmbasd.lib and
strmbase.lib are used by the lame_DirectShow project for linking.
