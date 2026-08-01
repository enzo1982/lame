/*
 *      rtp socket communication functions
 *
 *      initially contributed by Felix von Leitner
 *
 *      Copyright (c) 2000 Mark Taylor
 *                    2010 Robert Hegemann
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

/* $Id$ */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

struct rtpbits {
    int     sequence:16;     /* sequence number: random */
    int     pt:7;            /* payload type: 14 for MPEG audio */
    int     m:1;             /* marker: 0 */
    int     cc:4;            /* number of CSRC identifiers: 0 */
    int     x:1;             /* number of extension headers: 0 */
    int     p:1;             /* is there padding appended: 0 */
    int     v:2;             /* version: 2 */
};

struct rtpheader {           /* in network byte order */
    struct rtpbits b;
    int     timestamp;       /* start: random */
    int     ssrc;            /* random */
    int     iAudioHeader;    /* =0?! */
};


/* One implementation for every platform. What differs between a Windows and a
 * POSIX system is the spelling of a handful of calls and the way an error is
 * reported, not what this code does, and the two used to differ in what it
 * does: only one of them resolved names or handled IPv6, only one of them
 * enabled broadcast, only one of them set the multicast loopback option.
 */
#if defined(_WIN32) || defined(__MINGW32__)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define rtp_close_socket(s)  closesocket(s)

#else

#ifdef STDC_HEADERS
# include <stdio.h>
# include <stdarg.h>
# include <stdlib.h>
# include <string.h>
#else
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

typedef int SOCKET;
#define INVALID_SOCKET       (-1)
#define SOCKET_ERROR         (-1)
#define rtp_close_socket(s)  close(s)

#endif

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "rtp.h"
#include "console.h"

#define MAX_PORT_LENGTH 6       /* "65535" and its terminator */

struct rtpheader RTPheader;
SOCKET  rtpsocket;


/* Report the last socket error the way the platform can describe it. */
#if defined(_WIN32) || defined(__MINGW32__)
static void
report_socket_error(char const *what)
{
    int     code = WSAGetLastError();
    void   *p_msg_buf = NULL;
    char   *msg;

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                   (void *) 0,
                   (DWORD) code,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) & p_msg_buf, 0, NULL);
    msg = p_msg_buf ? strdup(p_msg_buf) : NULL;
    if (p_msg_buf)
        LocalFree(p_msg_buf);
    error_printf("%s failed, error %d\n%s\n", what, code, msg ? msg : "");
    free(msg);
}
#else
static void
report_socket_error(char const *what)
{
    error_printf("%s failed, error %d\n%s\n", what, errno, strerror(errno));
}
#endif


/* Print a resolved address into a caller supplied buffer, either family. */
static char const *
address_text(struct sockaddr const *sa, char *buf, size_t buflen)
{
    void   *addr;

#ifdef IPV6
    if (sa->sa_family == AF_INET6)
        addr = (void *) &((struct sockaddr_in6 *) sa)->sin6_addr;
    else
#endif
        addr = (void *) &((struct sockaddr_in *) sa)->sin_addr;

    if (inet_ntop(sa->sa_family, addr, buf, buflen) == NULL)
        return "?";
    return buf;
}


static int
is_multicast(struct sockaddr const *sa)
{
#ifdef IPV6
    if (sa->sa_family == AF_INET6)
        return ((struct sockaddr_in6 *) sa)->sin6_addr.s6_addr[0] == 0xFF;
#endif
    return (ntohl(((struct sockaddr_in *) sa)->sin_addr.s_addr) >> 28) == 0xE;
}


/* IPv6 has no broadcast, so this stays an IPv4 question. */
static int
is_broadcast(struct sockaddr const *sa)
{
    if (sa->sa_family != AF_INET)
        return 0;
    return ntohl(((struct sockaddr_in *) sa)->sin_addr.s_addr) == INADDR_BROADCAST;
}


/* create a sender socket. */
int
rtp_socket(char const *address, unsigned int port, unsigned int TTL)
{
    int     on = 1;
    int     ttl = (int) TTL;
    char    port_text[MAX_PORT_LENGTH];
    char    addr_text[INET6_ADDRSTRLEN];
    int     error;
    struct addrinfo hint, *dest = NULL;
    struct sockaddr_storage source;
    SOCKET  s = INVALID_SOCKET;

    if (port == 0 || port > 0xffff) {
        error_printf("rtp_socket() Invalid port number.\n");
        return 1;
    }
    snprintf(port_text, sizeof(port_text), "%u", port);

    memset(&hint, 0, sizeof(hint));
#ifdef IPV6
    hint.ai_family = AF_UNSPEC;
#else
    hint.ai_family = AF_INET;
#endif
    hint.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(address, port_text, &hint, &dest) != 0) {
        error_printf("Unknown host %s\n", address);
        return 1;
    }

    s = socket(dest->ai_family, dest->ai_socktype, 0);
    if (s == INVALID_SOCKET) {
        report_socket_error("socket()");
        goto err_cleanup;
    }

    /* The wildcard local address of whichever family was resolved: zeroed
       storage is INADDR_ANY and in6addr_any alike, on port zero. */
    memset(&source, 0, sizeof(source));
    source.ss_family = (unsigned short) dest->ai_family;

    error = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char const *) &on, sizeof(on));
    if (error == SOCKET_ERROR) {
        report_socket_error("setsockopt(SO_REUSEADDR)");
        goto err_cleanup;
    }
    error = bind(s, (struct sockaddr *) &source, (int) dest->ai_addrlen);
    if (error == SOCKET_ERROR) {
        report_socket_error("bind()");
        goto err_cleanup;
    }

    if (is_broadcast(dest->ai_addr)) {
        error_printf("broadcast %s:%u\n",
                     address_text(dest->ai_addr, addr_text, sizeof(addr_text)), port);
        error = setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char const *) &on, sizeof(on));
        if (error == SOCKET_ERROR) {
            report_socket_error("setsockopt(SO_BROADCAST)");
            goto err_cleanup;
        }
    }

    if (is_multicast(dest->ai_addr)) {
        int     level = IPPROTO_IP;
        int     opt_ttl = IP_MULTICAST_TTL;
        int     opt_loop = IP_MULTICAST_LOOP;

#ifdef IPV6
        if (dest->ai_family == AF_INET6) {
            level = IPPROTO_IPV6;
            opt_ttl = IPV6_MULTICAST_HOPS;
            opt_loop = IPV6_MULTICAST_LOOP;
        }
#endif
        error_printf("multicast %s:%u\n",
                     address_text(dest->ai_addr, addr_text, sizeof(addr_text)), port);
        error = setsockopt(s, level, opt_ttl, (char const *) &ttl, sizeof(ttl));
        if (error == SOCKET_ERROR) {
            report_socket_error("setsockopt(multicast TTL)");
            goto err_cleanup;
        }
        error = setsockopt(s, level, opt_loop, (char const *) &on, sizeof(on));
        if (error == SOCKET_ERROR) {
            report_socket_error("setsockopt(multicast loop)");
            goto err_cleanup;
        }
    }

    error = connect(s, dest->ai_addr, (int) dest->ai_addrlen);
    if (error == SOCKET_ERROR) {
        report_socket_error("connect()");
        goto err_cleanup;
    }

    freeaddrinfo(dest);
    rtpsocket = s;
    return 0;

err_cleanup:
    if (s != INVALID_SOCKET)
        rtp_close_socket(s);
    freeaddrinfo(dest);
    return 1;
}


#if defined(_WIN32) || defined(__MINGW32__)
static void
rtp_initialization_extra(void)
{
    WSADATA wsaData;
    int     rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc != 0)
        error_printf("WSAStartup() failed, error %d\n", rc);
}

static void
rtp_close_extra(void)
{
    WSACleanup();
}
#else
static void
rtp_initialization_extra(void)
{
}

static void
rtp_close_extra(void)
{
}
#endif


static int
rtp_send(unsigned char const *data, int len)
{
    SOCKET  s = rtpsocket;
    struct rtpheader *foo = &RTPheader;
    char   *buffer = malloc(len + sizeof(struct rtpheader));
    int    *cast = (int *) foo;
    int    *outcast = (int *) buffer;
    int     count, size;

    if (buffer == NULL)
        return -1;
    outcast[0] = htonl(cast[0]);
    outcast[1] = htonl(cast[1]);
    outcast[2] = htonl(cast[2]);
    outcast[3] = htonl(cast[3]);
    memmove(buffer + sizeof(struct rtpheader), data, len);
    size = len + sizeof(*foo);
    count = send(s, buffer, size, 0);
    free(buffer);

    return count != size;
}

void
rtp_output(unsigned char const *mp3buffer, int mp3size)
{
    rtp_send(mp3buffer, mp3size);
    RTPheader.timestamp += 5;
    RTPheader.b.sequence++;
}

void
rtp_initialization(void)
{
    struct rtpheader *foo = &RTPheader;
    foo->b.v = 2;
    foo->b.p = 0;
    foo->b.x = 0;
    foo->b.cc = 0;
    foo->b.m = 0;
    foo->b.pt = 14;     /* MPEG Audio */
    foo->b.sequence = rand() & 65535;
    foo->timestamp = rand();
    foo->ssrc = rand();
    foo->iAudioHeader = 0;
    rtp_initialization_extra();
}

void
rtp_deinitialization(void)
{
    rtp_close_extra();
}
