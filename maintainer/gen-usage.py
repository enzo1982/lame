#!/usr/bin/env python3
"""Generate USAGE from doc/html/detailed.html.

USAGE is the plain-text form of the documentation. It used to be maintained by
hand beside the HTML, and drifted: at the time this was written it was missing
fourteen of the seventy-five long options the parser accepts, including two
added in the same round, and carried a typo (``--resamle``) that had been
reported on the bug tracker and left for years.

So it is generated, from ``doc/html/usage.html`` and ``doc/html/detailed.html``
in that order - the examples and then the option reference. Both have to be
edited for a new option or a new example anyway, so there is nothing to
remember.

    python3 maintainer/gen-usage.py            # rewrite USAGE
    python3 maintainer/gen-usage.py --check    # exit 1 if USAGE is stale

Run from the top of the source tree, or give it one with --srcdir. Standard
library only, so it needs nothing installed.
"""

import argparse
import html.parser
import os
import re
import sys
import textwrap

WIDTH = 72
RULE = '=' * WIDTH
#  Stands in for <br /> until the text has been whitespace-normalised; a real
#  newline would be eaten by that normalisation, which is what turned the genre
#  table into one 482-character line.
BREAK = '\x00'


def banner(title, level):
    #  A row of '=' for a section and a row of '-' for what is inside one. Drawn
    #  the same, the sub-headings inside the examples read as top-level sections.
    if level >= 4:
        return '\n\n%s\n%s\n' % (title, '-' * min(len(title), WIDTH))
    return '\n\n%s\n%s\n%s\n' % (RULE, title, RULE)


class Renderer(html.parser.HTMLParser):
    """Turn the documentation page into text.

    Deliberately narrow: it understands the constructs detailed.html actually
    uses and nothing else. A general HTML renderer would be a much larger thing
    to get right, and would still have to be told what to do with this page's
    navigation furniture.
    """

    SKIP = ('head', 'script', 'style')

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out = []
        self.buf = []
        self.skip_depth = 0
        self.in_pre = 0
        self.div_stack = []
        self.suppress = 0        # inside the navigation menu
        self.cell = None         # collecting a table cell
        self.row = []
        self.in_table = 0
        self.list_depth = 0
        self.in_li = 0
        self.pending_title = False

    # -- helpers ---------------------------------------------------------
    def text(self):
        s = ''.join(self.buf)
        self.buf = []
        lines = [' '.join(part.split()) for part in s.split(BREAK)]
        while lines and not lines[0]:
            lines.pop(0)
        while lines and not lines[-1]:
            lines.pop()
        return '\n'.join(lines)

    def emit(self, s=''):
        self.out.append(s)

    def flush_text(self):
        """Emit whatever text is buffered, in the shape of its context.

        Called before a <pre> opens and at the end of a list item, so that a
        description followed by a command line comes out as two things rather
        than as one verbatim block.
        """
        s = self.text()
        if not s:
            return
        if self.in_li:
            self.para(s, indent='      ', first='    - ')
        else:
            self.para(s)

    def para(self, s, indent='    ', first=None):
        if not s:
            return
        head = first if first is not None else indent
        for chunk in s.split('\n'):
            if not chunk:
                continue
            for line in textwrap.wrap(chunk, WIDTH, initial_indent=head,
                                      subsequent_indent=indent,
                                      break_long_words=False):
                self.emit(line)
            head = indent
        self.emit('')

    # -- parser callbacks ------------------------------------------------
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag in self.SKIP:
            self.skip_depth += 1
            return
        if self.skip_depth or self.suppress:
            if tag == 'div':
                self.div_stack.append('skip')
            return
        if tag == 'div':
            if a.get('id') in ('menu', 'submenu'):
                self.suppress += 1
                self.div_stack.append('menu')
            else:
                self.div_stack.append('div')
            return
        if tag == 'br':
            #  A sentinel rather than a newline: the text is whitespace-
            #  normalised below, and a real newline would not survive that.
            self.buf.append(BREAK)
            return
        if tag == 'sup':
            self.buf.append(' [')
            return
        if tag == 'pre':
            self.flush_text()
            self.in_pre += 1
            return
        if tag in ('h1', 'h2', 'h3', 'h4'):
            self.buf = []
            return
        if tag == 'p':
            #  Same reason as <li> and <pre>: a paragraph opening inside a list
            #  item leaves the item's own text buffered, and discarding it here
            #  loses the sentence that says what the item is.
            self.flush_text()
            self.pending_title = a.get('class') == 'settingtitle'
            return
        if tag == 'table':
            self.in_table += 1
            self.row = []
            return
        if tag == 'tr':
            self.row = []
            return
        if tag in ('td', 'th'):
            self.cell = []
            self.buf = []
            return
        if tag in ('ul', 'ol'):
            self.list_depth += 1
            return
        if tag == 'li':
            #  Flush rather than discard: text sitting between <ul> and the
            #  first <li> is a mistake in the page, and throwing it away here
            #  hides the mistake instead of showing it.
            self.flush_text()
            self.in_li += 1
            return

    def handle_endtag(self, tag):
        if tag in self.SKIP:
            self.skip_depth = max(0, self.skip_depth - 1)
            return
        if tag == 'div':
            what = self.div_stack.pop() if self.div_stack else 'div'
            if what == 'menu':
                self.suppress = max(0, self.suppress - 1)
            return
        if self.skip_depth or self.suppress:
            return
        if tag == 'pre':
            self.in_pre = max(0, self.in_pre - 1)
            pad = '      ' if self.in_li else '    '
            for line in ''.join(self.buf).strip('\n').split('\n'):
                self.emit((pad + line).rstrip())
            self.buf = []
            self.emit('')
            return
        if tag in ('h1', 'h2', 'h3', 'h4'):
            self.emit(banner(self.text(), int(tag[1])))
            return
        if tag == 'p':
            s = self.text()
            if self.pending_title:
                self.emit('')
                self.para(s, indent='', first='')
            elif self.in_li:
                #  A paragraph inside a list item belongs under its bullet, not
                #  back at the margin as though the item had ended.
                self.para(s, indent='      ')
            else:
                self.para(s)
            self.pending_title = False
            return
        if tag in ('td', 'th'):
            self.row.append(self.text())
            self.cell = None
            return
        if tag == 'tr':
            self.flush_row()
            return
        if tag == 'table':
            self.in_table = max(0, self.in_table - 1)
            self.emit('')
            return
        if tag == 'sup':
            self.buf.append(']')
            return
        if tag in ('ul', 'ol'):
            self.list_depth = max(0, self.list_depth - 1)
            self.emit('')
            return
        if tag == 'li':
            self.flush_text()
            self.in_li = max(0, self.in_li - 1)
            return

    def handle_data(self, data):
        if self.skip_depth or self.suppress:
            return
        if self.in_pre:
            self.buf.append(data)
        else:
            self.buf.append(data)

    # -- table rows ------------------------------------------------------
    def flush_row(self):
        cells = [c for c in self.row]
        self.row = []
        if not any(cells):
            return
        if len(cells) == 1:
            self.para(cells[0], indent='')
            return
        left, right = cells[0], ' '.join(cells[1:])
        if not left:
            self.para(right, indent='')
            return
        if not right:
            self.emit(left)
            self.emit('')
            return
        pad = 16
        first = left.ljust(pad) if len(left) < pad else left + '\n' + ' ' * pad
        if '\n' in first:
            self.emit(left)
            self.para(right, indent=' ' * pad, first=' ' * pad)
        else:
            self.para(right, indent=' ' * pad, first=first)


def render(path):
    r = Renderer()
    with open(path, 'rb') as fp:
        r.feed(fp.read().decode('utf-8', 'replace'))
    r.close()
    lines = r.out
    # Collapse runs of blank lines; a rule line never wants one after it.
    out = []
    for line in lines:
        if line.strip() == '' and out and out[-1].strip() == '':
            continue
        out.append(line.rstrip())
    text = '\n'.join(out)
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text.strip('\n') + '\n'


PAGES = ('usage.html', 'detailed.html')

HEADER = """\
LAME - the command line, in plain text.

This file is generated from doc/html/usage.html and
doc/html/detailed.html by maintainer/gen-usage.py.
Edit those, not this file.
"""


def build(srcdir):
    note = '\n'.join(('    ' + l).rstrip() for l in HEADER.rstrip('\n').split('\n'))
    parts = [render(os.path.join(srcdir, 'doc', 'html', p)) for p in PAGES]
    return note + '\n' + '\n\n'.join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--srcdir', default='.')
    ap.add_argument('--check', action='store_true',
                    help='report whether USAGE is up to date, change nothing')
    args = ap.parse_args()

    target = os.path.join(args.srcdir, 'USAGE')
    new = build(args.srcdir)
    try:
        old = open(target, encoding='utf-8').read()
    except OSError:
        old = None

    if args.check:
        if old == new:
            print('USAGE is up to date')
            return 0
        print('USAGE is stale - run: python3 maintainer/gen-usage.py')
        return 1
    if old == new:
        print('USAGE unchanged')
        return 0
    with open(target, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write(new)
    print('USAGE written, %d lines' % new.count('\n'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
