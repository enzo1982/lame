# Publishing the API documentation {#maintainer_gen_api_docs}

`maintainer/gen-api-docs.sh` builds both documentation sets and copies them
into the website working tree, ready for the maintainer to review and publish.

```
sh maintainer/gen-api-docs.sh
```

With no arguments it configures a build directory beside the website
checkout, runs `make doxygen` and `make doxygen-internal`, and clean-copies
the result to `webpages/API/public` and `webpages/API/internal`.

It stops there. **Committing and uploading the website is the maintainer's
step**, the same as committing to SVN &mdash; the script writes into a working
tree and says what it wrote, and nothing leaves the machine.

## Options

| Option | Meaning |
|---|---|
| `-b DIR` | build directory to generate in. An already-configured one is reused; otherwise `configure` runs in it first. |
| `-s SRCDIR` | the LAME source tree (default: the parent of `maintainer/`). |
| `-w WEBDIR` | the website working tree (default: the `webpages` checkout beside the source tree). |
| `-n` | generate but do not copy &mdash; for looking at the result first. |

## Why both sets are published

The **public** set is the API reference for someone writing a program against
libmp3lame. It is the one the website surfaces prominently.

The **internal** set documents LAME's own structures and functions, for
someone working on the encoder. Publishing it is a deliberate decision, not a
default: it exposes internals that carry no stability promise whatsoever. It
is therefore linked asymmetrically &mdash; only from the developer-facing
pages, and never without the caveat.

Links into a published set are **relative** (`API/public/index.html`), so the
site works the same from a local checkout as from the server.

## Latest only

The copy overwrites: there is one published set, and it describes the current
source. Versioned trees (`API/<version>/`) were considered and deliberately
not done &mdash; the alternative is a growing archive of documentation for
releases nobody is running, and the documentation for an old release is in
that release's tarball anyway.

The copy is a **clean** copy: `API/public` and `API/internal` are removed
before writing, so a page that no longer exists does not survive on the
website as a stale link target.

## What it refuses to do

A Doxygen run that reads no input still writes a complete set of stylesheets,
scripts and images, and exits 0. The result is a directory that looks entirely
healthy and contains no documentation at all &mdash; and copying it over the
website would silently replace a working reference with an empty shell.

So before copying anything, the script requires each set to actually document
a function from the installed header. If either does not, it says so and
publishes nothing.

The test deliberately asks for documented *content* rather than for a
particular generated page: which pages Doxygen emits depends on the render
settings, so a page-name test also fails whenever those settings change for a
good reason, and it did.

It also warns if `index.html` is absent from either half after the copy, since
that is exactly the file every website link points at.

## The generated HTML is not in the tarball

`make dist` does not ship the generated documentation. It is reproducible from
the tarball with `make doxygen`, and shipping a second copy would go stale
against the source it was generated from. The website is where the built
version lives; the tarball carries what builds it.
