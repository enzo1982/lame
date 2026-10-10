/**
 * @file
 * @brief The texts that the ACM codec and the DirectShow filter both show: those
 *        of the same settings, and LAME's licence notice.
 */

#if !defined(_SETTINGTEXT_H__INCLUDED_)
#define _SETTINGTEXT_H__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include <stddef.h>

/** @brief The levels of the encoding quality that lame_set_quality() takes: 0 to 9. */
static const unsigned int ENCODING_QUALITY_LEVELS = 10;
/** @brief LAME's own encoding quality, which both components use by default. */
static const unsigned int ENCODING_QUALITY_DEFAULT = 3;

const char * EncodingQualityText(unsigned int level);

/** @brief The VBR quality levels that lame_set_VBR_q() takes: 0 (the best) to 9. */
static const unsigned int VBR_QUALITY_LEVELS = 10;

/** \name The input whose typical VBR bitrates the dialogs show: 44.1 kHz stereo
    @{ */
static const unsigned int VBR_TEXT_SAMPLE_RATE = 44100;
static const unsigned int VBR_TEXT_CHANNELS = 2;
/** @} */

unsigned int VbrTypicalBitrate(unsigned int frequency, unsigned int channels, unsigned int quality);
int VbrQualityText(unsigned int level, char *text, size_t size);

extern const char LICENSE_NOTICE[];
/** @brief The authors that the about boxes name: those of the ACM codec, of the
    DirectShow filter, and LAME's. */
extern const char ABOUT_CREDITS[];
/** @brief The credit for the icon, in the about boxes. */
extern const char ABOUT_ICON_CREDIT[];

#endif // !defined(_SETTINGTEXT_H__INCLUDED_)
