/**
 * @file
 * @ingroup unit_tests
 * @brief Unit tests for the destination handling of the RTP sender.
 *
 * @c frontend/rtp.c has one implementation for every platform. A destination
 * can be an IPv4 address, an IPv6 address or a host name. The code resolves
 * it the same way on every platform. A build configured for IPv4 only differs
 * in one point: the address family that it asks the resolver for. These tests
 * check this behavior.
 *
 * @c rtp_socket() is external, and @c rtpsocket is a non-static global. So the
 * test reads the connected address back with @c getpeername(). It does not
 * have to guess it. This is the main assertion: the family and the address
 * are the ones that the test asked for.
 *
 * One test also receives the packets that @c rtp_output() sends, in the same
 * process. It checks the RTP header of each packet. A unit test cannot
 * require loopback delivery. A host firewall can drop it, and on Windows it
 * often does between processes. So this test first probes the path. If no
 * packet gets through, the test skips and does not fail. Without the probe,
 * "no packets arrived" and "the sender is broken" look the same.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

/* Spelled the way rtp.c spells it, and that is a requirement rather than a
 * matter of taste: rtp.c is compiled into this program, so the two files have
 * to reach the same answer. They declare the same socket in terms of SOCKET,
 * which is Winsock's type on one side of this and a typedef below on the
 * other. MinGW targets Winsock like every other Windows compiler.
 */
#if defined(_WIN32) || defined(__MINGW32__)
# include <winsock2.h>
# include <ws2tcpip.h>
# define rtp_close_socket(s)  closesocket(s)
typedef int socket_len_t;
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <unistd.h>
# define INVALID_SOCKET       (-1)
# define rtp_close_socket(s)  close(s)
typedef int SOCKET;
typedef socklen_t socket_len_t;
#endif

#include "rtp.h"

/** The socket that rtp.c connected. The tests read it. */
extern SOCKET rtpsocket;

#define RTP_HEADER_LEN  16      /**< four 32 bit words, as rtp.c builds it */
#define PAYLOAD_LEN     32
#define FRAME_COUNT     5
#define RECV_TIMEOUT_MS 400

/**
 * @brief Replaces the console error function that rtp.c calls.
 *
 * The console layer of the frontend is a whole translation unit, and this
 * test does not need it. The test counts the messages and does not compare
 * their text. A rejected destination must print a message. The exact wording
 * does not matter.
 */
static int error_calls;

int
error_printf(const char *format, ...)
{
    (void) format;
    error_calls++;
    return 0;
}

/**
 * @brief Binds a receiver socket on @a host, and stores the port that the
 *        system chose in @a port.
 *
 * The function asks for port 0 and reads the assigned port back. A fixed port
 * number can race with another program. It can also collide with a program
 * that already uses that port. Port 0 avoids both problems.
 */
static SOCKET
open_receiver(int family, char const *host, unsigned int *port)
{
    struct addrinfo hint, *ai = NULL;
    struct sockaddr_storage sa;
    socket_len_t len = (socket_len_t) sizeof(sa);
    SOCKET  r;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = family;
    hint.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, "0", &hint, &ai) != 0)
        return INVALID_SOCKET;

    r = socket(ai->ai_family, ai->ai_socktype, 0);
    if (r != INVALID_SOCKET && bind(r, ai->ai_addr, (int) ai->ai_addrlen) != 0) {
        rtp_close_socket(r);
        r = INVALID_SOCKET;
    }
    freeaddrinfo(ai);
    if (r == INVALID_SOCKET)
        return r;

    if (getsockname(r, (struct sockaddr *) &sa, &len) != 0) {
        rtp_close_socket(r);
        return INVALID_SOCKET;
    }
    *port = (sa.ss_family == AF_INET6)
        ? ntohs(((struct sockaddr_in6 *) &sa)->sin6_port)
        : ntohs(((struct sockaddr_in *) &sa)->sin_port);
    return r;
}

static void
set_receive_timeout(SOCKET s)
{
#if defined(_WIN32) || defined(__MINGW32__)
    int     t = RECV_TIMEOUT_MS;
#else
    struct timeval t;
    t.tv_sec = RECV_TIMEOUT_MS / 1000;
    t.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char const *) &t, sizeof(t));
}

/** @brief Returns the peer address of @a s as text, and stores its family in @a family. */
static char const *
peer_of(SOCKET s, char *buf, size_t buflen, int *family)
{
    struct sockaddr_storage sa;
    socket_len_t len = (socket_len_t) sizeof(sa);
    void   *addr;

    *family = 0;
    if (getpeername(s, (struct sockaddr *) &sa, &len) != 0)
        return "";
    *family = sa.ss_family;
#ifdef IPV6
    if (sa.ss_family == AF_INET6)
        addr = (void *) &((struct sockaddr_in6 *) &sa)->sin6_addr;
    else
#endif
        addr = (void *) &((struct sockaddr_in *) &sa)->sin_addr;
    if (inet_ntop(sa.ss_family, addr, buf, buflen) == NULL)
        return "";
    return buf;
}

/**
 * @brief Checks that datagrams sent to @a host arrive within this process.
 *
 * The result decides what a later "nothing arrived" means: a fault in the
 * code, or a limit of the test environment.
 * @return 1 if all probe datagrams arrived, 0 otherwise.
 */
static int
path_works(int family, char const *host)
{
    unsigned int port = 0;
    SOCKET  r = open_receiver(family, host, &port);
    SOCKET  c;
    struct addrinfo hint, *ai = NULL;
    char    port_text[8], buf[64];
    int     i, got = 0;

    if (r == INVALID_SOCKET)
        return 0;
    set_receive_timeout(r);
    snprintf(port_text, sizeof(port_text), "%u", port);
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = family;
    hint.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port_text, &hint, &ai) != 0) {
        rtp_close_socket(r);
        return 0;
    }
    c = socket(ai->ai_family, ai->ai_socktype, 0);
    if (c != INVALID_SOCKET) {
        if (connect(c, ai->ai_addr, (int) ai->ai_addrlen) == 0) {
            for (i = 0; i < FRAME_COUNT; i++)
                send(c, "probe", 5, 0);
            while (recv(r, buf, sizeof(buf), 0) > 0)
                got++;
        }
        rtp_close_socket(c);
    }
    freeaddrinfo(ai);
    rtp_close_socket(r);
    return got == FRAME_COUNT;
}

/** @brief Closes the socket that the previous test left connected, if any. */
static int
teardown_socket(void **state)
{
    (void) state;
    if (rtpsocket != INVALID_SOCKET) {
        rtp_close_socket(rtpsocket);
        rtpsocket = INVALID_SOCKET;
    }
    return 0;
}

static int
group_setup(void **state)
{
    (void) state;
    rtp_initialization();       /* seeds the RTP header; WSAStartup on Windows */
    rtpsocket = INVALID_SOCKET;
    return 0;
}

static int
group_teardown(void **state)
{
    teardown_socket(state);
    rtp_deinitialization();
    return 0;
}

/** @brief Checks that an IPv4 literal connects, and to the address that it names. */
static void
test_ipv4_literal(void **state)
{
    unsigned int port = 0;
    SOCKET  r = open_receiver(AF_INET, "127.0.0.1", &port);
    char    text[INET6_ADDRSTRLEN];
    int     family = 0;

    if (r == INVALID_SOCKET)
        skip();
    (void) state;

    assert_int_equal(rtp_socket("127.0.0.1", port, 2), 0);
    assert_string_equal(peer_of(rtpsocket, text, sizeof(text), &family), "127.0.0.1");
    assert_int_equal(family, AF_INET);
    rtp_close_socket(r);
}

/**
 * @brief Checks an IPv6 literal. In an IPv4-only build, checks that the
 *        address is rejected.
 *
 * Each build has its own assertion, so the test is not empty in an IPv4-only
 * build. That build must reject the address. It must not accept it and do
 * something else with it.
 */
static void
test_ipv6_literal(void **state)
{
#ifdef IPV6
    unsigned int port = 0;
    SOCKET  r = open_receiver(AF_INET6, "::1", &port);
    char    text[INET6_ADDRSTRLEN];
    int     family = 0;

    if (r == INVALID_SOCKET)
        skip();                 /* a host with IPv6 compiled in but not up */
    (void) state;

    assert_int_equal(rtp_socket("::1", port, 2), 0);
    assert_string_equal(peer_of(rtpsocket, text, sizeof(text), &family), "::1");
    assert_int_equal(family, AF_INET6);
    rtp_close_socket(r);
#else
    (void) state;
    error_calls = 0;
    assert_int_not_equal(rtp_socket("::1", 5004, 2), 0);
    assert_true(error_calls > 0);
#endif
}

/**
 * @brief Returns 1 if this host resolves @a name, 0 otherwise.
 *
 * The test must ask this separately. Two cases look the same from outside:
 * "this machine has no localhost entry" and "the code under test cannot
 * resolve names". The test below exists to tell them apart. Only the first
 * case is a reason to skip.
 */
static int
host_resolves(char const *name)
{
    struct addrinfo hint, *ai = NULL;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(name, "5004", &hint, &ai) != 0)
        return 0;
    freeaddrinfo(ai);
    return 1;
}

/**
 * @brief Checks that a host name is resolved.
 *
 * A parser that accepts only address literals cannot do this.
 */
static void
test_host_name(void **state)
{
    char    text[INET6_ADDRSTRLEN];
    int     family = 0;
    char const *peer;
    unsigned int port = 0;
    SOCKET  r;

    /* Skip only for a reason that is about the machine. Once the resolver here
       has answered, a failure below is the code's, and asserted as such - an
       earlier draft skipped when rtp_socket() failed, which turned exactly the
       defect this test exists to catch into a pass. */
    if (!host_resolves("localhost"))
        skip();
    r = open_receiver(AF_INET, "127.0.0.1", &port);
    if (r == INVALID_SOCKET)
        skip();
    (void) state;

    /* "localhost" may resolve to either family, and rtp.c takes whichever the
       resolver returns first - so the assertion is that it reached a loopback
       address, not that it reached a particular one. */
    assert_int_equal(rtp_socket("localhost", port, 2), 0);
    peer = peer_of(rtpsocket, text, sizeof(text), &family);
    assert_true(family == AF_INET || family == AF_INET6);
    assert_true(strcmp(peer, "127.0.0.1") == 0 || strcmp(peer, "::1") == 0);
    rtp_close_socket(r);
}

/** @brief Checks that a destination that does not resolve is rejected with an error message. */
static void
test_unresolvable_is_refused(void **state)
{
    (void) state;
    error_calls = 0;
    /* Dotted and numeric, so it is the resolver rejecting it rather than any
       syntax check upstream. */
    assert_int_not_equal(rtp_socket("1.2.3.4.5", 5004, 2), 0);
    assert_true(error_calls > 0);
}

/** @brief Checks that a port outside the range 1 to 65535 is rejected and not truncated. */
static void
test_invalid_port_is_refused(void **state)
{
    (void) state;
    assert_int_not_equal(rtp_socket("127.0.0.1", 0, 2), 0);
    assert_int_not_equal(rtp_socket("127.0.0.1", 65536, 2), 0);
    assert_int_not_equal(rtp_socket("127.0.0.1", 0x10000000, 2), 0);
}

/**
 * @brief Checks that rtp_output() sends RTP packets.
 *
 * Each packet has version 2 and payload type 14 (MPEG audio). All packets of
 * the session have the same SSRC. The sequence number grows by one for each
 * packet. The payload follows the header without change.
 */
static void
test_sends_rtp_packets(void **state)
{
    unsigned char payload[PAYLOAD_LEN];
    unsigned char buf[512];
    unsigned int port = 0;
    SOCKET  r;
    int     i, n, received = 0;
    unsigned ssrc = 0, prev_seq = 0;

    if (!path_works(AF_INET, "127.0.0.1"))
        skip();                 /* the host will not carry it; not a verdict */
    (void) state;

    r = open_receiver(AF_INET, "127.0.0.1", &port);
    if (r == INVALID_SOCKET)
        skip();
    set_receive_timeout(r);
    assert_int_equal(rtp_socket("127.0.0.1", port, 2), 0);

    for (i = 0; i < FRAME_COUNT; i++) {
        memset(payload, 0, sizeof(payload));
        payload[0] = 0xFF;      /* an MPEG frame sync, so the payload is */
        payload[1] = 0xFB;      /* recognisable behind the header */
        payload[2] = (unsigned char) i;
        rtp_output(payload, PAYLOAD_LEN);
    }

    while ((n = recv(r, (char *) buf, sizeof(buf), 0)) > 0) {
        unsigned word = ((unsigned) buf[0] << 24) | ((unsigned) buf[1] << 16)
            | ((unsigned) buf[2] << 8) | buf[3];
        unsigned this_ssrc = ((unsigned) buf[8] << 24) | ((unsigned) buf[9] << 16)
            | ((unsigned) buf[10] << 8) | buf[11];
        unsigned seq = word & 0xFFFF;

        assert_int_equal(n, RTP_HEADER_LEN + PAYLOAD_LEN);
        assert_int_equal(word >> 30, 2);                /* version */
        assert_int_equal((word >> 16) & 0x7F, 14);      /* MPEG audio */
        assert_int_equal(buf[RTP_HEADER_LEN], 0xFF);
        assert_int_equal(buf[RTP_HEADER_LEN + 1], 0xFB);
        assert_int_equal(buf[RTP_HEADER_LEN + 2], received);
        if (received == 0)
            ssrc = this_ssrc;
        else {
            assert_int_equal(this_ssrc, ssrc);
            assert_int_equal((seq - prev_seq) & 0xFFFF, 1);
        }
        prev_seq = seq;
        received++;
    }
    assert_int_equal(received, FRAME_COUNT);
    rtp_close_socket(r);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_ipv4_literal, teardown_socket),
        cmocka_unit_test_teardown(test_ipv6_literal, teardown_socket),
        cmocka_unit_test_teardown(test_host_name, teardown_socket),
        cmocka_unit_test_teardown(test_unresolvable_is_refused, teardown_socket),
        cmocka_unit_test_teardown(test_invalid_port_is_refused, teardown_socket),
        cmocka_unit_test_teardown(test_sends_rtp_packets, teardown_socket),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
