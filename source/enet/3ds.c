/**
 @file  3ds.c
 @brief ENet platform layer for the Nintendo 3DS (libctru soc:U sockets).

 Replaces unix.c: libctru has no sendmsg/recvmsg, so scattered buffers are gathered into one
 datagram before sendto, and received with a single recvfrom. The soc service must be
 initialised (socInit) before enet_host_create.
*/
#include <3ds.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

static enet_uint32 timeBase = 0;

int
enet_initialize (void)
{
    return 0;
}

void
enet_deinitialize (void)
{
}

enet_uint32
enet_host_random_seed (void)
{
    return (enet_uint32) svcGetSystemTick ();
}

enet_uint32
enet_time_get (void)
{
    return (enet_uint32) osGetTime () - timeBase;
}

void
enet_time_set (enet_uint32 newTimeBase)
{
    timeBase = (enet_uint32) osGetTime () - newTimeBase;
}

int
enet_address_set_host_ip (ENetAddress * address, const char * name)
{
    struct in_addr in;
    if (! inet_aton (name, & in))
      return -1;
    address -> host = in.s_addr;
    return 0;
}

int
enet_address_set_host (ENetAddress * address, const char * name)
{
    if (enet_address_set_host_ip (address, name) == 0)
      return 0;

    struct hostent * hostEntry = gethostbyname (name);
    if (hostEntry == NULL || hostEntry -> h_addrtype != AF_INET)
      return -1;

    address -> host = * (enet_uint32 *) hostEntry -> h_addr_list [0];
    return 0;
}

int
enet_address_get_host_ip (const ENetAddress * address, char * name, size_t nameLength)
{
    struct in_addr in;
    in.s_addr = address -> host;
    const char * addr = inet_ntoa (in);
    size_t addrLen = strlen (addr);
    if (addrLen >= nameLength)
      return -1;
    memcpy (name, addr, addrLen + 1);
    return 0;
}

int
enet_address_get_host (const ENetAddress * address, char * name, size_t nameLength)
{
    return enet_address_get_host_ip (address, name, nameLength);
}

int
enet_socket_bind (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;

    memset (& sin, 0, sizeof (struct sockaddr_in));
    sin.sin_family = AF_INET;

    if (address != NULL)
    {
       sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
       sin.sin_addr.s_addr = address -> host;
    }
    else
    {
       sin.sin_port = 0;
       sin.sin_addr.s_addr = INADDR_ANY;
    }

    return bind (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
}

int
enet_socket_get_address (ENetSocket socket, ENetAddress * address)
{
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);

    if (getsockname (socket, (struct sockaddr *) & sin, & sinLength) == -1)
      return -1;

    address -> host = (enet_uint32) sin.sin_addr.s_addr;
    address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    return 0;
}

int
enet_socket_listen (ENetSocket socket, int backlog)
{
    return listen (socket, backlog < 0 ? 5 : backlog);
}

ENetSocket
enet_socket_create (ENetSocketType type)
{
    return socket (AF_INET, type == ENET_SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
}

int
enet_socket_set_option (ENetSocket socket, ENetSocketOption option, int value)
{
    int result = -1;
    switch (option)
    {
        case ENET_SOCKOPT_NONBLOCK:
        {
            int flags = fcntl (socket, F_GETFL, 0);
            result = fcntl (socket, F_SETFL, value ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
            break;
        }

        case ENET_SOCKOPT_BROADCAST:
            result = setsockopt (socket, SOL_SOCKET, SO_BROADCAST, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_REUSEADDR:
            result = setsockopt (socket, SOL_SOCKET, SO_REUSEADDR, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_RCVBUF:
            result = setsockopt (socket, SOL_SOCKET, SO_RCVBUF, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_SNDBUF:
            result = setsockopt (socket, SOL_SOCKET, SO_SNDBUF, (char *) & value, sizeof (int));
            break;

        default:
            // Timeouts, TCP_NODELAY, TTL and IPv6 options are unused by a UDP client.
            break;
    }
    return result == -1 ? -1 : 0;
}

int
enet_socket_get_option (ENetSocket socket, ENetSocketOption option, int * value)
{
    int result = -1;
    socklen_t len;
    switch (option)
    {
        case ENET_SOCKOPT_ERROR:
            len = sizeof (int);
            result = getsockopt (socket, SOL_SOCKET, SO_ERROR, value, & len);
            break;

        default:
            break;
    }
    return result == -1 ? -1 : 0;
}

int
enet_socket_connect (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;
    int result;

    memset (& sin, 0, sizeof (struct sockaddr_in));
    sin.sin_family = AF_INET;
    sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
    sin.sin_addr.s_addr = address -> host;

    result = connect (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    if (result == -1 && errno == EINPROGRESS)
      return 0;

    return result;
}

ENetSocket
enet_socket_accept (ENetSocket socket, ENetAddress * address)
{
    int result;
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);

    result = accept (socket, address != NULL ? (struct sockaddr *) & sin : NULL, address != NULL ? & sinLength : NULL);
    if (result == -1)
      return ENET_SOCKET_NULL;

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return result;
}

int
enet_socket_shutdown (ENetSocket socket, ENetSocketShutdown how)
{
    return shutdown (socket, (int) how);
}

void
enet_socket_destroy (ENetSocket socket)
{
    if (socket != -1)
      close (socket);
}

int
enet_socket_send (ENetSocket socket,
                  const ENetAddress * address,
                  const ENetBuffer * buffers,
                  size_t bufferCount)
{
    static enet_uint8 datagram [ENET_PROTOCOL_MAXIMUM_MTU];
    struct sockaddr_in sin;
    size_t length = 0;
    int sentLength;

    for (size_t i = 0; i < bufferCount; ++ i)
    {
        if (length + buffers [i].dataLength > sizeof (datagram))
          return -1;
        memcpy (datagram + length, buffers [i].data, buffers [i].dataLength);
        length += buffers [i].dataLength;
    }

    if (address != NULL)
    {
        memset (& sin, 0, sizeof (struct sockaddr_in));
        sin.sin_family = AF_INET;
        sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
        sin.sin_addr.s_addr = address -> host;
        sentLength = sendto (socket, datagram, length, 0, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    }
    else
        sentLength = send (socket, datagram, length, 0);

    if (sentLength == -1)
    {
       if (errno == EWOULDBLOCK || errno == EAGAIN)
         return 0;
       return -1;
    }

    return sentLength;
}

int
enet_socket_receive (ENetSocket socket,
                     ENetAddress * address,
                     ENetBuffer * buffers,
                     size_t bufferCount)
{
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);
    int recvLength;

    // ENet always receives into a single buffer (host -> packetData [0]).
    if (bufferCount != 1)
      return -1;

    recvLength = recvfrom (socket, buffers [0].data, buffers [0].dataLength, 0,
                           address != NULL ? (struct sockaddr *) & sin : NULL,
                           address != NULL ? & sinLength : NULL);

    if (recvLength == -1)
    {
        switch (errno)
        {
            case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
            case EAGAIN:
#endif
                return 0;
            case EINTR:
            case EMSGSIZE:
                return -2;
            default:
                return -1;
        }
    }

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return recvLength;
}

int
enet_socketset_select (ENetSocket maxSocket, ENetSocketSet * readSet, ENetSocketSet * writeSet, enet_uint32 timeout)
{
    struct timeval timeVal;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    return select (maxSocket + 1, readSet, writeSet, NULL, & timeVal);
}

int
enet_socket_wait (ENetSocket socket, enet_uint32 * condition, enet_uint32 timeout)
{
    struct pollfd pollSocket;
    int pollCount;

    pollSocket.fd = socket;
    pollSocket.events = 0;
    pollSocket.revents = 0;

    if (* condition & ENET_SOCKET_WAIT_SEND)
      pollSocket.events |= POLLOUT;

    if (* condition & ENET_SOCKET_WAIT_RECEIVE)
      pollSocket.events |= POLLIN;

    pollCount = poll (& pollSocket, 1, timeout);

    if (pollCount < 0)
    {
        if (errno == EINTR && * condition & ENET_SOCKET_WAIT_INTERRUPT)
        {
            * condition = ENET_SOCKET_WAIT_INTERRUPT;
            return 0;
        }
        return -1;
    }

    * condition = ENET_SOCKET_WAIT_NONE;

    if (pollCount == 0)
      return 0;

    if (pollSocket.revents & POLLOUT)
      * condition |= ENET_SOCKET_WAIT_SEND;

    if (pollSocket.revents & POLLIN)
      * condition |= ENET_SOCKET_WAIT_RECEIVE;

    return 0;
}
