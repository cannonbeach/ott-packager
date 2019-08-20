/*****************************************************************************                                                                                                                             
  Copyright (C) 2018-2019 John William
                                                                                                                                                                                                           
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

*******************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "fillet.h"
#include "dataqueue.h"
#include "esignal.h"
#if defined(ENABLE_TRANSCODE)
#include "curl.h"
#endif

static volatile int signal_thread_running = 0;
static pthread_t signal_thread_id;

static void *signal_thread(void *context);
    
int start_signal_thread(fillet_app_struct *core)
{
    signal_thread_running = 1;
    pthread_create(&signal_thread_id, NULL, signal_thread, (void*)core);    
    return 0;
}

int stop_signal_thread(fillet_app_struct *core)
{
    signal_thread_running = 0;
    pthread_join(signal_thread_id, NULL);
    return 0;
}

#if defined(ENABLE_TRANSCODE)

void *signal_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;
    int ret;

    while (signal_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->signal_queue);
        while (!msg && signal_thread_running) {
            usleep(100000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->signal_queue);
        }
        if (signal_thread_running) {
            int buffer_type = msg->buffer_type;
          
            /*if (buffer_type == SIGNAL_SOMETHING) {
            }
            */
        }
        memory_return(core->fillet_msg_pool, msg);
        msg = NULL;
    }
        
    return NULL;
}
#endif // ENABLE_TRANSCODE

#if !defined(ENABLE_TRANSCODE)
void *signal_thread(void *context)
{
    return NULL;
}
#endif // !ENABLE_TRANSCODE
