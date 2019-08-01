/*****************************************************************************
  Copyright (C) 2018 Fillet
 
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
 
  This program is also available under a commercial license with
  customization/support packages and additional features.  For more 
  information, please contact us at cannonbeachgoonie@gmail.com

******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <netdb.h>
#include <semaphore.h>
#include <string.h>
#include <signal.h>
#include <execinfo.h>
#include <syslog.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <math.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <linux/if_ether.h>
#include <linux/filter.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "udpsource.h"

typedef struct _local_socket_struct_ {
    int                       unused;
    int                       udp_socket;
    int                       mcast;
    struct ip_mreq            multicastreq;
    struct ip_mreq_source     mreq_source;    
} local_socket_struct;

static local_socket_struct socket_table[UDP_MAX_SOCKETS];

void socket_udp_global_init()    
{
    int i;
    for (i = 0; i < UDP_MAX_SOCKETS; i++) {
	memset(&socket_table[i], 0, sizeof(local_socket_struct));
        socket_table[i].unused = 1;
	socket_table[i].udp_socket = -1;
    }
    return;
}

int socket_udp_close(int udp_socket)
{  
    int i;
    for (i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (socket_table[i].udp_socket == udp_socket &&
	    socket_table[i].unused == 0) {
	    if (socket_table[i].mcast) {
		setsockopt(udp_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
			   &socket_table[i].multicastreq,
			   sizeof(socket_table[i].multicastreq));
	    }
	    socket_table[i].unused = 1;
	    socket_table[i].udp_socket = -1;
	    socket_table[i].mcast = 0;
	    close(udp_socket);
	    return 0;	    
	}
    }
    return -1;
}

void socket_udp_global_destroy()
{
    int i;
    for (i = 0; i < UDP_MAX_SOCKETS; i++) {
	if (socket_table[i].udp_socket != -1) {
	    socket_udp_close(socket_table[i].udp_socket);	    
	}
    }
    return;
}

int socket_udp_open(const char *iface, const char *addr, int port, int mcast, int flags, int ttl)    
{
    struct ifreq ifr;
    struct sockaddr_in local_addr;
    struct in_addr ip_addr;    
    struct ip_mreq multicastreq;
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    struct in_addr interface_address;

    int new_socket;
    int reuse = 1;
    int retcode;
    int size = UDP_MAX_SOCKET_SIZE;
    char interface_name[UDP_MAX_IFNAME];
    int family = AF_INET;
    int s;
    char host[NI_MAXHOST];
    struct in_addr addrcheck;

    if (inet_aton(addr, &addrcheck) == 0) {
	fprintf(stderr,"ERROR: INVALID IP ADDRESS: %s\n",
		addr);
	return -1;
    }
    

    memset(&ifr,0,sizeof(ifr));
    memset(interface_name,0,sizeof(interface_name));

    snprintf(interface_name, UDP_MAX_IFNAME-1, "%s", iface);
    retcode = inet_aton(addr, &ip_addr);
    if (!retcode) {
	return -1;
    }

    if (!strcmp(inet_ntoa(ip_addr), "127.0.0.1")) {
        strncpy(interface_name, "lo", UDP_MAX_IFNAME-1);
    }

    new_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (new_socket < 0) {
        return -1;
    }

    retcode = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (retcode < 0) {       
	socket_udp_close(new_socket);	
        return -1;
    }

    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interface_name);
    
    retcode = setsockopt(new_socket, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr));
    if (retcode < 0) {
	fprintf(stderr,"error: unable to bind to device: %s (are you running as root?  does the device exist?)\n", interface_name);
	socket_udp_close(new_socket);	    
	return -1;
    }

    if (getifaddrs(&ifaddr) == -1) {
	fprintf(stderr,"getifaddrs fail\n");
	socket_udp_close(new_socket);
	return -1;
    }    

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
	if (ifa->ifa_addr) {
	    family = ifa->ifa_addr->sa_family;        
	}
	
	if ((!strcasecmp(ifa->ifa_name, interface_name)) && (family == AF_INET)) {
	    s = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) :
			    sizeof(struct sockaddr_in6),
			    host, NI_MAXHOST, NULL,
			    0, NI_NUMERICHOST);           
	}
    }
    freeifaddrs(ifaddr); 
    inet_aton(host, &interface_address);

    local_addr.sin_family = AF_INET;    
    if (flags == UDP_FLAG_INPUT) {
        fprintf(stderr,"status: setting receiver socket size to: %d\n", size);
        retcode = setsockopt(new_socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        if (retcode < 0) {
	    socket_udp_close(new_socket);
            return -1;
        }
	local_addr.sin_port = htons(port);
	local_addr.sin_addr.s_addr = ip_addr.s_addr;
    }

    if (flags == UDP_FLAG_OUTPUT) {
        fprintf(stderr,"status: setting output socket size to: %d\n", size);
        retcode = setsockopt(new_socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
	if (retcode < 0) {
	    socket_udp_close(new_socket);
            return -1;
        }	
        local_addr.sin_addr.s_addr = interface_address.s_addr;
	local_addr.sin_port = htons(INADDR_ANY);
    }

    fprintf(stderr,"status: binding to local address\n");
    retcode = bind(new_socket, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (retcode < 0) {
	socket_udp_close(new_socket);
	
	return -1;
    }

    fprintf(stderr,"status: mcast flag is: %d\n", mcast);
 
    if (mcast && flags == UDP_FLAG_OUTPUT) {
        struct in_addr my_interface;
	uint8_t local_ttl;

        fprintf(stderr,"status: interface is defined as output: ttl:%d\n", ttl);
        memset(&my_interface, 0, sizeof(my_interface));
        memset((void*)&multicastreq, 0, sizeof(multicastreq));	
	if (ttl > 32) {
	    ttl = 32;
	}
	if (ttl < 0) {
	    ttl = 0;
	}
	local_ttl = (uint8_t)ttl;
	
        multicastreq.imr_multiaddr.s_addr = interface_address.s_addr;

        setsockopt(new_socket, IPPROTO_IP, IP_MULTICAST_TTL, &local_ttl, sizeof(local_ttl));
	setsockopt(new_socket, IPPROTO_IP, IP_MULTICAST_IF, (char*)&interface_address, sizeof(interface_address));
	
	for (s = 0; s < UDP_MAX_SOCKETS; s++) {
	    if (socket_table[s].unused) {
		socket_table[s].unused = 0;
		socket_table[s].udp_socket = new_socket;
		socket_table[s].mcast = 0;   // only signal for receiver
		break;
	    }
	}		
    } else {
        fprintf(stderr,"status: interface is defined as input: %s\n", host);
	for (s = 0; s < UDP_MAX_SOCKETS; s++) {
            fprintf(stderr,"status: checking for available receive socket(%d): %d\n", s, socket_table[s].unused);
	    if (socket_table[s].unused) {
                fprintf(stderr,"status: using socket index %d!\n", s);
		socket_table[s].unused = 0;
		socket_table[s].udp_socket = new_socket;
		socket_table[s].mcast = mcast;
		if (mcast) {
		    socket_table[s].multicastreq.imr_interface.s_addr = interface_address.s_addr;
		    socket_table[s].multicastreq.imr_multiaddr.s_addr = ip_addr.s_addr;

                    fprintf(stderr,"status: Sending out IGMP JOIN request on interface: %s\n", host);
		    setsockopt(new_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			       &socket_table[s].multicastreq, 
			       sizeof(socket_table[s].multicastreq));
		} else {
                    fprintf(stderr,"status: unicast interface: %s\n", host);
                }
		break;
	    }
	}
    }

    fprintf(stderr,"status: newly created socket: %d\n", new_socket);
    return new_socket;
}

int socket_udp_read(int udp_socket, uint8_t *buf, int size)
{
    ssize_t bytes;

    bytes = recvfrom(udp_socket, (void*)buf, size, 0, NULL, NULL);

    return (int)bytes;    
}

int socket_udp_ready(int udp_socket, int timeout, fd_set *sockset)
{
    int retcode;
    int largest_socket;
    struct timeval tdata;

    FD_ZERO(sockset);
    FD_SET(udp_socket, sockset);
    
    largest_socket = udp_socket;
    tdata.tv_sec = timeout / 1000;
    tdata.tv_usec = 1000 * (timeout % 1000);

    retcode = select(largest_socket + 1,
		     sockset, NULL, NULL,
		     &tdata);
    if (retcode < 0) {        
        FD_ZERO(sockset);
        return -1;
    }
    return retcode;
}


