# LAME API Documentation

<img src="lame_logo_full.svg" alt="LAME" style="display:block;margin:0 auto 1.5em;width:100%;max-width:360px;height:auto;">

LAME (LAME Ain't an MP3 Encoder) is an MP3 encoding library and command-line
frontend, developed and maintained by [The LAME Project](https://lame.sf.net).

## Where to start

- The @ref api describes everything a program may call. It is declared in
  `include/lame.h`, the only header LAME installs. A program calls
  lame_init(), sets the parameters and calls lame_init_params(). It then
  passes PCM to lame_encode_buffer() or one of its variants, and ends with
  lame_encode_flush() and lame_close().
- `libmp3lame/` contains the encoder itself. Most of it is internal and not
  part of the stable API or ABI.
- `frontend/` contains the `lame` command-line tool, which uses the library.
- `mp3x` is the optional GTK4 frame analyzer. It encodes a file frame by
  frame and shows the spectrum, the psychoacoustic decisions and the decoded
  result. This is the quickest way to see what an encoding setting does. Its
  manual page, `mp3x(1)`, describes how to use it.

## License

LAME is distributed under the GNU Library General Public License (LGPL),
version 2 or any later version. The full text is in the `COPYING` file in the
repository root. The `LICENSE` file covers common questions about using LAME
in other programs. See `USAGE`/`API` for command-line and library usage
details.
