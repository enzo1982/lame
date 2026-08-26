# Regenerating USAGE {#maintainer_gen_usage}

`USAGE` is the plain-text form of the command-line reference. It is generated
from `doc/html/detailed.html`, so a new option reaches it by being documented
in the HTML rather than by being remembered twice.

```
python3 maintainer/gen-usage.py
```

The script needs nothing installed: it uses the Python standard library only.
Run it from the top of the source tree, or give it one with `--srcdir DIR`.

| Option | Meaning |
|---|---|
| `--srcdir DIR` | the source tree to work in (default: the current directory). |
| `--check` | report whether `USAGE` is up to date and change nothing; exits non-zero if it is stale. |

## What is generated and what is not

The examples at the top of `USAGE` &mdash; the constant-bitrate and
variable-bitrate recipes, the streaming example, the note about the scripts in
`misc/` &mdash; are not in any HTML page. They stay hand-written, in
`doc/usage-preamble.txt`. Everything below them is rendered from
`detailed.html`.

So there are two files to edit and one that is written for you:

| File | Edit it? |
|---|---|
| `doc/usage-preamble.txt` | yes &mdash; the examples |
| `doc/html/detailed.html` | yes &mdash; the option reference |
| `USAGE` | **no** &mdash; run the script |

`USAGE` says so at the top of its generated half.

## Why it is generated

It had drifted. When the generator was written, `USAGE` named 61 of the 75
long options the parser accepts &mdash; two of the missing ones had been added
in that same round &mdash; and still carried a misspelling of `--resample`
that had been reported on the bug tracker in 2024 and left. Keeping a
second hand-written copy of a reference in step with the first is a job nobody
does reliably; `detailed.html` has to be edited for a new option anyway.

## Checking it

`build/optdoc.py` takes the option names the parser accepts and looks for each
of them in `doc/man/lame.1`, `doc/html/detailed.html` and `USAGE`, and reports
what is missing from each. It also reports index links in `detailed.html` that
point at an anchor the body does not define, because `USAGE` renders such a
link as a heading with nothing under it.

The renderer understands the constructs `detailed.html` actually uses and no
others. If a new kind of markup appears there and comes out wrong, that is
where to look.
