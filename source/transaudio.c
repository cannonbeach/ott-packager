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
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <math.h>
#include <syslog.h>
#include "transaudio.h"

#if defined(ENABLE_TRANSCODE)

void *audio_decode_thread(void *context)
{
    return NULL;
}

void *audio_encode_thread(void *context)
{
    return NULL;
}

#else // ENABLE_TRANSCODE

void *audio_decode_thread(void *context)
{
    fprintf(stderr,"AUDIO DECODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

void *audio_encode_thread(void *context)
{
    fprintf(stderr,"AUDIO ENCODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

#endif
    



