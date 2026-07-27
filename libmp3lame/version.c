/*
 *      Version numbering for LAME.
 *
 *      Copyright (c) 1999 A.L. Faber
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*!
  \file   version.c
  \brief  Version numbering for LAME.

  Contains functions which describe the version of LAME.

  \author A.L. Faber
  \version \$Id$
  \ingroup libmp3lame
*/


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include "lame.h"
#include "machine.h"

#include "version.h"    /* macros of version numbers */





/*! Get the LAME version string. */
/*!
  The full form, meant for a screen report: for an alpha or beta build it
  carries the build date, and for an alpha the build time as well, so two
  builds of identical sources do not necessarily produce the same string.
  Nothing about the layout is guaranteed - to compare versions, use
  \c get_lame_version_numerical(); to put a version into an encoded stream,
  use \c get_lame_short_version(), which never varies between builds.

  \return a pointer to a static, never-NULL string describing the version of
          LAME. It belongs to the library and must not be freed or modified;
          it stays valid for the lifetime of the process.
*/
const char *
get_lame_version(void)
{                       /* primary to write screen reports */
    /* Here we can also add informations about compile time configurations */

#if   LAME_ALPHA_VERSION
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) " "
        "(alpha " STR(LAME_PATCH_VERSION) ", " __DATE__ " " __TIME__ ")";
#elif LAME_BETA_VERSION
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) " "
        "(beta " STR(LAME_PATCH_VERSION) ", " __DATE__ ")";
#elif LAME_RELEASE_VERSION && (LAME_PATCH_VERSION > 0)
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) "." STR(LAME_PATCH_VERSION);
#else
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION);
#endif

    return str;
}


/*! Get the short LAME version string. */
/*!
  The same version as \c get_lame_version() with the build date and time left
  out, so two builds of identical sources produce identical output. That is
  what makes it the form to embed in an encoded stream, and what makes an
  encoder's output reproducible.

  \return a pointer to a static, never-NULL string holding the version of
          LAME. It belongs to the library and must not be freed or modified;
          it stays valid for the lifetime of the process.
*/
const char *
get_lame_short_version(void)
{
    /* adding date and time to version string makes it harder for output
       validation */

#if   LAME_ALPHA_VERSION
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) " (alpha " STR(LAME_PATCH_VERSION) ")";
#elif LAME_BETA_VERSION
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) " (beta " STR(LAME_PATCH_VERSION) ")";
#elif LAME_RELEASE_VERSION && (LAME_PATCH_VERSION > 0)
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) "." STR(LAME_PATCH_VERSION);
#else
    static /*@observer@ */ const char *const str =
        STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION);
#endif

    return str;
}

/*! Get the _very_ short LAME version string. */
/*!
  The most compact form: `"LAME"` followed by the major and minor version, a
  one-character build type (`'a'` alpha, `'b'` beta, `'r'` a patched release,
  `' '` otherwise) and, for a patched release, the patch level. Provided for a
  caller that has very little room; the library itself does not use it, and
  the encoder writes its own version into the LAME tag through a separate
  internal function whose width is fixed for binary compatibility.

  Unlike that one, this string has **no guaranteed maximum length** - it grows
  with the version numbers - so a caller copying it into a fixed-size field
  must bound the copy itself.

  \return a pointer to a static, never-NULL string holding the version of
          LAME. It belongs to the library and must not be freed or modified;
          it stays valid for the lifetime of the process.
*/
const char *
get_lame_very_short_version(void)
{
    /* adding date and time to version string makes it harder for output
       validation */
#if   LAME_ALPHA_VERSION
#define P "a"
#elif LAME_BETA_VERSION
#define P "b"
#elif LAME_RELEASE_VERSION && (LAME_PATCH_VERSION > 0)
#define P "r"
#else
#define P " "
#endif
    static /*@observer@ */ const char *const str =
#if (LAME_PATCH_VERSION > 0)
      "LAME" STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) P STR(LAME_PATCH_VERSION)
#else
      "LAME" STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) P
#endif
      ;
    return str;
}

/*!
  \internal
  Get the encoder version string written into the LAME tag.

  Not part of the public API - it is not exported, and the field it fills is
  fixed by the tag format rather than by anything a caller chooses. Library
  users wanting a compact version string want
  \c get_lame_very_short_version() instead.

  The whole comment sits inside \c \\internal on purpose: the marker runs to
  the end of its comment block, so a brief in a block of its own would survive
  into the public documentation and reintroduce the name there.

  Limited to 9 characters max. Due to some 3rd party HW/SW decoders, it has to
  start with LAME.

  \par Field width and format (fixed, binary-compatibility critical)
  This string is copied into a **fixed 9-byte** field of the LAME tag that
  gets embedded in every encoded MP3 stream (see \c VbrTag.c,
  \c LAMEHEADERSIZE and the \c strncpy(...,9) call in the tag-writing code).
  Widening that field would shift every subsequent byte of the tag and break
  every third-party decoder that parses it at a fixed offset - it is not
  adjustable. The format itself (`"LAME" major "." minor type`) is likewise
  fixed for the same reason: some decoders reportedly pattern-match on it.

  \par Byte budget
  `strlen("LAME")` (4) + `strlen(major)` + `strlen(".")` (1) +
  `strlen(minor)` + `strlen(type)` (1, always exactly one character:
  `'a'`/`'b'`/`'r'`/`' '`) must not exceed 9, i.e.
  \code
      strlen(major) + strlen(minor) <= 3
  \endcode
  This invariant is enforced at compile time right below this function
  (`compiletime_assert`) - if it ever trips, the version numbers (not this
  field's width or format) are what needs to change.

  \return a pointer to the short version of the LAME version string.
 */
const char*
get_lame_tag_encoder_short_version(void)
{
    static /*@observer@ */ const char *const str =
            /* FIXME: new scheme / new version counting / drop versioning here ? */
    "LAME" STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) P
    ;
    return str;
}

/*! \internal
 *  Enforces the byte budget documented above at compile time: catches an
 *  overflow the moment a version bump reintroduces it, instead of letting
 *  it silently truncate at run time. */
compiletime_assert(sizeof("LAME" STR(LAME_MAJOR_VERSION) "." STR(LAME_MINOR_VERSION) P) - 1 <= 9);

/*! Get the version string for GPSYCHO. */
/*!
  GPSYCHO is the psychoacoustic model LAME encodes with; it carries its own
  version, which moves independently of LAME's. As with
  \c get_lame_version(), an alpha or beta build embeds the build date, so the
  string is not a comparable value - \c get_lame_version_numerical() reports
  the same numbers in comparable form.

  \return a pointer to a static, never-NULL string describing the version of
          GPSYCHO. It belongs to the library and must not be freed or
          modified; it stays valid for the lifetime of the process.
*/
const char *
get_psy_version(void)
{
#if   PSY_ALPHA_VERSION > 0
    static /*@observer@ */ const char *const str =
        STR(PSY_MAJOR_VERSION) "." STR(PSY_MINOR_VERSION)
        " (alpha " STR(PSY_ALPHA_VERSION) ", " __DATE__ " " __TIME__ ")";
#elif PSY_BETA_VERSION > 0
    static /*@observer@ */ const char *const str =
        STR(PSY_MAJOR_VERSION) "." STR(PSY_MINOR_VERSION)
        " (beta " STR(PSY_BETA_VERSION) ", " __DATE__ ")";
#else
    static /*@observer@ */ const char *const str =
        STR(PSY_MAJOR_VERSION) "." STR(PSY_MINOR_VERSION);
#endif

    return str;
}


/*! Get the URL for the LAME website. */
/*!
  Fixed at compile time. Offered so a program reporting the encoder can point
  its users at the project without hard-coding an address that may move.

  \return a pointer to a static, never-NULL string holding the project's URL.
          It belongs to the library and must not be freed or modified; it
          stays valid for the lifetime of the process.
*/
const char *
get_lame_url(void)
{
    static /*@observer@ */ const char *const str = LAME_URL;

    return str;
}


/*! Get the numerical representation of the version. */
/*!
  The comparable form of everything the version strings report: LAME's own
  version and the psychoacoustic model's, each as separate integers, so a
  caller can test for a minimum version instead of parsing text.

  \c alpha and \c beta hold the patch level of an alpha or beta build and are
  0 otherwise; at most one of them is ever non-zero. \c features is retained
  for compatibility and is always the empty string - make no assumptions about
  its contents.

  \code
      lame_version_t v;
      get_lame_version_numerical(&v);
      if (v.major > 3 || (v.major == 3 && v.minor >= 100)) {
          / * a 3.100-or-later feature is available * /
      }
  \endcode

  \param lvp  the structure to fill in. Must not be NULL - it is written
              unconditionally, and every field is assigned, so it need not be
              initialised first.
*/
void
get_lame_version_numerical(lame_version_t * lvp)
{
    static /*@observer@ */ const char *const features = ""; /* obsolete */

    /* generic version */
    lvp->major = LAME_MAJOR_VERSION;
    lvp->minor = LAME_MINOR_VERSION;
#if LAME_ALPHA_VERSION
    lvp->alpha = LAME_PATCH_VERSION;
    lvp->beta = 0;
#elif LAME_BETA_VERSION
    lvp->alpha = 0;
    lvp->beta = LAME_PATCH_VERSION;
#else
    lvp->alpha = 0;
    lvp->beta = 0;
#endif

    /* psy version */
    lvp->psy_major = PSY_MAJOR_VERSION;
    lvp->psy_minor = PSY_MINOR_VERSION;
    lvp->psy_alpha = PSY_ALPHA_VERSION;
    lvp->psy_beta = PSY_BETA_VERSION;

    /* compile time features */
    /*@-mustfree@ */
    lvp->features = features;
    /*@=mustfree@ */
}


/*! Get the pointer width the library was built for. */
/*!
  Reports the build, not the operating system: it is derived from the size of
  a pointer in this translation unit, so a 32-bit library on a 64-bit system
  reports 32 bits. Intended for a version banner alongside
  \c get_lame_version().

  \return \c "32bits" or \c "64bits", or the empty string on a target whose
          pointers are neither 4 nor 8 bytes wide - never NULL. The string
          belongs to the library and must not be freed or modified.
*/
const char *
get_lame_os_bitness(void)
{
    static /*@observer@ */ const char *const strXX = "";
    static /*@observer@ */ const char *const str32 = "32bits";
    static /*@observer@ */ const char *const str64 = "64bits";

    switch (sizeof(void *)) {
    case 4:
        return str32;

    case 8:
        return str64;

    default:
        return strXX;
    }
}

/* end of version.c */
