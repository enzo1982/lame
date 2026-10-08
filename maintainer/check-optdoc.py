#!/usr/bin/env python3
"""Check that the documentation names every long option of the frontend.

The script takes the long options that ``frontend/parse.c`` accepts and looks
for each of them in ``doc/man/lame.1``, ``doc/html/detailed.html`` and
``USAGE``. It reports the names that are missing from each file. It also
reports the index links in ``detailed.html`` that point to an anchor the page
does not define; ``USAGE`` is generated from that page and would show such a
link as a heading with nothing below it.

It checks names only. An entry that names an option but describes it wrongly
counts as present.

    python3 maintainer/check-optdoc.py
    python3 maintainer/check-optdoc.py --srcdir DIR

Run from the top of the source tree, or give it one with --srcdir. Standard
library only, so it needs nothing installed. The exit status is 1 when a name
is missing or a link is dead, and 0 otherwise.
"""

import argparse
import os
import re
import sys

# The documentation files, each with the change that turns its text into
# plain option names (the man page escapes the hyphen).
FILES = [('doc/man/lame.1', lambda s: s.replace('\\-', '-')),
         ('doc/html/detailed.html', lambda s: s),
         ('USAGE', lambda s: s)]

OPTION = r'"([a-zA-Z0-9][a-zA-Z0-9-]*)"'


def read(srcdir, name):
    with open(os.path.join(srcdir, name), 'rb') as f:
        return f.read().decode('latin-1')


def public_options(parse_c):
    """The long options of the parser, without those that only an
    --enable-internal build accepts and that are not documented."""
    opts = set(re.findall(r'T_ELIF2?\(' + OPTION, parse_c))
    internal = set(re.findall(r'T_ELIF_INTERNAL\(' + OPTION, parse_c))
    return sorted(opts - internal), len(internal)


def dead_links(detailed):
    """The fragment links of detailed.html whose anchor the page lacks."""
    anchors = set(re.findall(r'<a name="([^"]+)"', detailed))
    links = []
    for h in re.findall(r'href="#([^"]+)"', detailed):
        if h not in links:
            links.append(h)
    return links, anchors, [h for h in links if h not in anchors]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--srcdir', default='.', help='the source tree (default: the current directory)')
    args = ap.parse_args()

    opts, n_internal = public_options(read(args.srcdir, 'frontend/parse.c'))
    if not opts:
        print('no long options found in frontend/parse.c')
        return 1
    print('frontend/parse.c accepts %d public long options (%d for --enable-internal only)'
          % (len(opts), n_internal))

    failed = False
    texts = {name: norm(read(args.srcdir, name)) for name, norm in FILES}
    for name, _ in FILES:
        missing = [o for o in opts if '--' + o not in texts[name]]
        print('%-24s %d of %d named, %d missing' % (name, len(opts) - len(missing), len(opts), len(missing)))
        for i in range(0, len(missing), 6):
            print('    ' + ' '.join(missing[i:i + 6]))
        failed = failed or bool(missing)

    links, anchors, dead = dead_links(texts['doc/html/detailed.html'])
    print('doc/html/detailed.html   %d index links, %d anchors, %d dead' % (len(links), len(anchors), len(dead)))
    for h in dead:
        print('    dead link: #%s' % h)
    failed = failed or bool(dead)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
