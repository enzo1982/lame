# LAME API Documentation

<img src="lame_logo_full.svg" alt="LAME" style="display:block;margin:0 auto 1.5em;width:100%;max-width:360px;height:auto;">

LAME (LAME Ain't an MP3 Encoder) is an MP3 encoding library and command-line
frontend, developed and maintained by [The LAME Project](https://lame.sf.net).

## Where to start

- The public encoding API is declared in `include/lame.h`. A typical encode
  session calls lame_init(), configures it, then lame_init_params(), feeds
  PCM through lame_encode_buffer() (or one of its buffer-format variants),
  and finishes with lame_encode_flush() and lame_close().
- `libmp3lame/` contains the encoder implementation itself. Most of it is
  internal - not part of the stable API or ABI.
- `frontend/` contains the `lame` command-line tool, built on top of the
  library.

## License

LAME is distributed under the GNU Lesser General Public License (LGPL),
version 2. See the `LICENSE` file in the repository root for the full text,
and `USAGE`/`API` for command-line and library usage details.
