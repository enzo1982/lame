#!/usr/bin/env python3
#
# coverage-mkinputs.py - generate the synthetic input files the coverage
# workload refers to by placeholder name.
#
# The point of this zoo is reaching input-parsing branches that a single
# well-formed stereo WAV never touches: the float sample path, the AIFF
# reader, the WAVE_FORMAT_EXTENSIBLE validation, the mono-only encoder paths,
# and the short-read rejections in the WAV/AIFF header parsers. Each file
# exists because some branch needs it, and the comment on each says which.
#
# Part of the LAME distribution.  No warranty; see COPYING.

import argparse
import math
import os
import struct

RATE = 44100
FRAMES = 2000


def pcm16(channels):
    """Signed 16-bit PCM, interleaved, with content on every channel."""
    out = []
    for i in range(FRAMES):
        left = int(20000 * math.sin(i * 0.02))
        right = int(20000 * math.cos(i * 0.03))
        out.append(struct.pack("<h", left) if channels == 1
                   else struct.pack("<hh", left, right))
    return b"".join(out)


def write_wav(path, channels=2, bits=16, fmt_tag=1, data=None,
              cbsize=None, valid_bits=None, blockalign=None, avgbytes=None):
    """A RIFF/WAVE file, deliberately parameterised so malformed variants are
    expressible: block align and average byte rate can be set inconsistently,
    and the extensible header's cbSize / wValidBitsPerSample can be made to
    violate the invariants the reader checks."""
    if data is None:
        data = pcm16(channels)
    align = channels * bits // 8 if blockalign is None else blockalign
    byterate = RATE * channels * bits // 8 if avgbytes is None else avgbytes

    if fmt_tag == 0xFFFE:                       # WAVE_FORMAT_EXTENSIBLE
        ext = struct.pack("<HI", valid_bits, 0x3)        # validbits + chanmask
        ext += struct.pack("<H", 1) + b"\x00" * 14       # SubFormat = PCM GUID
        fmt = struct.pack("<HHIIHH", fmt_tag, channels, RATE, byterate, align, bits)
        fmt += struct.pack("<H", cbsize) + ext
    else:
        fmt = struct.pack("<HHIIHH", fmt_tag, channels, RATE, byterate, align, bits)

    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(data)) + data
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def write_float_wav(path):
    """32-bit IEEE float WAV (format tag 3).

    Values are chosen to cover the branches of the float->int conversion:
    zero, +/-1 exactly, beyond +/-1 (clamped), just inside +/-1, and the
    smallest representable step, followed by a sweep that exercises rounding
    on both signs. A 16-bit integer WAV never reaches this code at all.
    """
    vals = []
    edges = [0.0, 1.0, -1.0, 2.0, -2.0, 1.0 - 2 ** -24, -(1.0 - 2 ** -24),
             2 ** -24, -2 ** -24, 0.5, -0.5, 0.9999999, -0.9999999]
    for v in edges:
        vals += [v, -v]
    for i in range(20000):
        vals.append(math.sin(i * 0.01) * (1.0 - 1e-7))
        vals.append(math.cos(i * 0.013) * (1.0 - 1e-7))

    data = b"".join(struct.pack("<f", v) for v in vals)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 3, 2, RATE, RATE * 8, 8, 32)
    hdr += b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f:
        f.write(hdr + data)


def extended80(value):
    """IEEE 754 80-bit extended, as AIFF stores the sample rate."""
    if value == 0:
        return b"\x00" * 10
    exponent = int(math.floor(math.log2(value)))
    mantissa = int(value / (2.0 ** exponent) * (2 ** 63))
    return struct.pack(">HQ", exponent + 16383, mantissa)


def write_aiff(path, channels=2):
    """AIFF, big-endian, so the AIFF branch of the reader is exercised.

    The sample data is byte-swapped relative to the WAV files: AIFF is
    big-endian, and a reader that ignores that produces noise rather than
    failing, which is exactly why it needs its own coverage.
    """
    frames = FRAMES
    samples = []
    for i in range(frames):
        left = int(20000 * math.sin(i * 0.02))
        right = int(20000 * math.cos(i * 0.03))
        samples.append(struct.pack(">h", left) if channels == 1
                       else struct.pack(">hh", left, right))
    data = b"".join(samples)

    comm = struct.pack(">hIh", channels, frames, 16) + extended80(RATE)
    ssnd = struct.pack(">II", 0, 0) + data
    body = b"AIFF"
    body += b"COMM" + struct.pack(">I", len(comm)) + comm
    body += b"SSND" + struct.pack(">I", len(ssnd)) + ssnd
    with open(path, "wb") as f:
        f.write(b"FORM" + struct.pack(">I", len(body)) + body)


def write_truncations(outdir, source):
    """Headers cut short at the points where the parsers give up.

    These lengths are not arbitrary: they are the measured early-exit points
    of the WAV/AIFF header readers, where each successive field read is the
    first one to fail. A file truncated mid-header must be rejected, and the
    rejection path is only reachable with an input like this.
    """
    with open(source, "rb") as f:
        full = f.read()
    made = {}
    for n in (8, 20, 30, 44):
        path = os.path.join(outdir, "trunc%d.wav" % n)
        with open(path, "wb") as f:
            f.write(full[:n])
        made["TRUNC%d" % n] = path
    return made


# A minimal but structurally valid JPEG: SOI, APP0/JFIF, a comment, EOI. Only
# needs to satisfy the album-art type sniffing, not decode to an image.
MINIMAL_JPEG = (
    b"\xff\xd8"
    b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    b"\xff\xfe" + struct.pack(">H", 2 + len(b"lame coverage")) + b"lame coverage"
    b"\xff\xd9"
)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("outdir", help="directory to write the input files into")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    def p(name):
        return os.path.join(args.outdir, name)

    made = {}

    # The canonical well-formed input: stereo, 16-bit, plain PCM.
    write_wav(p("stereo.wav"))
    made["WAV"] = p("stereo.wav")

    # Mono. Several encoder paths are mono-only, including the block-type
    # decision that only fills one channel of its per-channel array.
    write_wav(p("mono.wav"), channels=1)
    made["MONOWAV"] = p("mono.wav")

    # WAVE_FORMAT_EXTENSIBLE: one valid, three violating an invariant the
    # reader checks (over-large valid-bits, undersized cbSize) or merely
    # inconsistent (block align / byte rate disagree with the format, which
    # real encoders get wrong often enough that it must stay accepted).
    write_wav(p("ext_ok.wav"), fmt_tag=0xFFFE, cbsize=22, valid_bits=16)
    made["WAVEXT"] = p("ext_ok.wav")
    write_wav(p("ext_badbits.wav"), fmt_tag=0xFFFE, cbsize=22, valid_bits=32)
    made["WAVEXTBAD"] = p("ext_badbits.wav")
    write_wav(p("ext_badcb.wav"), fmt_tag=0xFFFE, cbsize=2, valid_bits=16)
    made["WAVEXTCB"] = p("ext_badcb.wav")
    write_wav(p("sloppy.wav"), blockalign=3, avgbytes=12345)
    made["WAVSLOPPY"] = p("sloppy.wav")

    write_float_wav(p("float.wav"))
    made["FLOATWAV"] = p("float.wav")

    write_aiff(p("stereo.aiff"))
    made["AIFF"] = p("stereo.aiff")

    # Headerless PCM, for the raw-input path (-r), which bypasses format
    # detection entirely.
    with open(p("raw.pcm"), "wb") as f:
        f.write(pcm16(2))
    made["RAW"] = p("raw.pcm")

    made.update(write_truncations(args.outdir, p("stereo.wav")))

    with open(p("art.jpg"), "wb") as f:
        f.write(MINIMAL_JPEG)
    made["JPG"] = p("art.jpg")

    # The map the runner reads to resolve %PLACEHOLDER% tokens.
    with open(p("inputs.map"), "w") as f:
        for key in sorted(made):
            f.write("%s\t%s\n" % (key, made[key]))
            print("%-12s %s" % (key, made[key]))


if __name__ == "__main__":
    main()
