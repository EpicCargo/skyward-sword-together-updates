// Socket portability for the game client and embedded host. Wire format is unchanged.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <pthread.h>
#include <stdlib.h>
typedef SOCKET SstSocket;
typedef WSAPOLLFD SstPoll;
typedef int SstSocklen;
#define SST_INVALID_SOCKET INVALID_SOCKET
static inline int sst_socket_code(int code) {
    switch (code) {
    case 0: return 0;
    case WSAEWOULDBLOCK: return EAGAIN;
    case WSAEINPROGRESS: case WSAEALREADY: return EINPROGRESS;
    case WSAEINTR: return EINTR;
    case WSAECONNREFUSED: return ECONNREFUSED;
    case WSAECONNRESET: return ECONNRESET;
    case WSAECONNABORTED: return ECONNABORTED;
    case WSAETIMEDOUT: return ETIMEDOUT;
    case WSAEADDRINUSE: return EADDRINUSE;
    case WSAEADDRNOTAVAIL: return EADDRNOTAVAIL;
    case WSAEACCES: return EACCES;
    case WSAENETUNREACH: return ENETUNREACH;
    case WSAEHOSTUNREACH: return EHOSTUNREACH;
    case WSAENOTSOCK: return EBADF;
    case WSAENOBUFS: return ENOMEM;
    default: return EIO;
    }
}
static inline void sst_socket_failed(void) { errno = sst_socket_code(WSAGetLastError()); }
static int sst_winsock_status;
static pthread_once_t sst_winsock_once = PTHREAD_ONCE_INIT;
static void sst_winsock_cleanup(void) { WSACleanup(); }
static void sst_winsock_start(void) {
    WSADATA data;
    sst_winsock_status = WSAStartup(MAKEWORD(2,2), &data);
    if (!sst_winsock_status) atexit(sst_winsock_cleanup);
}
static inline bool sst_socket_init(void) {
    pthread_once(&sst_winsock_once, sst_winsock_start);
    if (sst_winsock_status) { errno = sst_socket_code(sst_winsock_status); return false; }
    return true;
}
static inline void sst_socket_close(SstSocket s) { closesocket(s); }
static inline bool sst_socket_nonblock(SstSocket s) {
    u_long one = 1;
    if (ioctlsocket(s, FIONBIO, &one)) { sst_socket_failed(); return false; }
    if (!SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0)) { errno = EIO; return false; }
    return true;
}
static inline int sst_socket_poll(SstPoll *p, unsigned count, int ms) {
    int result = WSAPoll(p, count, ms);
    if (result < 0) sst_socket_failed();
    return result;
}
#define SST_SEND_FLAGS 0
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
typedef int SstSocket;
typedef struct pollfd SstPoll;
typedef socklen_t SstSocklen;
#define SST_INVALID_SOCKET (-1)
static inline int sst_socket_code(int code) { return code; }
static inline void sst_socket_failed(void) {}
static inline bool sst_socket_init(void) { return true; }
static inline void sst_socket_close(SstSocket s) { close(s); }
static inline bool sst_socket_nonblock(SstSocket s) {
    int flags = fcntl(s,F_GETFL), fdflags = fcntl(s,F_GETFD);
    return flags >= 0 && fdflags >= 0 && !fcntl(s,F_SETFL,flags|O_NONBLOCK) &&
        !fcntl(s,F_SETFD,fdflags|FD_CLOEXEC);
}
static inline int sst_socket_poll(SstPoll *p, unsigned count, int ms) { return poll(p,count,ms); }
#ifdef MSG_NOSIGNAL
#define SST_SEND_FLAGS MSG_NOSIGNAL
#else
#define SST_SEND_FLAGS 0
#endif
#endif

static inline SstSocket sst_socket_open(int family, int type, int protocol) {
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif
    SstSocket s = socket(family,type,protocol);
    if (s == SST_INVALID_SOCKET) { sst_socket_failed(); return s; }
    if (!sst_socket_nonblock(s)) { sst_socket_close(s); return SST_INVALID_SOCKET; }
#ifdef _WIN32
    if (type == SOCK_DGRAM) {
        // An unreachable optional UDP port must not poison the reliable TCP session.
        BOOL report_reset=FALSE; DWORD returned;
        WSAIoctl(s,_WSAIOW(IOC_VENDOR,12),&report_reset,sizeof report_reset,NULL,0,&returned,NULL,NULL);
    }
#endif
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
    return s;
}
static inline SstSocket sst_socket_accept(SstSocket listener) {
    SstSocket s = accept(listener,NULL,NULL);
    if (s == SST_INVALID_SOCKET) { sst_socket_failed(); return s; }
    if (!sst_socket_nonblock(s)) { sst_socket_close(s); return SST_INVALID_SOCKET; }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
    return s;
}
static inline int sst_socket_option(SstSocket s, int level, int option, const void *data, SstSocklen size) {
    int result = setsockopt(s,level,option,(const char *)data,size);
    if (result < 0) sst_socket_failed();
    return result;
}
static inline int sst_socket_error(SstSocket s, int *error) {
    SstSocklen size = sizeof *error;
    int result = getsockopt(s,SOL_SOCKET,SO_ERROR,(char *)error,&size);
    if (result < 0) sst_socket_failed();
    else *error = sst_socket_code(*error);
    return result;
}
static inline int sst_socket_connect(SstSocket s, const struct sockaddr *address, SstSocklen size) {
    int result = connect(s,address,size);
    if (result < 0) {
        sst_socket_failed();
#ifdef _WIN32
        if (errno == EAGAIN) errno = EINPROGRESS;
#endif
    }
    return result;
}
static inline int sst_socket_bind(SstSocket s, const struct sockaddr *address, SstSocklen size) {
    int result = bind(s,address,size);
    if (result < 0) sst_socket_failed();
    return result;
}
static inline int sst_socket_listen(SstSocket s, int count) {
    int result = listen(s,count);
    if (result < 0) sst_socket_failed();
    return result;
}
static inline int sst_socket_name(SstSocket s, struct sockaddr *address, SstSocklen *size) {
    int result = getsockname(s,address,size);
    if (result < 0) sst_socket_failed();
    return result;
}
static inline ptrdiff_t sst_socket_send(SstSocket s, const void *data, size_t size) {
    if (size > INT_MAX) { errno = EMSGSIZE; return -1; }
    ptrdiff_t result = send(s,(const char *)data,(int)size,SST_SEND_FLAGS);
    if (result < 0) sst_socket_failed();
    return result;
}
static inline ptrdiff_t sst_socket_recv(SstSocket s, void *data, size_t size) {
    if (size > INT_MAX) { errno = EMSGSIZE; return -1; }
    ptrdiff_t result = recv(s,(char *)data,(int)size,0);
    if (result < 0) sst_socket_failed();
    return result;
}

static inline ptrdiff_t sst_socket_sendto(SstSocket s, const void *data, size_t size,
                                         const struct sockaddr *address, SstSocklen len) {
    ptrdiff_t result = sendto(s, (const char *)data, (int)size, SST_SEND_FLAGS, address, len);
    if (result < 0) sst_socket_failed();
    return result;
}
static inline ptrdiff_t sst_socket_recvfrom(SstSocket s, void *data, size_t size,
                                           struct sockaddr *address, SstSocklen *len) {
    ptrdiff_t result = recvfrom(s, (char *)data, (int)size, 0, address, len);
    if (result < 0) sst_socket_failed();
    return result;
}
// Authentication tokens must fail closed if the OS random source is unavailable.
static inline bool sst_socket_random(uint8_t *out, size_t size) {
#ifdef _WIN32
    while (size) {
        unsigned value;
        if (rand_s(&value)) return false;
        size_t count = size < sizeof value ? size : sizeof value;
        memcpy(out, &value, count); out += count; size -= count;
    }
    return true;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    bool ok = fread(out, 1, size, f) == size;
    fclose(f); return ok;
#endif
}
