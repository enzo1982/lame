#!/usr/bin/env python3
#
# coverage-report.py - turn per-run coverage data into the two answers the
# sanitizer work needs: which configuration cells are worth running, and which
# invocations are worth running in them.
#
# Input is one or more cell output directories from coverage-run.sh, each
# holding runs/*.info (one per invocation) and baseline.info (every line the
# compiler emitted for that cell, at zero hits).
#
# Part of the LAME distribution.  No warranty; see COPYING.

import argparse
import os
import sys
from collections import defaultdict


def parse_info(path):
    """Read an lcov .info file.

    Returns (covered, present): sets of (source_file, line). "present" is
    every line with a DA record, "covered" only those with a nonzero count.
    """
    covered = set()
    present = set()
    current = None
    with open(path, "r", errors="replace") as f:
        for line in f:
            if line.startswith("SF:"):
                current = line[3:].strip()
            elif line.startswith("DA:") and current is not None:
                body = line[3:].strip()
                if "," not in body:
                    continue
                num, _, count = body.partition(",")
                try:
                    key = (current, int(num))
                    hits = int(count.split(",")[0])
                except ValueError:
                    continue
                present.add(key)
                if hits > 0:
                    covered.add(key)
    return covered, present


def short(path, roots):
    """Strip whichever source root a file sits under, for readable output."""
    for root in roots:
        if root and path.startswith(root):
            return path[len(root):].lstrip("/")
    return path


def load_cell(celldir):
    """All per-run coverage for one cell, plus its compiled-in line set."""
    runsdir = os.path.join(celldir, "runs")
    if not os.path.isdir(runsdir):
        sys.exit("no runs/ directory in %s - was coverage-run.sh run?" % celldir)

    runs = {}
    for name in sorted(os.listdir(runsdir)):
        if not name.endswith(".info"):
            continue
        covered, _ = parse_info(os.path.join(runsdir, name))
        runs[name[:-5]] = covered

    baseline = os.path.join(celldir, "baseline.info")
    if os.path.exists(baseline):
        _, present = parse_info(baseline)
    else:
        # Fall back to the union of what the runs mention, which understates
        # the compiled-in set but keeps the tool usable.
        present = set()
        for name in sorted(os.listdir(runsdir)):
            if name.endswith(".info"):
                _, p = parse_info(os.path.join(runsdir, name))
                present |= p
    return runs, present


def greedy_cover(runs, target):
    """Order runs by how much *new* coverage each adds.

    Set cover is NP-hard; greedy is the standard approximation and is the
    right tool here because the output is a ranked list a human reads, not an
    optimum anyone needs to prove. Stops when nothing adds anything.
    """
    remaining = set(target)
    chosen = []
    pool = dict(runs)
    while remaining and pool:
        best = None
        best_gain = 0
        for name, cov in pool.items():
            gain = len(cov & remaining)
            if gain > best_gain:
                best, best_gain = name, gain
        if best is None:
            break
        chosen.append((best, best_gain, len(target) - len(remaining) + best_gain))
        remaining -= pool[best]
        del pool[best]
    return chosen, remaining


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("celldirs", nargs="+",
                    help="coverage-out directories from coverage-run.sh")
    ap.add_argument("-o", "--outdir", default="coverage-report",
                    help="where to write the report (default: coverage-report)")
    ap.add_argument("-r", "--root", action="append", default=[],
                    help="source root to strip from paths, repeatable")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    cells = {}
    for d in args.celldirs:
        name = os.path.basename(os.path.dirname(os.path.abspath(d)))
        cells[name] = load_cell(d)

    # --- totals -------------------------------------------------------------

    all_covered = set()
    all_present = set()
    for runs, present in cells.values():
        all_present |= present
        for cov in runs.values():
            all_covered |= cov

    lines = []
    lines.append("LAME coverage report")
    lines.append("=" * 70)
    lines.append("")
    lines.append("cells analysed : %d" % len(cells))
    lines.append("runs analysed  : %d" % sum(len(r) for r, _ in cells.values()))
    lines.append("")
    lines.append("lines compiled in at least one cell : %d" % len(all_present))
    lines.append("lines executed by at least one run  : %d (%.1f%%)"
                 % (len(all_covered),
                    100.0 * len(all_covered) / max(1, len(all_present))))
    lines.append("")
    lines.append("NOTE: a line inside a #if that NO cell enables has no gcov")
    lines.append("record anywhere and is invisible to this report. File-level")
    lines.append("absence is detectable and listed below; line-level absence")
    lines.append("within a compiled file is not.")
    lines.append("")

    # --- per-cell value -----------------------------------------------------
    #
    # The cell-selection answer: what does each cell reach that no other cell
    # reaches? A cell with no unique lines is not worth a separate sanitizer
    # run, however different its configure line looks.

    lines.append("Per-cell contribution")
    lines.append("-" * 70)
    per_cell_cov = {}
    for name, (runs, present) in cells.items():
        cov = set()
        for c in runs.values():
            cov |= c
        per_cell_cov[name] = cov

    for name in sorted(per_cell_cov):
        others = set()
        for other, cov in per_cell_cov.items():
            if other != name:
                others |= cov
        unique = per_cell_cov[name] - others
        compiled = cells[name][1]
        lines.append("  %-12s compiled %6d  executed %6d (%.1f%%)  unique %5d"
                     % (name, len(compiled), len(per_cell_cov[name]),
                        100.0 * len(per_cell_cov[name]) / max(1, len(compiled)),
                        len(unique)))
    lines.append("")

    # --- the minimal set ----------------------------------------------------

    combined = {}
    for name, (runs, _) in cells.items():
        for run, cov in runs.items():
            combined["%s/%s" % (name, run)] = cov

    chosen, missed = greedy_cover(combined, all_covered)

    lines.append("Minimal invocation set (greedy)")
    lines.append("-" * 70)
    lines.append("%-40s %8s %8s %7s" % ("cell/run", "new", "cumul", "cumul%"))
    for name, gain, cumul in chosen:
        lines.append("%-40s %8d %8d %6.1f%%"
                     % (name, gain, cumul,
                        100.0 * cumul / max(1, len(all_covered))))
    lines.append("")
    lines.append("%d of %d runs carry all reachable coverage; the rest are"
                 % (len(chosen), len(combined)))
    lines.append("redundant for coverage purposes (they may still be worth")
    lines.append("running for other reasons - a sanitizer finds bugs on paths")
    lines.append("already covered, it just does not find NEW paths).")
    lines.append("")

    summary = os.path.join(args.outdir, "summary.txt")
    with open(summary, "w") as f:
        f.write("\n".join(lines) + "\n")

    with open(os.path.join(args.outdir, "cover-set.txt"), "w") as f:
        for name, gain, cumul in chosen:
            f.write("%s\t%d\t%d\n" % (name, gain, cumul))

    # --- what is never reached ---------------------------------------------

    uncovered = all_present - all_covered
    by_file = defaultdict(list)
    for path, line in uncovered:
        by_file[path].append(line)

    covered_files = set(p for p, _ in all_covered)
    present_files = set(p for p, _ in all_present)

    with open(os.path.join(args.outdir, "uncovered.txt"), "w") as f:
        f.write("Lines compiled in some cell but executed by nothing\n")
        f.write("=" * 70 + "\n\n")
        f.write("These are the gaps a new workload entry could close - or\n")
        f.write("genuinely dead code, which is a finding in its own right.\n\n")
        for path in sorted(by_file, key=lambda p: -len(by_file[p])):
            nums = sorted(by_file[path])
            total = len([1 for p, _ in all_present if p == path])
            f.write("%-50s %5d/%5d uncovered\n"
                    % (short(path, args.root), len(nums), total))
            f.write("    lines: %s\n" % compress(nums))
        f.write("\n\nFiles with no executed line at all\n")
        f.write("-" * 70 + "\n")
        for path in sorted(present_files - covered_files):
            f.write("  %s\n" % short(path, args.root))

    print("\n".join(lines))
    print("written to: %s" % args.outdir)


def compress(nums):
    """1,2,3,7,8 -> 1-3,7-8. Long line lists are unreadable otherwise."""
    out = []
    start = prev = nums[0]
    for n in nums[1:]:
        if n == prev + 1:
            prev = n
            continue
        out.append("%d" % start if start == prev else "%d-%d" % (start, prev))
        start = prev = n
    out.append("%d" % start if start == prev else "%d-%d" % (start, prev))
    return ",".join(out)


if __name__ == "__main__":
    main()
