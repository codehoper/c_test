/*
 * TTCP
 *
 * Test TCP connection.  Makes a connection on port 5001
 * and transfers fabricated buffers or data copied from stdin.
 *
 * Usable on 4.2, 4.3, and 4.1a systems by defining one of
 * BSD42 BSD43 (BSD41a)
 * Machines using System V with BSD sockets should define SYSV.
 *
 * Modified for operation under 4.2BSD, 18 Dec 84
 *      T.C. Slattery, USNA
 * Minor improvements, Mike Muuss and Terry Slattery, 16-Oct-85.
 * Modified in 1989 at Silicon Graphics, Inc.
 *      catch SIGPIPE to be able to print stats when receiver has died
 *      for tcp, don't look for sentinel during reads to allow small transfers
 *      increased default buffer size to 8K, nbuf to 2K to transfer 16MB
 *      moved default port to 5001, beyond IPPORT_USERRESERVED
 *      make sinkmode default because it is more popular,
 *              -s now means don't sink/source
 *      count number of _read/_write system calls to see effects of
 *              blocking from full socket buffers
 *      for tcp, -D option turns off buffered writes (sets SO_NODELAY sockopt)
 *      buffer alignment options, -A and -O
 *      print stats in a format that's a bit easier to use with grep & awk
 *      for SYSV, mimic BSD routines to use most of the existing timing code
 *
 * Distribution Status -
 *      Public Domain.  Distribution Unlimited.
 */

#define BSD43
/* #define BSD42 */
/* #define BSD41a */
#if defined(sgi) || defined(CRAY)
#define SYSV
#endif

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <io.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#include <mswsock.h>

extern void
GetRouteUsage(struct sockaddr *Dest,
              DWORD *pNumChanges,
              DWORD *pNumRecords,
              void **pHistory);
void
PrintRouteUsage(
    FILE *File,
    DWORD NumChanges,
    DWORD NumBeforeRecords,
    void *BeforeHistory,
    DWORD NumAfterRecords,
    void *AfterHistory);

DWORD NumBeforeChanges;
DWORD NumBeforeRecords;
void *BeforeHistory;
DWORD NumAfterChanges;
DWORD NumAfterRecords;
void *AfterHistory;
int ConvertHostName = TRUE;

u_short prot; // 0 (don't care), PF_INET, PF_INET6

struct sockaddr_storage sinsrcStorage;
struct sockaddr *sinsrc = (struct sockaddr *)&sinsrcStorage;
struct sockaddr_storage sinmeStorage;
struct sockaddr *sinme = (struct sockaddr *)&sinmeStorage;
struct sockaddr_storage sinhimStorage;
struct sockaddr *sinhim = (struct sockaddr *)&sinhimStorage;
DWORD tmpbuf;

struct addrinfo *aihim;

SOCKET fd;                      /* fd of network socket */
SOCKET fd2;                     /* fd of accepted connection */

int buflen = 8 * 1024;          /* length of buffer */
char *buf;                      /* ptr to dynamic buffer */
int nbuf = 2 * 1024;            /* number of buffers to send */
int duration = 60 * 1000;       /* test duration in milliseconds */
int bufoffset = 0;              /* align buffer to this */
int bufalign = 16*1024;         /* modulo this */

int udp = 0;                    /* 0 = tcp, !0 = udp */
int udpcoverage = 0;            /* UDP Lite checksum coverage */
int options = 0;                /* socket options */
int one = 1;                    /* for 4.3 BSD style setsockopt() */
short port = 5001;              /* TCP port number */
char *host;                     /* ptr to name of host */
int trans;                      /* 0=receive, !0=transmit mode */
int sinkmode = 1;               /* 0=normal I/O, !0=sink/source mode */
char *identity = NULL;          /* identity string for sinkmode */
int verbose = 0;                /* 0=print basic info, 1=print cpu rate, proc
                                 * resource usage. */
int nodelay = 0;                /* set TCP_NODELAY socket option */
int b_flag = 0;                 /* use mread() */
int write_delay = 0;            /* milliseconds of delay before each write */
int hops = -1;                  /* hop limit */

int udp_connect = 0;            /* connect UDP sockets */

#define SOBUF_DEFAULT -1
int sobuf = SOBUF_DEFAULT;      /* SO_RCVBUF/SO_SNDBUF setting; 0 == default */
int async = 0;                  /* async vs. synchronous io calls. value == */
                                /* number of simultaneous async calls. */
int connecttest = 0;

char *filename = NULL;
HANDLE filehandle;

WSADATA WsaData;

char stats[128];
unsigned long nbytes;           /* bytes on net */
unsigned long numCalls;         /* # of I/O system calls */

int Nread( SOCKET fd, PBYTE buf, INT count );
int mread( SOCKET fd, PBYTE bufp, INT n);
int Nwrite( SOCKET fd, PBYTE buf, INT count );

void err(const char *message);
void pattern(char *cp, int cnt );

void prep_timer();
double read_timer(char *s, int l);
DWORD realt;

typedef struct _TTCP_ASYNC_INFO {
    PVOID Buffer;
    DWORD BytesWritten;
    OVERLAPPED Overlapped;
} TTCP_ASYNC_INFO, *PTTCP_ASYNC_INFO;

int parse_addr(char *s, struct sockaddr *sa);
char *format_addr(struct sockaddr *sa);
void set_port(struct sockaddr *sa, u_short port);
u_short get_port(struct sockaddr *sa);
u_int addr_len(struct sockaddr *sa);

void __cdecl
main(argc,argv)
int argc;
char **argv;
{
        char *Term;
        struct in_addr IPv4Group;
        struct in6_addr IPv6Group;
        int error;
        int i;
        BOOL ret;

        error = WSAStartup(MAKEWORD(2, 0), &WsaData );
        if ( error == SOCKET_ERROR ) {
            printf("ttcp: WSAStartup failed: %d\n", WSAGetLastError());
            exit(1);
        }

        if (argc < 2) goto usage;

        for (i = 1; i < argc; i++) {

                if ((argv[i][0] != '-') &&
                    (argv[i][0] != '/'))
                    break;

                switch (argv[i][1]) {

                case 'B':
                        b_flag = 1;
                        break;
                case 't':
                        trans = 1;
                        break;
                case 'f':
                        trans = 1;
                        filename = &argv[i][2];
                        break;
                case 'r':
                        trans = 0;
                        break;
                case 'd':
                        ConvertHostName = FALSE;
                        break;
                case 'D':
                        nodelay = 1;
                        break;
                case 'n':
                        nbuf = atoi(&argv[i][2]);
                        duration = 0;
                        break;
                case 'L':
                        duration = atoi(&argv[i][2]) * 1000;
                        break;
                case 'l':
                        buflen = atoi(&argv[i][2]);
                        break;
                case 'h':
                        sobuf = atoi(&argv[i][2]);
                        break;
                case 'H':
                        hops = atoi(&argv[i][2]);
                        break;
                case 's':
                        sinkmode = 0;   /* sink/source data */
                        break;
                case 'i':
                        identity = &argv[i][2];
                        break;
                case 'p':
                        port = (short) atoi(&argv[i][2]);
                        break;
                case 'u':
                        udp = 1;
                        connecttest = 0;
                        if (argv[i][2] == '\0')
                            udpcoverage = 0;
                        else
                            udpcoverage = atoi(&argv[i][2]);
                        break;
                case 'v':
                        verbose = 1;
                        break;
                case 'A':
                        bufalign = atoi(&argv[i][2]);
                        break;
                case 'O':
                        bufoffset = atoi(&argv[i][2]);
                        break;
                case 'c':
                        udp_connect = 1;
                        break;
                case 'a':
                        if (argv[i][2] == '\0') {
                            async = 3;
                        } else {
                            async = atoi(&argv[i][2]);
                        }
                        break;
                case 'C':
                        connecttest = 1;
                        udp = 0;
                        break;
                case 'S':
                    if (!parse_addr(&argv[i][2], sinsrc))
                        err("bad source address");
                    break;
                case 'w':
                    if (argv[i][2] == '\0')
                        goto usage;
                    write_delay = atoi(&argv[i][2]);
                    break;
                case 'P':
                    if (argv[i][2] == '4')
                        prot = PF_INET;
                    else if (argv[i][2] == '6')
                        prot = PF_INET6;
                    else
                        goto usage;
                    break;
                case 'j':
                    trans = 0;
                    udp = 1;

                    // Figure out if this is an IPv4 or IPv6 group.
                    if (NT_SUCCESS(RtlIpv6StringToAddressA(&argv[i][2],
                                                           &Term,
                                                           &IPv6Group))) {
                        // We should use IPv6.
                        if (prot == 0)
                            prot = PF_INET6;
                        else if (prot != PF_INET6)
                            goto usage;
                    }
                    else if (NT_SUCCESS(RtlIpv4StringToAddressA(&argv[i][2],
                                                                TRUE,
                                                                &Term,
                                                                &IPv4Group))) {
                        // We should use IPv4.
                        if (prot == 0)
                            prot = PF_INET;
                        else if (prot != PF_INET)
                            goto usage;
                    }
                    else
                        goto usage;

                    // Sanity-check the interface index, if present.
                    if (*Term == '\0')
                        ; // No interface index.
                    else if (*Term == '/') {
                        if (atoi(Term+1) == 0)
                            goto usage;
                    } else
                        goto usage;
                    break;

                default:
                    goto usage;
                }
        }

        if (filename != NULL) {
            filehandle = CreateFile(
                             filename,
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL
                             );
            if ( filehandle == INVALID_HANDLE_VALUE ) {
                printf("failed to open file %s: %d\n",
                       filename, GetLastError( ) );
                exit(1);
            }
            printf("ttcp-t: opened file %s\n", filename );
        }

        if ((async != 0) && trans && (sobuf == SOBUF_DEFAULT)) {
            sobuf = 0;
            printf("ttcp-t: for async write, setting SO_SNDBUF to 0.\n");
        }

        if (udp && !trans && (sobuf == SOBUF_DEFAULT))
            sobuf = 65536;

        if (connecttest) {
            INT zero = 0;

            // ??? What is this?
            // disable socket sharing in the process
            setsockopt((SOCKET)NULL, SOL_SOCKET, 0x8002, (char *)&zero, 4);
        }

        if (trans) {
            /* xmitr */
            struct addrinfo hints;

            if (i + 1 != argc) goto usage;
            host = argv[i];

            memset(&hints, 0, sizeof hints);
            hints.ai_flags = AI_NUMERICHOST;
            hints.ai_family = prot;

            if (getaddrinfo(host, NULL, &hints, &aihim) != 0) {
                struct addrinfo *aitmp;

                hints.ai_flags = AI_CANONNAME;

                if (getaddrinfo(host, NULL, &hints, &aihim) != 0)
                    err("getaddrinfo");

                for (aitmp = aihim; aitmp != NULL; aitmp = aitmp->ai_next)
                    printf("ttcp-t: %s -> %s\n",
                           aihim->ai_canonname,
                           format_addr(aitmp->ai_addr));
            }

          retry:
            if (aihim == NULL)
                err("connect");
            memcpy(sinhim, aihim->ai_addr, aihim->ai_addrlen);
            aihim = aihim->ai_next;

            memcpy(sinme, sinsrc, sizeof(struct sockaddr_storage));
            if (sinme->sa_family == 0) {
                // Use same family as destination.
                sinme->sa_family = sinhim->sa_family;
            } else {
                // Source and destination family should be the same.
                // Let connect() check for this.
            }
            set_port(sinhim, htons(port));
            set_port(sinme, 0); // free choice
        } else {
            /* rcvr */
            if (i != argc) goto usage;

            memcpy(sinme, sinsrc, sizeof(struct sockaddr_storage));
            if (sinme->sa_family == 0)
                sinme->sa_family = prot;
            set_port(sinme, htons(port));
        }

        //
        // Create the socket and prepare it for the test.
        //

        if (trans) {
            fd = socket(sinme->sa_family, udp?SOCK_DGRAM:SOCK_STREAM, 0);
            if (fd == SOCKET_ERROR)
                err("socket");

            if (bind(fd, sinme, addr_len(sinme)) < 0)
                err("bind");

            if (options) {
#if defined(BSD42)
                if (setsockopt(fd, SOL_SOCKET, options, 0, 0) < 0)
#else // BSD43
                if (setsockopt(fd, SOL_SOCKET, options,
                               (char *)&one, sizeof(one)) < 0)
#endif
                    err("setsockopt");
            }

            if (!udp && nodelay) {
                if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                               (char *)&one, sizeof(one)) < 0)
                    err("setsockopt: nodelay");
            }

            if (udp && udpcoverage) {
                if (setsockopt(fd, IPPROTO_UDP, UDP_CHECKSUM_COVERAGE,
                               (char *)&udpcoverage, sizeof(udpcoverage)) < 0)
                    err("setsockopt: udp checksum coverage");
            }

            if (sobuf != SOBUF_DEFAULT) {
                if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                               (char *)&sobuf, sizeof(sobuf)) < 0)
                    err("setsockopt: SO_SNDBUF");
            }

            if (hops != -1) {
                switch (sinme->sa_family) {
                case AF_INET:
                    if (setsockopt(fd, IPPROTO_IP, IP_TTL,
                                   (char *)&hops, sizeof(hops)) < 0)
                        err("setsockopt: IP_TTL");
                    if (udp) {
                        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
                                       (char *)&hops, sizeof(hops)) < 0)
                            err("setsockopt: IP_MULTICAST_TTL");
                    }
                    break;

                case AF_INET6:
                    if (setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                                   (char *)&hops, sizeof(hops)) < 0)
                        err("setsockopt: IPV6_UNICAST_HOPS");
                    if (udp) {
                        if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                                       (char *)&hops, sizeof(hops)) < 0)
                            err("setsockopt: IPV6_MULTICAST_HOPS");
                    }
                    break;
                }
            }

            if (!udp || udp_connect) {
                if (connect(fd, sinhim, addr_len(sinhim)) < 0)
                    goto retry;

                tmpbuf = sizeof(struct sockaddr_storage);
                if (getpeername(fd, (struct sockaddr *)sinhim, &tmpbuf) < 0)
                    err("getpeername");
            }

            tmpbuf = sizeof(struct sockaddr_storage);
            if (getsockname(fd, (struct sockaddr *)sinme, &tmpbuf) < 0)
                err("getsockname");

        } else { // if not (trans)
            if (sinme->sa_family == 0) {
                SOCKET fd4, fd6;
                fd_set fdset;
                int numsockets;
                struct sockaddr_in sin;
                struct sockaddr_in6 sin6;

                //
                // We do not know apriori whether to use IPv4 or IPv6.
                // So we create two sockets and listen on both.
                // socket() will fail if the protocol is not installed,
                // and bind() will fail if the stack is stopped,
                // so we allow for those errors.
                //

                FD_ZERO(&fdset);
                numsockets = 0;

                fd4 = socket(AF_INET, udp?SOCK_DGRAM:SOCK_STREAM, 0);
                if (fd4 != INVALID_SOCKET) {

                    memset(&sin, 0, sizeof sin);
                    sin.sin_family = AF_INET;
                    sin.sin_port = get_port(sinme);
                    if (bind(fd4, (struct sockaddr *)&sin, sizeof sin) == 0) {

                        if (!udp) {
                            if (hops != -1) {
                                if (setsockopt(fd4, IPPROTO_IP, IP_TTL,
                                        (char *)&hops, sizeof(hops)) < 0)
                                    err("setsockopt: IP_TTL");
                            }

                            if (listen(fd4, 0) < 0)
                                err("listen");
                        }

                        numsockets++;
                        FD_SET(fd4, &fdset);
                    }
                }

                fd6 = socket(AF_INET6, udp?SOCK_DGRAM:SOCK_STREAM, 0);
                if (fd6 != INVALID_SOCKET) {

                    memset(&sin6, 0, sizeof sin6);
                    sin6.sin6_family = AF_INET6;
                    sin6.sin6_port = get_port(sinme);
                    if (bind(fd6, (struct sockaddr *)&sin6, sizeof sin6) == 0) {

                        if (!udp) {
                            if (hops != -1) {
                                if (setsockopt(fd6, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                                        (char *)&hops, sizeof(hops)) < 0)
                                    err("setsockopt: IPV6_UNICAST_HOPS");
                            }

                            if (listen(fd6, 0) < 0)
                                err("listen");
                        }

                        numsockets++;
                        FD_SET(fd6, &fdset);
                    }
                }

                if (numsockets == 0)
                    err("socket");

                if (select(numsockets, &fdset, NULL, NULL, NULL) != 1)
                    err("select");

                if ((fd4 != INVALID_SOCKET) && FD_ISSET(fd4, &fdset)) {
                    fd = fd4;
                    if (fd6 != INVALID_SOCKET)
                        closesocket(fd6);
                    memcpy(sinme, &sin, sizeof sin);
                }
                else if ((fd6 != INVALID_SOCKET) && FD_ISSET(fd6, &fdset)) {
                    fd = fd6;
                    if (fd4 != INVALID_SOCKET)
                        closesocket(fd4);
                    memcpy(sinme, &sin6, sizeof sin6);
                }
                else {
                    printf("select() bug\n");
                    exit(1);
                }
            } else { // if not (sinme->sa_family == 0)
                fd = socket(sinme->sa_family, udp?SOCK_DGRAM:SOCK_STREAM, 0);
                if (fd == SOCKET_ERROR)
                    err("socket");

                if (bind(fd, sinme, addr_len(sinme)) < 0)
                    err("bind");

                if (!udp) {
                    if (hops != -1) {
                        switch (sinme->sa_family) {
                        case AF_INET:
                            if (setsockopt(fd, IPPROTO_IP, IP_TTL,
                                    (char *)&hops, sizeof(hops)) < 0)
                                err("setsockopt: IP_TTL");
                            break;
                        case AF_INET6:
                            if (setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                                    (char *)&hops, sizeof(hops)) < 0)
                                err("setsockopt: IPV6_UNICAST_HOPS");
                            break;
                        }
                    }

                    if (listen(fd, 0) < 0)   /* allow a queue of 0 */
                        err("listen");
                }
            } // end if (sinme->sa_family == 0)

            if (options) {
#if defined(BSD42)
                if (setsockopt(fd, SOL_SOCKET, options, 0, 0) < 0)
#else // BSD43
                if (setsockopt(fd, SOL_SOCKET, options,
                               (char *)&one, sizeof(one)) < 0)
#endif
                    err("setsockopt");
            }

            if (sobuf != SOBUF_DEFAULT) {
                if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                               (char *)&sobuf, sizeof(sobuf)) < 0)
                    err("setsockopt: SO_RCVBUF");
            }

            if (!udp) {
                tmpbuf = sizeof(struct sockaddr_storage);
                fd2 = accept(fd, (struct sockaddr *)sinhim, &tmpbuf);
                if (fd2 == SOCKET_ERROR)
                    err("accept");

                tmpbuf = sizeof(struct sockaddr_storage);
                if (getsockname(fd2, (struct sockaddr *)sinme, &tmpbuf) < 0)
                    err("getsockname");

            } else {
                tmpbuf = sizeof(struct sockaddr_storage);
                if (getsockname(fd, (struct sockaddr *)sinme, &tmpbuf) < 0)
                    err("getsockname");

                // Join multicast groups.
                for (i = 1; i < argc; i++) {
                    if ((argv[i][0] != '-') &&
                        (argv[i][0] != '/'))
                        break;
                    if (argv[i][1] == 'j') {
                        if (sinme->sa_family == AF_INET) {
                            struct ip_mreq mreq;

                            (void) RtlIpv4StringToAddressA(&argv[i][2],
                                                           TRUE,
                                                           &Term,
                                                           &mreq.imr_multiaddr);
                            if ((*Term == ':') || (*Term == '/')) {
                                // In Whistler, this ioctl allows an
                                // interface index in addition to an address.
                                mreq.imr_interface.s_addr = htonl(atoi(Term+1));
                            } else {
                                mreq.imr_interface.s_addr = 0;
                            }

                            if (setsockopt(fd, IPPROTO_IP,
                                           IP_ADD_MEMBERSHIP,
                                           (char *)&mreq, sizeof mreq) < 0)
                                err("setsockopt: IP_ADD_MEMBERSHIP");

                        } else { // sinme->sa_family == AF_INET6
                            struct ipv6_mreq mreq;

                            (void) RtlIpv6StringToAddressA(&argv[i][2],
                                                           &Term,
                                                           &mreq.ipv6mr_multiaddr);
                            if ((*Term == ':') || (*Term == '/')) {
                                mreq.ipv6mr_interface = atoi(Term+1);
                            } else {
                                mreq.ipv6mr_interface = 0;
                            }

                            if (setsockopt(fd, IPPROTO_IPV6,
                                           IPV6_ADD_MEMBERSHIP,
                                           (char *)&mreq, sizeof mreq) < 0)
                                err("setsockopt: IPV6_ADD_MEMBERSHIP");
                        }
                    }
                }
            }
        } // end if (trans)

        if (trans) {
            printf("ttcp-t: local %s", format_addr(sinme));
            printf(" -> remote %s\n", format_addr(sinhim));
        } else {
            printf("ttcp-r: local %s", format_addr(sinme));
            if (udp)
                printf("\n");
            else
                printf(" <- remote %s\n", format_addr(sinhim));
        }

        if (trans || !udp)
            GetRouteUsage(sinhim, &NumBeforeChanges,
                          &NumBeforeRecords, &BeforeHistory);

        if (connecttest) {

            //
            // Instead of testing data transfer,
            // test connection setup/teardown.
            //

            if (trans) {
                //
                // Close the socket that we have from above.
                //
                closesocket(fd);

                prep_timer();

                for (i = 1; i < nbuf; i++) {

                    fd = socket(sinme->sa_family, SOCK_STREAM, 0);
                    if (fd == INVALID_SOCKET)
                        err("socket");

                    if (bind(fd, sinme, addr_len(sinme)) < 0)
                        err("bind");

                    if (connect(fd, sinhim, addr_len(sinhim)) < 0)
                        err("connect");

                    if (recv(fd, (char *)&tmpbuf, sizeof(tmpbuf), 0) < 0)
                        err("recv");

                    closesocket(fd);
                }

            } else { // if not (trans)
                //
                // Close the socket that we have from above.
                //
                closesocket(fd2);

                prep_timer();

                for (i = 1; i < nbuf; i++) {

                    fd2 = accept(fd, NULL, NULL);
                    if (fd2 == INVALID_SOCKET)
                        err("accept");

                    closesocket(fd2);
                }

            } // end if (trans)

            numCalls = i;
            (void)read_timer(stats,sizeof(stats));
            goto display;

        } // end if (connecttest)

        //
        // Send/receive data using the socket.
        //

        if (!udp && !trans) {
            closesocket(fd);
            fd = fd2;
        }

        if (udp && buflen < 5) {
            buflen = 5;         /* send more than the sentinel size */
        }

        if ( (buf = (char *)malloc(buflen+bufalign)) == (char *)NULL)
            err("malloc");
        if (bufalign != 0)
            buf +=(bufalign - (PtrToUlong(buf) % bufalign) + bufoffset) % bufalign;

        if (trans) {
            if (sinkmode && duration)
                printf("ttcp-t: buflen=%d, duration=%ds, align=%d/+%d, port=%d %s\n",
                       buflen, duration/1000, bufalign, bufoffset, port,
                       udp ? "udp" : "tcp");
            else
                printf("ttcp-t: buflen=%d, nbuf=%d, align=%d/+%d, port=%d %s\n",
                       buflen, nbuf, bufalign, bufoffset, port);
        } else {
            printf("ttcp-r: buflen=%d, align=%d/+%d, port=%d %s\n",
                   buflen, bufalign, bufoffset, port,
                   udp ? "udp" : "tcp");
        }

        prep_timer();

        if (async != 0) {
            TTCP_ASYNC_INFO *info;
            HANDLE *events;

            info = malloc( sizeof(*info) * async );
            if ( info == NULL ) {
                printf("malloc failed.\n" );
                exit(1);
            }

            events = malloc( sizeof(HANDLE) * async );
            if ( events == NULL ) {
                printf("malloc failed.\n" );
                exit(1);
            }

            for ( i = 0; i < async; i++ ) {

                info[i].Buffer = malloc(buflen);
                if ( info[i].Buffer == NULL ) {
                    printf("malloc failed.\n" );
                    exit(1);
                }

                events[i] = CreateEvent( NULL, FALSE, FALSE, NULL );
                if ( events[i] == NULL ) {
                    printf("CreateEvent failed: %d\n", GetLastError( ) );
                    exit(1);
                }

                info[i].Overlapped.Internal = 0;
                info[i].Overlapped.InternalHigh = 0;
                info[i].Overlapped.Offset = 0;
                info[i].Overlapped.OffsetHigh = 0;
                info[i].Overlapped.hEvent = events[i];
            }

            if (trans) {

                for ( i = 0; i < async; i++ ) {

                    ret = WriteFile(
                              (HANDLE)fd,
                              info[i].Buffer,
                              buflen,
                              &info[i].BytesWritten,
                              &info[i].Overlapped
                              );
                    if ( !ret && GetLastError( ) != ERROR_IO_PENDING ) {
                        printf("WriteFile failed: %d\n", GetLastError( ) );
                        break;
                    }
                    nbuf--;
                    numCalls++;
                }

                while (nbuf > 0) {
                    ret = WaitForMultipleObjects( async, events, FALSE, INFINITE );
                    i = ret - WAIT_OBJECT_0;

                    ret = GetOverlappedResult(
                              (HANDLE)fd,
                              &info[i].Overlapped,
                              &info[i].BytesWritten,
                              FALSE
                              );
                    if ( !ret ) {
                        printf("pended WriteFile failed: %d\n",
                               GetLastError( ) );
                        break;
                    }

                    nbytes += info[i].BytesWritten;

                    ret = WriteFile(
                              (HANDLE)fd,
                              info[i].Buffer,
                              buflen,
                              &info[i].BytesWritten,
                              &info[i].Overlapped
                              );
                    if ( !ret && GetLastError( ) != ERROR_IO_PENDING ) {
                        printf("WriteFile failed: %d\n", GetLastError( ) );
                        break;
                    }
                    nbuf--;
                    numCalls++;
                }

                for ( i = 0; i < async; i++ ) {
                    ret = GetOverlappedResult(
                              (HANDLE)fd,
                              &info[i].Overlapped,
                              &info[i].BytesWritten,
                              TRUE
                              );
                    if ( !ret ) {
                        printf("pended WriteFile failed: %d\n",
                               GetLastError());
                        break;
                    }

                    nbytes += info[i].BytesWritten;
                }

            } else { // if not (trans)

                for ( i = 0; i < async; i++ ) {

                    ret = ReadFile(
                              (HANDLE)fd,
                              info[i].Buffer,
                              buflen,
                              &info[i].BytesWritten,
                              &info[i].Overlapped
                              );
                    if ( !ret && GetLastError( ) != ERROR_IO_PENDING ) {
                        printf("ReadFile failed: %d\n", GetLastError( ) );
                        break;
                    }
                    nbuf--;
                    numCalls++;
                }

                while (TRUE) {
                    ret = WaitForMultipleObjects( async, events, FALSE, INFINITE );
                    i = ret - WAIT_OBJECT_0;

                    ret = GetOverlappedResult(
                              (HANDLE)fd,
                              &info[i].Overlapped,
                              &info[i].BytesWritten,
                              FALSE
                              );
                    if ( !ret ) {
                        printf("pended ReadFile failed: %d\n",
                               GetLastError( ) );
                        break;
                    }

                    nbytes += info[i].BytesWritten;
                    if (info[i].BytesWritten == 0) {
                        break;
                    }

                    ret = ReadFile(
                              (HANDLE)fd,
                              info[i].Buffer,
                              buflen,
                              &info[i].BytesWritten,
                              &info[i].Overlapped
                              );
                    if ( !ret && GetLastError( ) != ERROR_IO_PENDING ) {
                        printf("ReadFile failed: %d\n", GetLastError( ) );
                        break;
                    }
                    nbuf--;
                    numCalls++;
                }

                for ( i = 0; i < async; i++ ) {
                    ret = GetOverlappedResult(
                              (HANDLE)fd,
                              &info[i].Overlapped,
                              &info[i].BytesWritten,
                              TRUE
                              );
                    if ( !ret ) {
                        printf("pended ReadFile failed: %d\n",
                               GetLastError( ) );
                        break;
                    }

                    nbytes += info[i].BytesWritten;
                }
            } // end if (trans)

        } // end if (async != 0)

        else if (filename != NULL ) {

            ret = TransmitFile( fd, filehandle,
                                0,      // nNumberOfBytesToWrite
                                0,      // nNumberOfBytesPerSend
                                NULL,   // lpOverlapped
                                NULL,   // lpTransmitBuffers
                                0 );    // dwFlags

            if ( !ret ) {
                printf("TransmitFile failed: %d\n", GetLastError( ) );
                exit(1);
            }

        } else if (sinkmode) {
                register int cnt;

                if (trans)  {
                        pattern( buf, buflen );
                        if (!udp && identity)
                            (void)Nwrite(fd, identity, strlen(identity) + 1);
                        if (duration != 0) {
                            // time-limited
                            int start = GetTickCount();
                            nbuf = 0;
                            do {
                                if (Nwrite(fd,buf,buflen) != buflen)
                                    break;
                                nbuf++;
                                nbytes += buflen;
                            } while (((int)GetTickCount() - start) < duration);
                        } else {
                            // data-limited
                            while (nbuf-- && Nwrite(fd,buf,buflen) == buflen)
                                nbytes += buflen;
                        }
                        printf("ttcp-t: done sending, nbuf = %d\n", nbuf );
                } else {
                        if (udp) {
                            int going = 0;
                            while ((cnt=Nread(fd,buf,buflen)) > 0) {
                                if (! going) {
                                    going = 1;
                                    prep_timer();
                                }
                                if (buf[0] == '\0')
                                    break; // EOF
                                nbytes += cnt;
                            }
                        } else {
                            while ((cnt=Nread(fd,buf,buflen)) > 0)  {
                                    nbytes += cnt;
                            }
                        }
                }

        } else {
                register int cnt;
                if (trans)  {
			_setmode(0, _O_BINARY);
                        while((cnt=_read(0,buf,buflen)) > 0 &&
                            Nwrite(fd,buf,cnt) == cnt)
                                nbytes += cnt;
                }  else  {
			_setmode(1, _O_BINARY);
                        while((cnt=Nread(fd,buf,buflen)) > 0 &&
                            _write(1,buf,cnt) == cnt)
                                nbytes += cnt;
                }
        }

        (void)read_timer(stats,sizeof(stats));
        if (udp && trans) {
            int cnt;
            buf[0] = '\0';
            for (cnt = 0; cnt < 20; cnt++) {
                (void)Nwrite(fd, buf, 1); // EOF
                Sleep(100);
            }
        }
display:
        closesocket(fd);
        //if( cput <= 0.0 )  cput = 0.001;
        if ( numCalls == 0 ) {
            numCalls = 1;
        }
        if ( realt == 0 ) {
            realt = 1;
        }
        printf("ttcp%s: %d bytes in %d real milliseconds = %d Kbps\n",
               trans ? "-t" : "-r",
               nbytes, realt,
               (int)((8000.0*(nbytes/(double)realt))/1024.0));

        printf("ttcp%s: %d I/O calls, msec/call = %d, calls/sec = %d, bytes/call = %d\n",
               trans ? "-t" : "-r",
               numCalls, realt/numCalls,
               (1000*numCalls)/realt, nbytes/numCalls);

        if (trans || !udp) {
            GetRouteUsage(sinhim, &NumAfterChanges,
                          &NumAfterRecords, &AfterHistory);
            if (NumAfterRecords == 0)
                printf("    Route Usage not available\n");
            else
                PrintRouteUsage(stdout,
                                NumAfterChanges - NumBeforeChanges,
                                NumBeforeRecords, BeforeHistory,
                                NumAfterRecords, AfterHistory);
        }

        if (verbose) {
            printf("ttcp%s: buffer address %#p\n",
                   trans?"-t":"-r", buf);
        }

        WSACleanup();
        exit(0);

usage:
        printf(
"Usage: ttcp -t [-options] host [ < in ]\n"
"       ttcp -r [-options > out]\n"
"Common options:\n"
"        -l##    length of bufs read from or written to network (default 8192)\n"
"        -u      use UDP instead of TCP\n"
"        -p##    port number to send to or listen at (default 5001)\n"
"        -P4     use IPv4\n"
"        -P6     use IPv6\n"
"        -s      -t: don't source a pattern to network, get data from stdin\n"
"                -r: don't sink (discard), print data on stdout\n"
"        -A      align the start of buffers to this modulus (default 16384)\n"
"        -O      start buffers at this offset from the modulus (default 0)\n"
"        -v      verbose: print more statistics\n"
"        -d      do not resolve addresses to hostnames\n"
"        -h      set SO_SNDBUF or SO_RCVBUF\n"
"        -a      use asynchronous I/O calls\n"
"        -S##    specify source address\n"
"        -H##    specify TTL or hop limit\n"
"Options specific to -t:\n"
"        -i###   in sinkmode, sends the null-terminated string first\n"
"        -L##    duration of test in seconds (default 60)\n"
"        -n##    number of source bufs written to network (default 2048)\n"
"        -D      don't buffer TCP writes (sets TCP_NODELAY socket option)\n"
"        -w##    milliseconds of delay before each write\n"
"        -f##    specify a file name for TransmitFile\n"
"Options specific to -r:\n"
"        -B      for -s, only output full blocks as specified by -l (for TAR)\n"
"        -j##[/##] specify multicast group and optional ifindex (UDP-only)\n");
        WSACleanup();
        exit(1);
}

void err(message)
    const char *message;
{
    printf("ttcp%s: %s: error=%d\n",
           trans ? "-t" : "-r",
           message,
           WSAGetLastError());

    WSACleanup();
    exit(1);
}

void pattern( cp, cnt )
register char *cp;
register int cnt;
{
    int i;

    for (i = 0; i < cnt; i++)
        cp[i] = 0xCC;
}

__int64 time0;
__int64 time1;
__int64 freq;

/*
 *                      P R E P _ T I M E R
 */
void
prep_timer()
{
    (void) QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    (void) QueryPerformanceCounter((LARGE_INTEGER *)&time0);
}

/*
 *                      R E A D _ T I M E R
 *
 */
double
read_timer(str,len)
char *str;
int len;
{
    (void) QueryPerformanceCounter((LARGE_INTEGER *)&time1);

    // realt is real elapsed time in milliseconds
    realt = (DWORD) ((1000 * (time1 - time0)) / freq);

    return 0;
}

/*
 *                      N R E A D
 */
int
Nread( SOCKET fd, PBYTE buf, INT count )
{
        static int didit = 0;
        int len = sizeof(sinhimStorage);
        register int cnt;
        if( udp )  {
            if (udp_connect) {
                cnt = recv( fd, buf, count, 0 );
                numCalls++;
            } else {
                cnt = recvfrom( fd, buf, count, 0, sinhim, &len );
                if ((recvfrom > 0) && !didit)
                    didit = 1;
                numCalls++;
            }
        } else {
                if( b_flag )
                        cnt = mread( fd, buf, count );  /* fill buf */
                else {
                        cnt = recv( fd, buf, count, 0 );
                        numCalls++;
                }
        }
        if (cnt<0) {
            printf("recv(from) failed: %d\n", WSAGetLastError( ) );
        }
        return(cnt);
}

/*
 *                      N W R I T E
 */
int
Nwrite( SOCKET fd, PBYTE buf, INT count )
{
        register int cnt = 0;
        int bytesToSend = count;

        if (write_delay)
                Sleep(write_delay);
        if( udp && !udp_connect)  {
again:
                cnt = sendto( fd, buf, count, 0, sinhim, addr_len(sinhim) );
                numCalls++;
                if( cnt<0 && WSAGetLastError( ) == WSAENOBUFS )  {
                        Sleep(18000);
                        goto again;
                }
        } else {
                while( count > 0 )
                {
                    cnt = send( fd, buf, count, 0 );
                    numCalls++;

                    //if (count != cnt) {
                    //    printf("Tried %d, sent %d\n", count, cnt );
                    //} else {
                    //    printf("send %d bytes as requested.\n", cnt );
                    //}

                    if( cnt == SOCKET_ERROR )
                    {
                        break;
                    }

                    count -= cnt;
                    buf += cnt;
                }
        }
        if (cnt<0) {
            printf("send(to) failed: %d\n", WSAGetLastError( ) );
            return -1;
        }
        return(bytesToSend);
}

/*
 *                      M R E A D
 *
 * This function performs the function of a read(II) but will
 * call read(II) multiple times in order to get the requested
 * number of characters.  This can be necessary because
 * network connections don't deliver data with the same
 * grouping as it is written with.  Written by Robert S. Miles, BRL.
 */
int
mread( SOCKET fd, PBYTE bufp, INT n)
{
        register unsigned       count = 0;
        register int            nread;

        do {
                nread = recv(fd, bufp, n-count, 0);
                numCalls++;
                if(nread < 0)  {
                        return(-1);
                }
                if(nread == 0)
                        return((int)count);
                count += (unsigned)nread;
                bufp += nread;
         } while(count < (UINT)n);

        return((int)count);
}


int
parse_addr(char *s, struct sockaddr *sa)
{
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = prot;

    if (getaddrinfo(s, NULL, &hints, &result) != 0)
        return FALSE; // Failed to parse/resolve the address.

    memcpy(sa, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    return TRUE;
}

u_int
addr_len(struct sockaddr *sa)
{
    u_int salen;

    switch (sa->sa_family) {
    case AF_INET:
        salen = sizeof(struct sockaddr_in);
        break;
    case AF_INET6:
        salen = sizeof(struct sockaddr_in6);
        break;
    default:
        salen = 0;
        break;
    }

    return salen;
}

char *
format_addr(struct sockaddr *sa)
{
    static char buffer[NI_MAXHOST];

    if (getnameinfo(sa, addr_len(sa),
                    buffer, sizeof buffer,
                    NULL, 0, NI_NUMERICHOST) != 0)
        strcpy(buffer, "<invalid>");

    return buffer;
}

void
set_port(struct sockaddr *sa, u_short port)
{
    //
    // The port field is in the same location
    // for both sockaddr_in and sockaddr_in6.
    //
    ((struct sockaddr_in *)sa)->sin_port = port;
}

u_short
get_port(struct sockaddr *sa)
{
    //
    // The port field is in the same location
    // for both sockaddr_in and sockaddr_in6.
    //
    return ((struct sockaddr_in *)sa)->sin_port;
}
