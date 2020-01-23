/*****************************************************************************
  Copyright (C) 2018-2020 John William

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111, USA.

  This program is also available with customization/support packages.
  For more information, please contact me at cannonbeachgoonie@gmail.com

******************************************************************************/

#if !defined(_UDP_SOURCE_H_)
#define _UDP_SOURCE_H_

#include <sys/select.h>

#define UDP_FLAG_OUTPUT     0x01
#define UDP_FLAG_INPUT      0x00
#define UDP_FLAG_MULTICAST  0xff

#define UDP_MAX_SOCKETS     32
#define UDP_MAX_IFNAME      32
#define UDP_MAX_SOCKET_SIZE 1024*1024

#if defined(__cplusplus)
extern "C" {
#endif

    void socket_udp_global_init();
    void socket_udp_global_destroy();
    int socket_udp_close(int udp_socket);
    int socket_udp_open(const char *iface,
                        const char *addr,
                        int port, int mcast,
                        int flags,
                        int ttl);
    int socket_udp_read(int udp_socket, uint8_t *buf, int size);
    int socket_udp_ready(int udp_socket, int timeout, fd_set *sockset);

#if defined(__cplusplus)
}
#endif

#endif // _UDP_SOURCE_H_
