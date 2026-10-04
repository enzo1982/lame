# Publishing the API documentation {#maintainer_gen_api_docs}

`maintainer/gen-api-docs.sh` builds both documentation sets and copies them into
the working tree of the website. The maintainer then reviews and publishes
them.

```
sh maintainer/gen-api-docs.sh
```

Without arguments, the script configures a build directory next to the website
checkout, runs `make doxygen` and `make doxygen-internal`, and copies the result
to `webpages/API/public` and `webpages/API/internal`, after removing the old
content.

Then it stops. **The maintainer commits and uploads the website**, as with the
commits to SVN. The script writes into a working tree and says what it wrote.
Nothing leaves the machine.

## Options

| Option | Meaning |
|---|---|
| `-b DIR` | build directory to generate in. A directory that is already configured is reused; otherwise `configure` runs in it first. |
| `-s SRCDIR` | the LAME source tree (default: the parent of `maintainer/`). |
| `-w WEBDIR` | the working tree of the website (default: the `webpages` checkout next to the source tree). |
| `-n` | generate, but do not copy, so you can look at the result first. |

## Why both sets are published

The **public** set is the API reference for people who write a program that
uses libmp3lame. The website shows it prominently.

The **internal** set documents the structures and functions inside LAME, for
people who work on the encoder. It is published on purpose. It shows internals
that have no stability promise at all. So only the pages for developers link to
it, and always with this warning.

Links into a published set are **relative** (`API/public/index.html`), so the
site works the same from a local checkout and from the server.

## Only the latest version

The copy overwrites the old one. There is one published set, and it describes
the current source. The documentation for an older release is in the tarball of
that release.

The copy is a **clean** copy: the script removes `API/public` and `API/internal`
before it writes. So a page that the new set does not have does not stay on the
website as an old link target.

## What it does not publish

A Doxygen run that reads no input still writes a complete set of style sheets,
scripts and images, and exits with 0. The result looks like a healthy
directory, but it contains no documentation. Copying it over the website would
replace a working reference with an empty one, without any error.

So before it copies anything, the script checks that each set documents a
function from the installed header. If a set does not, the script says so and
publishes nothing.

The check looks for documented *content*, not for a specific generated page.
Which pages Doxygen writes depends on the settings, so a check for a page name
would also fail when the settings change for a good reason.

After the copy, the script also warns if `index.html` is missing from either
set, because every link on the website points to this file.

## The generated HTML is not in the tarball

`make dist` does not include the generated documentation. `make doxygen` can
create it from the tarball, and a second copy in the tarball would get out of
date with its source. The website has the built documentation. The tarball has
what builds it.
