# Regenerating USAGE {#maintainer_gen_usage}

`USAGE` is the plain-text form of the command-line reference. It is generated
from `doc/html/detailed.html`. So you document a new option once, in the HTML,
and the script adds it to `USAGE`.

```
python3 maintainer/gen-usage.py
```

The script needs nothing installed. It uses only the Python standard library.
Run it from the top of the source tree, or name the tree with `--srcdir DIR`.

| Option | Meaning |
|---|---|
| `--srcdir DIR` | the source tree to work in (default: the current directory). |
| `--check` | report whether `USAGE` is up to date, and change nothing. Exits with a non-zero status if it is not up to date. |

## What is generated and what is not

The examples at the top of `USAGE` (the CBR and VBR examples, the streaming
example, and the note about the scripts in `misc/`) are not in any HTML page.
They are written by hand, in `doc/usage-preamble.txt`. The script creates
everything below them from `detailed.html`.

So you edit two files, and the script writes the third:

| File | Edit it? |
|---|---|
| `doc/usage-preamble.txt` | yes: the examples |
| `doc/html/detailed.html` | yes: the option reference |
| `USAGE` | **no**: run the script |

The generated part of `USAGE` says this at its top.

## Checking it

`maintainer/check-optdoc.py` takes the option names that the parser accepts and
looks for each of them in `doc/man/lame.1`, `doc/html/detailed.html` and
`USAGE`. It reports which names are missing from each file. It also reports
index links in `detailed.html` that point to an anchor that the page does not
define, because `USAGE` shows such a link as a heading with nothing below it.
It exits with a non-zero status when it finds either. Like the generator, it
takes the tree with `--srcdir DIR`.

```
python3 maintainer/check-optdoc.py
```

The script handles only the HTML constructs that `detailed.html` uses. If new
markup appears there and the output is wrong, look at the script first.
