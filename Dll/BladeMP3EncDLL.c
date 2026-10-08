/*
*	Blade DLL Interface for LAME.
*
*	Copyright (c) 1999 - 2002 A.L. Faber
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
* Boston, MA  02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <windows.h>
#define _BLADEDLL
#include "BladeMP3EncDLL.h"
#include "lametag_scan.h"
#include <limits.h>
#include <stdio.h>

#include <lame.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define         Min(A, B)       ((A) < (B) ? (A) : (B))

#define _RELEASEDEBUG 0

// lame_enc DLL version number
const BYTE MAJORVERSION = 1;
const BYTE MINORVERSION = 32;


// Local variables
static HMODULE				gs_hModule=NULL;
static BOOL					gs_bLogFile=FALSE;
static lame_global_flags*	gfp_save = NULL;
/* the stream the DLL released last: a handle equal to it is not dereferenced */
static lame_global_flags*	gfp_released = NULL;

// Local function prototypes
static void DebugVPrintf( const char* pzFormat, va_list ap );
static void DebugPrintf( const char* pzFormat, ... );
static void DispErr( char const* strErr );
static void PresetOptions( lame_global_flags *gfp, LONG myPreset );


/**
 * \internal
 * \brief Writes a formatted message to the log file beside the DLL, when the
 *        log is switched on, and to the debugger in debug builds. It has the
 *        type of a libmp3lame report function.
 * \param pzFormat  the printf format.
 * \param ap        its arguments.
 */
static void DebugVPrintf(const char* pzFormat, va_list ap)
{
    char	szBuffer[1024]={'\0',};
    char	szFileName[MAX_PATH+1]={'\0',};
    DWORD	dwNameLen;

    // Get the full module (DLL) file name.  Zero means the call failed and
    // the buffer size means the name did not fit; a name that did not fit is
    // not null terminated on every Windows version, so the returned length
    // is the only one there is.
    dwNameLen = GetModuleFileNameA(	gs_hModule, 
        szFileName,
        sizeof( szFileName ) );
    if ( dwNameLen >= (DWORD) sizeof( szFileName ) )
        dwNameLen = 0;
    szFileName[ dwNameLen ] = '\0';

    // change file name extention
    if ( dwNameLen >= 3 )
    {
        szFileName[ dwNameLen - 3 ] = 't';
        szFileName[ dwNameLen - 2 ] = 'x';
        szFileName[ dwNameLen - 1 ] = 't';
    }

    // copy it to the string buffer.  _vsnprintf writes no terminator when
    // the text does not fit, so the last byte is kept for one.
    _vsnprintf(szBuffer, sizeof(szBuffer) - 1, pzFormat, ap);
    szBuffer[ sizeof(szBuffer) - 1 ] = '\0';

    // log it to the file?
    if ( gs_bLogFile && szFileName[0] != '\0' ) 
    {	
        FILE* fp = NULL;

        // try to open the log file
        fp=fopen( szFileName, "a+" );

        // check file open result
        if (fp)
        {
            // write string to the file
            fputs(szBuffer,fp);

            // close the file
            fclose(fp);
        }
    }

#if defined _DEBUG || _RELEASEDEBUG
    OutputDebugStringA( szBuffer );
#endif
}

/**
 * \internal
 * \brief DebugVPrintf() with the arguments in the call.
 * \param pzFormat  the printf format.
 */
static void DebugPrintf(const char* pzFormat, ...)
{
    va_list ap;

    va_start(ap, pzFormat);
    DebugVPrintf(pzFormat, ap);
    va_end(ap);
}


static void PresetOptions( lame_global_flags *gfp, LONG myPreset )
{
    switch (myPreset)
    {
        /*-1*/case LQP_NOPRESET:
            break;

        /*0*/case LQP_NORMAL_QUALITY:
            /*	lame_set_quality( gfp, 5 );*/
            break;

        /*1*/case LQP_LOW_QUALITY:
             lame_set_quality( gfp, 9 );
             break;

        /*2*/case LQP_HIGH_QUALITY:
             lame_set_quality( gfp, 2 );
             break;

        /*3*/case LQP_VOICE_QUALITY:				// --voice flag for experimental voice mode
             lame_set_mode( gfp, MONO );
             lame_set_preset( gfp, 56);
             break;

        /*4*/case LQP_R3MIX:					// --R3MIX
             lame_set_preset( gfp, R3MIX);
             break;

        /*5*/case LQP_VERYHIGH_QUALITY:
             lame_set_quality( gfp, 0 );
             break;

        /*6*/case LQP_STANDARD:				// --PRESET STANDARD
            lame_set_preset( gfp, STANDARD);
            break;

        /*7*/case LQP_FAST_STANDARD:				// --PRESET FAST STANDARD
            lame_set_preset( gfp, STANDARD_FAST);
            break;

        /*8*/case LQP_EXTREME:				// --PRESET EXTREME
            lame_set_preset( gfp, EXTREME);
            break;

        /*9*/case LQP_FAST_EXTREME:				// --PRESET FAST EXTREME:
            lame_set_preset( gfp, EXTREME_FAST);
            break;

        /*10*/case LQP_INSANE:				// --PRESET INSANE
            lame_set_preset( gfp, INSANE);
            break;

        /*11*/case LQP_ABR:					// --PRESET ABR
            // handled in beInitStream
            break;

        /*12*/case LQP_CBR:					// --PRESET CBR
            // handled in beInitStream
            break;

        /*13*/case LQP_MEDIUM:					// --PRESET MEDIUM
            lame_set_preset( gfp, MEDIUM);
            break;

        /*14*/case LQP_FAST_MEDIUM:					// --PRESET FAST MEDIUM
            lame_set_preset( gfp, MEDIUM_FAST);
            break;

        /*1000*/case LQP_PHONE:
            lame_set_mode( gfp, MONO );
            lame_set_preset( gfp, 16);
            break;

        /*2000*/case LQP_SW:
            lame_set_mode( gfp, MONO );
            lame_set_preset( gfp, 24);
            break;

        /*3000*/case LQP_AM:
            lame_set_mode( gfp, MONO );
            lame_set_preset( gfp, 40);
            break;

        /*4000*/case LQP_FM:
            lame_set_preset( gfp, 112);
            break;

        /*5000*/case LQP_VOICE:
            lame_set_mode( gfp, MONO );
            lame_set_preset( gfp, 56);
            break;

        /*6000*/case LQP_RADIO:
            lame_set_preset( gfp, 112);
            break;

        /*7000*/case LQP_TAPE:
            lame_set_preset( gfp, 112);
            break;

        /*8000*/case LQP_HIFI:
            lame_set_preset( gfp, 160);
            break;

        /*9000*/case LQP_CD:
            lame_set_preset( gfp, 192);
            break;

        /*10000*/case LQP_STUDIO:
            lame_set_preset( gfp, 256);
            break;

    }
}


/**
    \brief Closes a stream, and records its handle as released, so that
           beCloseStream() and beWriteInfoTag() do not use it again.

    \param gfp the stream to close. If beWriteVBRHeader() would write to
               this stream, its reference to the stream is cleared too.
*/
static void release_stream( lame_global_flags* gfp )
{
    lame_close( gfp );
    gfp_released = gfp;
    if ( gfp == gfp_save )
    {
        gfp_save = NULL;
    }
}

/**
 * \internal
 * \brief Returns the ABR bitrate for a bitrate in bit/s: rounded to the
 *        nearest kbit/s, within the range LAME encodes, 8 to 320 kbit/s.
 * \param bps  the bitrate in bit/s.
 * \return the bitrate in kbit/s.
 */
static int
abr_kbps_from_bps(DWORD bps)
{
    DWORD const kbps = bps / 1000 + (bps % 1000 >= 500 ? 1 : 0);

    if (kbps > 320)
        return 320;
    if (kbps < 8)
        return 8;
    return (int) kbps;
}

/**
 * \internal
 * \brief Turns the result of an encode or flush call into the result of the
 *        DLL function.
 * \param nOutputBytes  what the call returned: the bytes it wrote, or a
 *                      negative error.
 * \param pdwOutput     receives the bytes written, or 0 after an error.
 * \return BE_ERR_SUCCESSFUL, or BE_ERR_BUFFER_TOO_SMALL for every negative
 *         result.
 */
static BE_ERR
report_output(int nOutputBytes, PDWORD pdwOutput)
{
    if (nOutputBytes < 0) {
        *pdwOutput = 0;
        return BE_ERR_BUFFER_TOO_SMALL;
    }
    *pdwOutput = (DWORD) nOutputBytes;
    return BE_ERR_SUCCESSFUL;
}

/**
 * \internal
 * \brief Returns the output buffer size for one encode or flush call of a
 *        stream: lame.h's figure for one frame, counted at the output rate,
 *        since upsampling multiplies what a call returns.
 * \param gfp  the stream, after lame_init_params().
 * \return the size in bytes.
 */
static DWORD
mp3_buffer_size(lame_global_flags *gfp)
{
    DWORD const samples = (DWORD) lame_get_framesize( gfp );
    int const in_rate = lame_get_in_samplerate( gfp );
    int const out_rate = lame_get_out_samplerate( gfp );
    double ratio = 1;

    if ( in_rate > 0 && out_rate > in_rate )
        ratio = (double) out_rate / in_rate;
    return (DWORD)( 1.25 * samples * ratio + 7200 );
}

__declspec(dllexport) BE_ERR	beInitStream(PBE_CONFIG pbeConfig, PDWORD dwSamples, PDWORD dwBufferSize, PHBE_STREAM phbeStream)
{
    int actual_bitrate;
    //2001-12-18
    BE_CONFIG			lameConfig = { 0, };
    int					nInitReturn = 0;
    lame_global_flags*	gfp = NULL;

    // Init the global flags structure
    gfp = lame_init();
    *phbeStream = (HBE_STREAM)gfp;
    if ( gfp == gfp_released )
    {
        gfp_released = NULL;
    }

    // clear out structure
    memset(&lameConfig,0x00,CURRENT_STRUCT_SIZE);

    // Check if this is a regular BLADE_ENCODER header
    if (pbeConfig->dwConfig!=BE_CONFIG_LAME)
    {
        int nCRC=pbeConfig->format.mp3.bCRC;
        int nVBR=(nCRC>>12)&0x0F;

        // Copy parameter from old Blade structure
        lameConfig.format.LHV1.dwSampleRate	=pbeConfig->format.mp3.dwSampleRate;
        //for low bitrates, LAME will automatically downsample for better
        //sound quality.  Forcing output samplerate = input samplerate is not a good idea 
        //unless the user specifically requests it:
        //lameConfig.format.LHV1.dwReSampleRate=pbeConfig->format.mp3.dwSampleRate;
        lameConfig.format.LHV1.nMode		=(pbeConfig->format.mp3.byMode&0x0F);
        lameConfig.format.LHV1.dwBitrate	=pbeConfig->format.mp3.wBitrate;
        lameConfig.format.LHV1.bPrivate		=pbeConfig->format.mp3.bPrivate;
        lameConfig.format.LHV1.bOriginal	=pbeConfig->format.mp3.bOriginal;
        lameConfig.format.LHV1.bCRC		=nCRC&0x01;
        lameConfig.format.LHV1.bCopyright	=pbeConfig->format.mp3.bCopyright;

        // Fill out the unknowns
        lameConfig.format.LHV1.dwStructSize=CURRENT_STRUCT_SIZE;
        lameConfig.format.LHV1.dwStructVersion=CURRENT_STRUCT_VERSION;

        // Get VBR setting from fourth nibble
        if ( nVBR>0 )
        {
            lameConfig.format.LHV1.bWriteVBRHeader = TRUE;
            lameConfig.format.LHV1.bEnableVBR = TRUE;
            lameConfig.format.LHV1.nVBRQuality = nVBR-1;
        }

        // Get Quality from third nibble
        lameConfig.format.LHV1.nPreset=((nCRC>>8)&0x0F);

    }
    else
    {
        // Copy the parameters
        memcpy(&lameConfig,pbeConfig,Min(pbeConfig->format.LHV1.dwStructSize,sizeof(lameConfig)));
    }

    // --------------- Set arguments to LAME encoder -------------------------

    // Set input sample frequency
    lame_set_in_samplerate( gfp, lameConfig.format.LHV1.dwSampleRate );

    // disable INFO/VBR tag by default.  
    // if this tag is used, the calling program must call beWriteVBRTag()
    // after encoding.  But the original DLL documentation does not 
    // require the 
    // app to call beWriteVBRTag() unless they have specifically
    // set LHV1.bWriteVBRHeader=TRUE.  Thus the default setting should
    // be disabled.  
    lame_set_bWriteVbrTag( gfp, 0 );

    //2001-12-18 Dibrom's ABR preset stuff

    if(lameConfig.format.LHV1.nPreset == LQP_ABR)		// --ALT-PRESET ABR
    {
        actual_bitrate = abr_kbps_from_bps(lameConfig.format.LHV1.dwVbrAbr_bps);

        lame_set_preset( gfp, actual_bitrate );
    }    

    // end Dibrom's ABR preset 2001-12-18 ****** START OF CBR

    if(lameConfig.format.LHV1.nPreset == LQP_CBR)		// --ALT-PRESET CBR
    {
        actual_bitrate = lameConfig.format.LHV1.dwBitrate;
        lame_set_preset(gfp, actual_bitrate);
        lame_set_VBR(gfp, vbr_off);
    }

    // end Dibrom's CBR preset 2001-12-18

    // The following settings only used when preset is not one of the LAME QUALITY Presets
    if ( (int)lameConfig.format.LHV1.nPreset < (int) LQP_STANDARD )
    {
        switch ( lameConfig.format.LHV1.nMode )
        {
        case BE_MP3_MODE_STEREO:
            lame_set_mode( gfp, STEREO );
            lame_set_num_channels( gfp, 2 );
            break;
        case BE_MP3_MODE_JSTEREO:
            lame_set_mode( gfp, JOINT_STEREO );
            //lame_set_force_ms( gfp, bForceMS ); // no check box to force this?
            lame_set_num_channels( gfp, 2 );
            break;
        case BE_MP3_MODE_MONO:
            lame_set_mode( gfp, MONO );
            lame_set_num_channels( gfp, 1 );
            break;
        case BE_MP3_MODE_DUALCHANNEL:
            lame_set_mode( gfp, DUAL_CHANNEL );
            lame_set_num_channels( gfp, 2 );
            break;
        default:
            {
                DebugPrintf("Invalid lameConfig.format.LHV1.nMode, value is %d\n",lameConfig.format.LHV1.nMode);
                release_stream( gfp );
                *phbeStream = NULL;
                return BE_ERR_INVALID_FORMAT_PARAMETERS;
            }
        }

        if ( lameConfig.format.LHV1.bEnableVBR )
        {
            /* set VBR quality */
            lame_set_VBR_q( gfp, lameConfig.format.LHV1.nVBRQuality );

            /* select proper VBR method */
            switch ( lameConfig.format.LHV1.nVbrMethod)
            {
            case VBR_METHOD_NONE:
                lame_set_VBR( gfp, vbr_off );
                break;

            case VBR_METHOD_DEFAULT:
                lame_set_VBR( gfp, vbr_default ); 
                break;

            case VBR_METHOD_OLD:
                lame_set_VBR( gfp, vbr_rh ); 
                break;

            case VBR_METHOD_MTRH:
            case VBR_METHOD_NEW:
                /*                                
                * the --vbr-mtrh commandline switch is obsolete. 
                * now --vbr-mtrh is known as --vbr-new
                */
                lame_set_VBR( gfp, vbr_mtrh ); 
                break;

            case VBR_METHOD_ABR:
                lame_set_VBR( gfp, vbr_abr ); 
                break;

            default:
                DebugPrintf("Invalid lameConfig.format.LHV1.nVbrMethod, value is %d\n",lameConfig.format.LHV1.nVbrMethod);
                release_stream( gfp );
                *phbeStream = NULL;
                return BE_ERR_INVALID_FORMAT_PARAMETERS;
            }
        }
        else
        {
            /* use CBR encoding method, so turn off VBR */
            lame_set_VBR( gfp, vbr_off );
        }

        /* Set bitrate.  (CDex users always specify bitrate=Min bitrate when using VBR) */
        lame_set_brate( gfp, lameConfig.format.LHV1.dwBitrate );

        /* check if we have to use ABR, in order to backwards compatible, this
        * condition should still be checked indepedent of the nVbrMethod method
        */
        if (lameConfig.format.LHV1.dwVbrAbr_bps > 0 )
        {
            /* set VBR method to ABR */
            lame_set_VBR( gfp, vbr_abr );

            lame_set_VBR_mean_bitrate_kbps( gfp,
                abr_kbps_from_bps( lameConfig.format.LHV1.dwVbrAbr_bps ) );
        }

    }

    // First set all the preset options
    if ( LQP_NOPRESET !=  lameConfig.format.LHV1.nPreset )
    {
        PresetOptions( gfp, lameConfig.format.LHV1.nPreset );
    }


    // Set frequency resampling rate, if specified
    if ( lameConfig.format.LHV1.dwReSampleRate > 0 )
    {
        lame_set_out_samplerate( gfp, lameConfig.format.LHV1.dwReSampleRate );
    }


    switch ( lameConfig.format.LHV1.nMode )
    {
    case BE_MP3_MODE_MONO:
        lame_set_mode( gfp, MONO );
        lame_set_num_channels( gfp, 1 );
        break;

    default:
        break;
    }


    // Use strict ISO encoding?
    lame_set_strict_ISO( gfp, ( lameConfig.format.LHV1.bStrictIso ) ? 1 : 0 );

    // Set copyright flag?
    if ( lameConfig.format.LHV1.bCopyright )
    {
        lame_set_copyright( gfp, 1 );
    }

    // Do we have to tag  it as non original 
    if ( !lameConfig.format.LHV1.bOriginal )
    {
        lame_set_original( gfp, 0 );
    }
    else
    {
        lame_set_original( gfp, 1 );
    }

    // Add CRC?
    if ( lameConfig.format.LHV1.bCRC )
    {
        lame_set_error_protection( gfp, 1 );
    }
    else
    {
        lame_set_error_protection( gfp, 0 );
    }

    // Set private bit?
    if ( lameConfig.format.LHV1.bPrivate )
    {
        lame_set_extension( gfp, 1 );
    }
    else
    {
        lame_set_extension( gfp, 0 );
    }


    // Set VBR min bitrate, if specified
    if ( lameConfig.format.LHV1.dwBitrate > 0 )
    {
        lame_set_VBR_min_bitrate_kbps( gfp, lameConfig.format.LHV1.dwBitrate );
    }

    // Set Maxbitrate, if specified
    if ( lameConfig.format.LHV1.dwMaxBitrate > 0 )
    {
        lame_set_VBR_max_bitrate_kbps( gfp, lameConfig.format.LHV1.dwMaxBitrate );
    }
    // Set bit resovoir option
    if ( lameConfig.format.LHV1.bNoRes )
    {
        lame_set_disable_reservoir( gfp,1 );
    }

    // check if the VBR tag is required
    if ( lameConfig.format.LHV1.bWriteVBRHeader ) 
    {
        lame_set_bWriteVbrTag( gfp, 1 );
    }
    else
    {
        lame_set_bWriteVbrTag( gfp, 0 );
    }

    // Override Quality setting, use HIGHBYTE = NOT LOWBYTE to be backwards compatible
    if (	( lameConfig.format.LHV1.nQuality & 0xFF ) ==
        ((~( lameConfig.format.LHV1.nQuality >> 8 )) & 0xFF) )
    {
        lame_set_quality( gfp, lameConfig.format.LHV1.nQuality & 0xFF );
    }

    // The library's messages, into the log; lame_init_params() takes them over
    lame_set_msgf( gfp, DebugVPrintf );

    if ( 0 != ( nInitReturn = lame_init_params( gfp ) ) )
    {
        release_stream( gfp );
        *phbeStream = NULL;
        return nInitReturn;
    }

    // One frame of samples per call, for all channels
    *dwSamples = lame_get_framesize( gfp ) * lame_get_num_channels( gfp );

    *dwBufferSize = mp3_buffer_size( gfp );

    // The settings, into the log
    lame_print_config( gfp );
    lame_print_internals( gfp );

    // Everything went OK, thus return SUCCESSFUL
    return BE_ERR_SUCCESSFUL;
}



__declspec(dllexport) BE_ERR	beFlushNoGap(HBE_STREAM hbeStream, PBYTE pOutput, PDWORD pdwOutput)
{
    int nOutputSamples = 0;

    lame_global_flags*	gfp = (lame_global_flags*)hbeStream;

    // Init the global flags structure
    nOutputSamples = lame_encode_flush_nogap( gfp, pOutput, (int) mp3_buffer_size( gfp ) );

    return report_output( nOutputSamples, pdwOutput );
}

__declspec(dllexport) BE_ERR	beDeinitStream(HBE_STREAM hbeStream, PBYTE pOutput, PDWORD pdwOutput)
{
    int nOutputSamples = 0;

    lame_global_flags*	gfp = (lame_global_flags*)hbeStream;

    nOutputSamples = lame_encode_flush( gfp, pOutput, (int) mp3_buffer_size( gfp ) );

    return report_output( nOutputSamples, pdwOutput );
}


__declspec(dllexport) BE_ERR	beCloseStream(HBE_STREAM hbeStream)
{
    lame_global_flags*	gfp = (lame_global_flags*)hbeStream;

    // released already, by beWriteInfoTag() or an earlier close
    if ( gfp == gfp_released )
    {
        return BE_ERR_SUCCESSFUL;
    }

    // lame will be close in VbrWriteTag function
    if ( !lame_get_bWriteVbrTag( gfp ) )
    {
        // clean up of allocated memory
        release_stream( gfp );

        gfp_save = NULL;
    }
    else
    {
        gfp_save = (lame_global_flags*)hbeStream;
    }

    // DeInit encoder
    return BE_ERR_SUCCESSFUL;
}



__declspec(dllexport) VOID		beVersion(PBE_VERSION pbeVersion)
{
    // DLL Release date: __DATE__ is "Mmm dd yyyy"
    static const char months[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const char *const lpszDate = __DATE__;
    lame_version_t lv   = { 0, };
    int i;


    // Set DLL interface version
    pbeVersion->byDLLMajorVersion=MAJORVERSION;
    pbeVersion->byDLLMinorVersion=MINORVERSION;

    get_lame_version_numerical ( &lv );

    // Set Engine version number (Same as Lame version)
    pbeVersion->byMajorVersion = (BYTE)lv.major;
    pbeVersion->byMinorVersion = (BYTE)lv.minor;
    pbeVersion->byAlphaLevel   = (BYTE)lv.alpha;
    pbeVersion->byBetaLevel    = (BYTE)lv.beta;

    pbeVersion->byMMXEnabled=0; /* no MMX-specific code remains */

    memset( pbeVersion->btReserved, 0, sizeof( pbeVersion->btReserved ) );

    // Set month
    pbeVersion->byMonth=1;
    for ( i = 0; i < (int) ( sizeof months / sizeof months[0] ); i++ )
    {
        if ( strncmp( lpszDate, months[i], sizeof months[i] - 1 ) == 0 )
            pbeVersion->byMonth = (BYTE) ( i + 1 );
    }

    // Get day of month string (char [4..5])
    pbeVersion->byDay = (BYTE) atoi( lpszDate + 4 );

    // Get year of compilation date (char [7..10])
    pbeVersion->wYear = (WORD) atoi( lpszDate + 7 );

    memset( pbeVersion->zHomepage, 0x00, BE_MAX_HOMEPAGE );

    snprintf( pbeVersion->zHomepage, sizeof pbeVersion->zHomepage, "%s", "https://lame.sourceforge.io/" );
}

__declspec(dllexport) BE_ERR	beEncodeChunk(HBE_STREAM hbeStream, DWORD nSamples, 
                                              PSHORT pSamples, PBYTE pOutput, PDWORD pdwOutput)
{
    // Encode it
    int dwSamples;
    int	nOutputSamples = 0;
    lame_global_flags*	gfp = (lame_global_flags*)hbeStream;

    dwSamples = nSamples / lame_get_num_channels( gfp );

    // old versions of lame_enc.dll required exactly 1152 samples
    // and worked even if nSamples accidently set to 2304 
    // simulate this behavoir:
    if ( 1 == lame_get_num_channels( gfp ) && nSamples == 2304)
    {
        dwSamples/= 2;
    }


    if ( 1 == lame_get_num_channels( gfp ) )
    {
        nOutputSamples = lame_encode_buffer(gfp,pSamples,pSamples,dwSamples,pOutput,(int) mp3_buffer_size( gfp ));
    }
    else
    {
        nOutputSamples = lame_encode_buffer_interleaved(gfp,pSamples,dwSamples,pOutput,(int) mp3_buffer_size( gfp ));
    }


    return report_output( nOutputSamples, pdwOutput );
}


// accept floating point audio samples, scaled to the range of a signed 16-bit
//  integer (within +/- 32768), in non-interleaved channels  -- DSPguru, jd
__declspec(dllexport) BE_ERR	beEncodeChunkFloatS16NI(HBE_STREAM hbeStream, DWORD nSamples, 
                                                        PFLOAT buffer_l, PFLOAT buffer_r, PBYTE pOutput, PDWORD pdwOutput)
{
    int nOutputSamples;
    lame_global_flags*	gfp = (lame_global_flags*)hbeStream;

    nOutputSamples = lame_encode_buffer_float(gfp,buffer_l,buffer_r,nSamples,pOutput,(int) mp3_buffer_size( gfp ));

    return report_output( nOutputSamples, pdwOutput );
}

static int
maybeSyncWord(FILE* fpStream)
{
    unsigned char mp3_frame_header[4];
    size_t nbytes = fread(mp3_frame_header, 1, sizeof(mp3_frame_header), fpStream);
    if ( nbytes != sizeof(mp3_frame_header) ) {
        return -1;
    }
    if ( !lametag_is_frame_sync(mp3_frame_header) ) {
        return -1; /* doesn't look like a sync word */
    }
    return 0;
}

static int
skipId3v2(FILE * fpStream, size_t lametag_frame_size)
{
    size_t  nbytes;
    /* fseek() takes a long, which is 32 bits on Windows whatever the word
       size is, so the offset is held in one. The tag size field is 28 bits,
       so it fits. */
    long    id3v2TagSize = 0;
    unsigned char id3v2Header[ID3V2_HEADER_BYTES];

    /* seek to the beginning of the stream */
    if (fseek(fpStream, 0, SEEK_SET) != 0) {
        return -2;  /* not seekable, abort */
    }
    /* read 10 bytes in case there's an ID3 version 2 header here */
    nbytes = fread(id3v2Header, 1, sizeof(id3v2Header), fpStream);
    if (nbytes != sizeof(id3v2Header)) {
        return -3;  /* not readable, maybe opened Write-Only */
    }
    id3v2TagSize = lametag_audio_offset(id3v2Header);
    /* Seek to the beginning of the audio stream */
    if ( fseek(fpStream, id3v2TagSize, SEEK_SET) != 0 ) {
        return -2;
    }
    if ( maybeSyncWord(fpStream) != 0) {
        return -1;
    }
    /* The frame size comes from the caller, so the sum is bounded rather
       than assumed to fit. A LAME tag frame is one MPEG frame and cannot
       come close, which is why this returns the not-seekable answer. */
    if ( lametag_frame_size > (size_t)(LONG_MAX - id3v2TagSize) ) {
        return -2;
    }
    if ( fseek(fpStream, id3v2TagSize + (long) lametag_frame_size, SEEK_SET) != 0 ) {
        return -2;
    }
    if ( maybeSyncWord(fpStream) != 0) {
        return -1;
    }
    /* OK, it seems we found our LAME-Tag/Xing frame again */
    /* Seek to the beginning of the audio stream */
    if ( fseek(fpStream, id3v2TagSize, SEEK_SET) != 0 ) {
        return -2;
    }
    return 0;
}

static BE_ERR
updateLameTagFrame(lame_global_flags* gfp, FILE* fpStream)
{
    size_t n = lame_get_lametag_frame( gfp, 0, 0 ); /* ask for bufer size */

    if ( n > 0 )
    {
        unsigned char* buffer = 0;
        size_t m = 1;

        if ( 0 != skipId3v2(fpStream, n) ) 
        {
            DispErr( "Error updating LAME-tag frame:\n\n"
                     "can't locate old frame\n" );
            return BE_ERR_INVALID_FORMAT_PARAMETERS;
        }

        buffer = (unsigned char*)malloc( n );

        if ( buffer == 0 ) 
        {
            DispErr( "Error updating LAME-tag frame:\n\n"
                     "can't allocate frame buffer\n" );
            return BE_ERR_INVALID_FORMAT_PARAMETERS;
        }

        /* Put it all to disk again */
        n = lame_get_lametag_frame( gfp, buffer, n );
        if ( n > 0 ) 
        {
            m = fwrite( buffer, n, 1, fpStream );        
        }
        free( buffer );

        if ( m != 1 ) 
        {
            DispErr( "Error updating LAME-tag frame:\n\n"
                     "couldn't write frame into file\n" );
            return BE_ERR_INVALID_FORMAT_PARAMETERS;
        }
    }
    return BE_ERR_SUCCESSFUL;
}

__declspec(dllexport) BE_ERR beWriteInfoTag( HBE_STREAM hbeStream,
                                            LPCSTR lpszFileName )
{
    FILE* fpStream	= NULL;
    BE_ERR beResult	= BE_ERR_SUCCESSFUL;

    lame_global_flags*	gfp = (lame_global_flags*)hbeStream;

    if ( NULL != gfp && gfp == gfp_released )
    {
        return BE_ERR_INVALID_HANDLE;
    }
    if ( NULL != gfp )
    {
        // Do we have to write the VBR tag?
        if ( lame_get_bWriteVbrTag( gfp ) )
        {
            // Try to open the file
            fpStream=fopen( lpszFileName, "rb+" );

            // Check file open result
            if ( NULL == fpStream )
            {
                beResult = BE_ERR_INVALID_FORMAT_PARAMETERS;
                DispErr( "Error updating LAME-tag frame:\n\n"
                         "can't open file for reading and writing\n" );
            }
            else
            {
                beResult = updateLameTagFrame( gfp, fpStream );

                // Close the file stream
                fclose( fpStream );
            }
        }

        // clean up of allocated memory
        release_stream( gfp );
    }
    else
    {
        beResult = BE_ERR_INVALID_FORMAT_PARAMETERS;
    }

    // return result
    return beResult;
}

// for backwards compatiblity
__declspec(dllexport) BE_ERR beWriteVBRHeader(LPCSTR lpszFileName)
{
    return beWriteInfoTag( (HBE_STREAM)gfp_save, lpszFileName );
}


BOOL APIENTRY DllMain(HANDLE hModule, 
                      DWORD  ul_reason_for_call, 
                      LPVOID lpReserved)
{
    (void) lpReserved;
    gs_hModule = (HMODULE) hModule;

    switch( ul_reason_for_call )
    {
    case DLL_PROCESS_ATTACH:
        // Enable debug/logging?
        gs_bLogFile = GetPrivateProfileIntA("Debug","WriteLogFile",gs_bLogFile,"lame_enc.ini");
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


static void DispErr(char const* strErr)
{
    MessageBoxA(NULL,strErr,"LAME_ENC.DLL",MB_OK|MB_ICONHAND);
}

#ifdef	__cplusplus
}
#endif
