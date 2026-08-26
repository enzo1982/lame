/*
 *	Lame time routines source file
 *
 *	Copyright (c) 2000 Mark Taylor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

/*
 * name:        GetCPUTime ( void )
 *
 * description: returns CPU time used by the process
 * input:       none
 * output:      time in seconds
 * known bugs:  may not work in SMP and RPC
 * conforming:  ANSI C
 *
 * There is some old difficult to read code at the end of this file.
 * Can someone integrate this into this function (if useful)?
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <time.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "lametime.h"

#if !defined(CLOCKS_PER_SEC)
# warning Your system does not define CLOCKS_PER_SEC, guessing one...
# define CLOCKS_PER_SEC 1000000
#endif


double
GetCPUTime(void)
{
    clock_t t;

#if defined(_MSC_VER)  ||  defined(__BORLANDC__)
    t = clock();
#else
    t = clock();
#endif
    return t / (double) CLOCKS_PER_SEC;
}


/*
 * name:        GetRealTime ( void )
 *
 * description: returns real (human) time elapsed relative to a fixed time (mostly 1970-01-01 00:00:00)
 * input:       none
 * output:      time in seconds
 * known bugs:  bad precision with time()
 */

#if defined(__unix__)  ||  defined(SVR4)  ||  defined(BSD)

# include <sys/time.h>
# include <unistd.h>

double
GetRealTime(void)
{                       /* conforming:  SVr4, BSD 4.3 */
    struct timeval t;

    if (0 != gettimeofday(&t, NULL))
        assert(0);
    return t.tv_sec + 1.e-6 * t.tv_usec;
}

#elif defined(WIN16)  ||  defined(WIN32)

# include <stdio.h>
# include <sys/types.h>
# include <sys/timeb.h>

double
GetRealTime(void)
{                       /* conforming:  Win 95, Win NT */
    struct timeb t;

    ftime(&t);
    return t.time + 1.e-3 * t.millitm;
}

#else

double
GetRealTime(void)
{                       /* conforming:  SVr4, SVID, POSIX, X/OPEN, BSD 4.3 */ /* BUT NOT GUARANTEED BY ANSI */
    time_t  t;

    t = time(NULL);
    return (double) t;
}

#endif


#if defined(_WIN32) || defined(__CYGWIN__)
# include <io.h>
# include <fcntl.h>
#else
# include <unistd.h>
#endif

int
lame_set_stream_binary_mode(FILE * const fp)
{
#if   defined __EMX__
    _fsetmode(fp, "b");
#elif defined __BORLANDC__
    setmode(_fileno(fp), O_BINARY);
#elif defined __CYGWIN__
    setmode(fileno(fp), O_BINARY);
#elif defined _WIN32
    _setmode(_fileno(fp), _O_BINARY);
#else
    (void) fp;          /* doing nothing here, silencing the compiler only. */
#endif
    return 0;
}


#include <sys/stat.h>

#if defined(HAVE_UTIME_H)
# include <utime.h>
#elif defined(HAVE_SYS_UTIME_H)
# include <sys/utime.h>
#endif

#if defined(HAVE_UTIME) && (defined(HAVE_UTIME_H) || defined(HAVE_SYS_UTIME_H))
# define LAME_HAVE_FILE_TIMES 1
#endif

/**
 * @internal
 * @brief Remembers the access and modification times a file carries.
 *
 * Called before anything opens the file: reading a file updates its access
 * time, so times taken afterwards describe the reader rather than the file.
 *
 * @param path   the file to read the times from.
 * @param times  receives them, and is marked invalid where they could not be
 *               read or where this build cannot write them again anyway.
 * @return 0 on success, -1 otherwise.
 */
int
lame_read_file_times(char const *path, lame_file_times * times)
{
    if (times == 0) {
        return -1;
    }
    times->valid = 0;
    times->actime = 0;
    times->modtime = 0;
#ifdef LAME_HAVE_FILE_TIMES
    {
        struct stat source;

        if (path == 0 || stat(path, &source) != 0) {
            return -1;
        }
        times->actime = source.st_atime;
        times->modtime = source.st_mtime;
        times->valid = 1;
        return 0;
    }
#else
    (void) path;        /* the system has no way to set them later, so there */
    return -1;          /* is nothing to be gained by reading them now.      */
#endif
}


/**
 * @internal
 * @brief Gives a file the times remembered by lame_read_file_times().
 *
 * @param path   the file to stamp.
 * @param times  times previously read; an invalid set is refused rather than
 *               applied, so a failed read cannot become a wrong stamp.
 * @return 0 on success, -1 otherwise, including where the platform offers no
 *         way to set file times.
 */
int
lame_write_file_times(char const *path, lame_file_times const *times)
{
#ifdef LAME_HAVE_FILE_TIMES
    struct utimbuf when;

    if (path == 0 || times == 0 || times->valid == 0) {
        return -1;
    }
    when.actime = times->actime;
    when.modtime = times->modtime;
    return utime(path, &when) == 0 ? 0 : -1;
#else
    (void) path;        /* the system has no way to set them; say so rather */
    (void) times;       /* than report a success nothing performed.         */
    return -1;
#endif
}


/* End of lametime.c */
