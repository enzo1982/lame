#!/usr/bin/env python3
#
# Compare the output of two LAME builds, or of one build against stored
# reference output, over every option line of an options file.
#
# Part of the LAME distribution.  No warranty; see COPYING.

import getopt
import os
import shlex
import subprocess
import sys


def usage(mesg):
    print(mesg + os.linesep)

    print("Usage:" + os.linesep)

    print("Mode 1. Compare output of 'lame1' and 'lame2':")
    print("./lametest.py [options] options_file input.wav lame1 lame2" + os.linesep)

    print("Mode 2. Compare output of lame1 with reference solutions:")
    print("./lametest.py [options] options_file input.wav lame1" + os.linesep)

    print("Mode 3. Generate reference solutions using lame1:")
    print("./lametest.py -m options_file input.wav lame1" + os.linesep)

    print("options:")
    print("   -w   convert mp3's to wav's before comparison")

    sys.exit(0)


def run(argv):
    """Run a command, returning True when it succeeded."""
    try:
        return subprocess.call(argv) == 0
    except OSError as err:
        print("cannot run %s: %s" % (argv[0], err))
        return False


def read_file(name):
    """The contents of a file, or None when it cannot be read."""
    try:
        with open(name, "rb") as f:
            return f.read()
    except OSError:
        return None


def fdiff(name1, name2):
    """Count the bytes two files differ in, and the size of the larger one.

    A file that cannot be read gives a count of -1.
    """
    data1 = read_file(name1)
    data2 = read_file(name2)
    if data1 is None or data2 is None:
        return (-1, 0)

    # Bytes that differ where both files have one, plus the tail of the longer
    # file, which differs by being there at all.
    common = min(len(data1), len(data2))
    diff = sum(1 for i in range(common) if data1[i] != data2[i])
    diff += abs(len(data1) - len(data2))
    return (diff, max(len(data1), len(data2)))


def compare(name1, name2, decoder):
    """Compare two encoded files, decoding them first when decoder is set.

    Returns 1 when they are identical, 0 otherwise.
    """
    if decoder:
        print("converting mp3 to wav for comparison...")
        for name in (name1, name2):
            run([decoder, "--quiet", "--mp3input", "--decode", name])
        name1 = name1 + ".wav"
        name2 = name2 + ".wav"

    diff, size = fdiff(name1, name2)
    if diff == 0:
        print("output identical:  diff=%i  total=%i" % (diff, size))
        return 1
    if diff > 0:
        print("output different: diff=%i  total=%i  %2.0f%%"
              % (diff, size, 100 * float(diff) / size))
        return 0

    print("Error comparing files:")
    print("File 1: " + name1)
    print("File 2: " + name2)
    return 0


def encode(lame, options, input_file, output_file):
    """Encode input_file to output_file, replacing any previous output."""
    try:
        os.remove(output_file)
    except OSError:
        pass
    argv = [lame, "--quiet"] + shlex.split(options) + [input_file, output_file]
    print(" ".join(argv))
    run(argv)


def find_executable(name):
    """Whether name can be run, either as given or from PATH."""
    if os.path.isfile(name):
        return os.access(name, os.X_OK)
    for directory in os.environ.get("PATH", "").split(os.pathsep) + [os.curdir]:
        if os.access(os.path.join(directory, name), os.X_OK):
            return True
    return False


def main():
    try:
        optlist, args = getopt.getopt(sys.argv[1:], "wm")
    except getopt.error as val:
        usage("ERROR: " + str(val))

    decode = False
    lame2 = "none"
    for opt in optlist:
        if opt[0] == "-w":
            decode = True
        elif opt[0] == "-m":
            lame2 = "makeref"
            print(os.linesep + "Generating reference output")

    if len(args) < 3:
        usage("Not enough arguments.")
    if len(args) > 4:
        usage("Too many arguments.")

    if lame2 == "makeref":
        if len(args) != 3:
            usage("Too many arguments for -r/-m mode.")
    elif len(args) == 3:
        lame2 = "ref"

    options_file = os.path.normpath(os.path.expanduser(args[0]))
    input_file = os.path.normpath(os.path.expanduser(args[1]))
    lame1 = os.path.normpath(os.path.expanduser(args[2]))
    if len(args) >= 4:
        lame2 = os.path.normpath(os.path.expanduser(args[3]))

    if not os.access(options_file, os.R_OK):
        usage(options_file + " not readable")
    if not os.access(input_file, os.R_OK):
        usage(input_file + " not readable")
    if not find_executable(lame1):
        usage(lame1 + " is not executable")
    if lame2 not in ("ref", "makeref") and not find_executable(lame2):
        usage(lame2 + " is not executable")

    # Decoding back to wav uses the encoder under test, so that the comparison
    # does not depend on whichever lame happens to be installed.
    decoder = lame1 if decode else None

    basename = input_file.replace(".wav", "") + "." + os.path.basename(options_file)

    num_ok = 0
    n = 0
    with open(options_file) as f:
        for line in f:
            line = line.rstrip()
            if not line:
                continue
            n += 1
            name1 = "%s.%d.mp3" % (basename, n)
            name2 = "%s.%dref.mp3" % (basename, n)

            print()
            if lame2 == "ref":
                print("executable:      ", lame1)
                print("options:         ", line)
                print("input:           ", input_file)
                print("reference output:", name2)
                encode(lame1, line, input_file, name1)
                num_ok += compare(name1, name2, decoder)
            elif lame2 == "makeref":
                print("executable: ", lame1)
                print("options:    ", line)
                print("input:      ", input_file)
                print("output:     ", name2)
                encode(lame1, line, input_file, name2)
            else:
                print("executable:  ", lame1)
                print("executable2: ", lame2)
                print("options:     ", line)
                print("input:       ", input_file)
                encode(lame1, line, input_file, name1)
                encode(lame2, line, input_file, name2)
                num_ok += compare(name1, name2, decoder)

    if lame2 != "makeref":
        print(os.linesep + "Number of tests which passed: ", num_ok)
        print("Number of tests which failed: ", n - num_ok)
        return 0 if num_ok == n else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
