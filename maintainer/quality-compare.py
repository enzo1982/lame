#!/usr/bin/env python3
#
# quality-compare.py - score the perceptual quality of two LAME builds.
#
# Some changes are meant to alter the output: a psychoacoustic tweak, a
# quantization fix. The bitstream check that guards every other change says
# nothing useful about those - what matters is whether what a listener hears
# got better, worse, or stayed put. This encodes the same reference audio with
# a baseline and a candidate build, decodes both, and scores each against the
# original with PEAQ, reporting the difference per track.
#
# PEAQ (ITU-R BS.1387) returns an Objective Difference Grade: 0 is
# imperceptible, -1 perceptible but not annoying, -4 very annoying. Only the
# difference between the two builds is meaningful here. No free implementation
# meets the ITU conformance requirements, this one included, so treat a score
# as a relative signal and not as a measurement of absolute quality.
#
# setup-quality-corpus.sh prepares the reference audio this reads.
#
# Part of the LAME distribution.  No warranty; see COPYING.

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


def die(msg):
    print("quality-compare.py: " + msg, file=sys.stderr)
    sys.exit(1)


# --- prerequisites ---------------------------------------------------------

def find_peaq():
    """Locate the PEAQ scorer, explaining where to get it if it is absent."""
    peaq = shutil.which("peaq")
    if not peaq:
        die(
            "peaq not found - it is the scorer this depends on.\n"
            "  GstPEAQ implements ITU-R BS.1387 and is not packaged anywhere;\n"
            "  build it from https://github.com/HSU-ANT/gstpeaq (LGPLv2):\n"
            "    apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \\\n"
            "                gstreamer1.0-plugins-base gstreamer1.0-tools gtk-doc-tools\n"
            "    git clone https://github.com/HSU-ANT/gstpeaq && cd gstpeaq\n"
            "    touch ChangeLog && autoreconf -fi && ./configure --disable-gtk-doc\n"
            "    make -C src && sudo make -C src install && sudo ldconfig\n"
            "  (the doc/ directory wants a generated man page and does not build;\n"
            "   only src/ is needed. touch ChangeLog satisfies automake's GNU mode.)"
        )
    return peaq


def peaq_env():
    """The environment peaq needs to find its plugin.

    GstPEAQ installs under /usr/local by default, which is not on GStreamer's
    default search path, and the element then fails to instantiate at run time
    rather than at install time.
    """
    env = os.environ.copy()
    extra = "/usr/local/lib/gstreamer-1.0"
    if os.path.isdir(extra):
        path = env.get("GST_PLUGIN_PATH", "")
        if extra not in path.split(os.pathsep):
            env["GST_PLUGIN_PATH"] = (path + os.pathsep + extra) if path else extra
    return env


def check_ffmpeg():
    if not shutil.which("ffmpeg"):
        die("ffmpeg not found - it decodes the encoded files back to WAV.\n"
            "  Debian/Ubuntu: apt install ffmpeg   FreeBSD: pkg install ffmpeg")


# --- locating the encoders -------------------------------------------------

def matrix_cells(master):
    """The cells of a build matrix, as <compiler>/<cell> paths."""
    out = []
    for root, dirs, files in os.walk(master):
        if "config.status" in files:
            rel = os.path.relpath(root, master).replace(os.sep, "/")
            if rel.count("/") == 1:
                out.append(rel)
    return sorted(out)


def find_encoder(master, cell):
    """The encoder of a cell, and the library path it must run with.

    With dynamic frontends libtool leaves a wrapper script named "lame" and
    puts the executable in .libs. The executable binds to an installed
    libmp3lame where the host has one, so it is run with the cell's own
    library directory ahead of that. See perf-compare.sh, which has the same
    problem for the same reason.
    """
    d = os.path.join(master, cell, "frontend")
    lib = os.path.join(master, cell, "libmp3lame", ".libs")
    if not os.path.isdir(d):
        die("'%s' not found - is the cell built?" % d)
    for name in ("lame", "lame.exe"):
        f = os.path.join(d, name)
        if not os.path.isfile(f):
            continue
        with open(f, "rb") as fh:
            wrapper = fh.read(2) == b"#!"
        if wrapper:
            for real in (os.path.join(d, ".libs", n) for n in ("lame", "lame.exe")):
                if os.access(real, os.X_OK):
                    return real, (lib if os.path.isdir(lib) else None)
        return f, None
    die("no encoder under '%s' - is the cell built?" % d)


def resolve_cell(master, cell):
    if os.path.isdir(os.path.join(master, cell)):
        return cell
    hits = [c for c in matrix_cells(master) if c.endswith("/" + cell)]
    if len(hits) == 1:
        return hits[0]
    if not hits:
        die("no cell '%s' in %s. Available:\n  %s"
            % (cell, master, "\n  ".join(matrix_cells(master)) or "(none built)"))
    die("'%s' is ambiguous in %s:\n  %s" % (cell, master, "\n  ".join(hits)))


# --- the work --------------------------------------------------------------

ODG_RE = re.compile(r"Objective Difference Grade:\s*(-?[\d.]+|-?nan)", re.I)


def score(peaq, env, reference, decoded):
    """The ODG of decoded against reference, or None when PEAQ gave up."""
    try:
        p = subprocess.run([peaq, reference, decoded], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           universal_newlines=True)
    except OSError as err:
        die("cannot run peaq: %s" % err)
    m = ODG_RE.search(p.stdout)
    if not m:
        return None
    try:
        return float(m.group(1))
    except ValueError:
        # PEAQ reports nan on degenerate input, e.g. a pure tone encoded so
        # well that its model has nothing to divide by.
        return None


def encode_and_score(lame, libpath, opts, ref, work, tag, peaq, env):
    mp3 = os.path.join(work, tag + ".mp3")
    wav = os.path.join(work, tag + ".wav")
    e = env.copy()
    if libpath:
        e["LD_LIBRARY_PATH"] = libpath + os.pathsep + e.get("LD_LIBRARY_PATH", "")
    rc = subprocess.call([lame, "--quiet"] + opts.split() + [ref, mp3], env=e,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rc != 0:
        return None
    rc = subprocess.call(["ffmpeg", "-nostdin", "-loglevel", "error",
                          "-i", mp3, "-ar", "48000", "-ac", "2",
                          "-c:a", "pcm_s16le", "-y", wav],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rc != 0:
        return None
    return score(peaq, env, ref, wav)


def glue_values(argv, flags):
    """Join "-e -V4" into "-e=-V4".

    Every encoder option starts with a dash, and argparse takes a value that
    does for an option of its own and refuses the argument.
    """
    out = []
    i = 0
    while i < len(argv):
        if argv[i] in flags and i + 1 < len(argv):
            out.append("%s=%s" % (argv[i], argv[i + 1]))
            i += 2
        else:
            out.append(argv[i])
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser(
        description="Score the perceptual quality of two LAME builds against "
                    "a reference corpus.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="A negative delta means the candidate sounds worse. Report the\n"
               "per-track numbers, not just the mean: a regression in one hard\n"
               "case averages away across a corpus.")
    ap.add_argument("-o", "--old", required=True, metavar="DIR",
                    help="baseline build matrix")
    ap.add_argument("-n", "--new", required=True, metavar="DIR",
                    help="candidate build matrix")
    ap.add_argument("-c", "--cell", default="gcc/full",
                    help="matrix cell to compare (default: gcc/full)")
    ap.add_argument("-d", "--corpus", required=True, metavar="DIR",
                    help="corpus from setup-quality-corpus.sh")
    ap.add_argument("-e", "--encoder-options", default="-V2", metavar="ARGS",
                    help="encoder options to score at (default: -V2)")
    ap.add_argument("-f", "--options-file", metavar="FILE",
                    help="score every option line of this file instead, e.g. "
                         "one of test/*.op")
    ap.add_argument("-t", "--tolerance", type=float, default=0.05,
                    help="flag a track whose ODG drops by more than this "
                         "(default: 0.05; calibrate it by running the harness "
                         "twice on one build to measure its own noise)")
    args = ap.parse_args(glue_values(sys.argv[1:],
                                     {"-e", "--encoder-options"}))

    peaq = find_peaq()
    check_ffmpeg()
    env = peaq_env()

    refdir = os.path.join(args.corpus, "ref")
    if not os.path.isdir(refdir):
        die("no ref/ in '%s' - run setup-quality-corpus.sh first." % args.corpus)
    refs = sorted(f for f in os.listdir(refdir) if f.endswith(".wav"))
    if not refs:
        die("no reference audio in '%s'." % refdir)

    if args.options_file:
        with open(args.options_file) as f:
            optsets = [l.strip() for l in f if l.strip()]
    else:
        optsets = [args.encoder_options]

    old_cell = resolve_cell(args.old, args.cell)
    new_cell = resolve_cell(args.new, args.cell)
    old_bin, old_lib = find_encoder(args.old, old_cell)
    new_bin, new_lib = find_encoder(args.new, new_cell)

    print("baseline : %s" % old_bin)
    print("candidate: %s" % new_bin)
    print("corpus   : %s (%d tracks)" % (refdir, len(refs)))
    print("scorer   : %s" % peaq)
    print()

    regressions = []
    unscored = []
    for opts in optsets:
        print("=== %s ===" % opts)
        print("  %-10s %8s %8s %8s" % ("track", "baseline", "candidate", "delta"))
        deltas = []
        for r in refs:
            ref = os.path.join(refdir, r)
            name = os.path.splitext(r)[0]
            with tempfile.TemporaryDirectory() as work:
                a = encode_and_score(old_bin, old_lib, opts, ref, work, "old", peaq, env)
                b = encode_and_score(new_bin, new_lib, opts, ref, work, "new", peaq, env)
            if a is None or b is None:
                print("  %-10s %8s %8s %8s" % (name, "-", "-", "unscored"))
                unscored.append((opts, name))
                continue
            d = b - a
            deltas.append(d)
            flag = ""
            if d < -args.tolerance:
                flag = "  <-- worse"
                regressions.append((opts, name, a, b, d))
            print("  %-10s %8.3f %8.3f %+8.3f%s" % (name, a, b, d, flag))
        if deltas:
            mean = sum(deltas) / len(deltas)
            print("  %-10s %8s %8s %+8.3f  (mean of %d)"
                  % ("", "", "", mean, len(deltas)))
        print()

    if unscored:
        print("%d track/option pair(s) could not be scored; PEAQ returns nan on "
              "material its model cannot handle." % len(unscored))
    if regressions:
        print("%d track(s) dropped by more than %.3f:" % (len(regressions), args.tolerance))
        for opts, name, a, b, d in regressions:
            print("  %s  %s: %.3f -> %.3f (%+.3f)" % (opts, name, a, b, d))
        return 1
    print("No track dropped by more than %.3f." % args.tolerance)
    return 0


if __name__ == "__main__":
    sys.exit(main())
