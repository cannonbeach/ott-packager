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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <math.h>
#include <syslog.h>
#include "fillet.h"
#include "tsdecode.h"
#include "mempool.h"
#include "dataqueue.h"
#include "udpsource.h"
#include "tsreceive.h"

static int source_count = 0;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;

void *udp_source_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    transport_data_struct *tsdata;
    int udp_socket;
    int timeout_ms = 1000;
    fd_set sockset;
    uint8_t *udp_buffer;
    int udp_buffer_size;
    int i;

#define MAX_UDP_BUFFER_READ 2048
    pthread_mutex_lock(&start_lock);
    
    udp_buffer = (uint8_t*)malloc(MAX_UDP_BUFFER_READ);
    udp_buffer_size = MAX_UDP_BUFFER_READ;
    tsdata = (transport_data_struct*)malloc(sizeof(transport_data_struct));

    memset(tsdata, 0, sizeof(transport_data_struct));
    
    for (i = 0; i< MAX_ACTUAL_PIDS; i++) {
	 tsdata->initial_pcr_low[i] = -1;
    }
    tsdata->pat_program_count = -1;
    tsdata->pat_version_number = -1;
    tsdata->pat_transport_stream_id = -1;
    tsdata->source = source_count;

    udp_socket = socket_udp_open(core->fillet_input[source_count].interface,
				 core->fillet_input[source_count].udp_source_ipaddr,
				 core->fillet_input[source_count].udp_source_port,
				 0, UDP_FLAG_INPUT, 1);
    
    source_count++;
    memset(tsdata->pmt_version, -1, sizeof(tsdata->pmt_version));

    pthread_mutex_unlock(&start_lock);

    if (udp_socket < 0) {
	core->source_running = 0;
	goto _cleanup_udp_source_thread;
    }    

    while (1) {
	int is_thread_running;
	int anysignal;

	is_thread_running = core->source_running;
	if (!is_thread_running || udp_socket < 0) {
	    core->source_running = 0;
	    goto _cleanup_udp_source_thread;
	}

	anysignal = socket_udp_ready(udp_socket, timeout_ms, &sockset);
	if (anysignal == 0) {
	    fprintf(stderr,"no source signal\n");
	    continue;
	}

	if (FD_ISSET(udp_socket, &sockset)) {
	    int bytes = socket_udp_read(udp_socket, udp_buffer, udp_buffer_size);
	    if (bytes > 0) {
		int total_packets = bytes / 188;
		if (total_packets > 0) {
		    decode_packets(udp_buffer, total_packets, tsdata);
		}
	    }
	}
    }

_cleanup_udp_source_thread:
    if (udp_socket > 0) {
	socket_udp_close(udp_socket);
    }
    free(tsdata);

    source_count = 0;

    return NULL;
}
