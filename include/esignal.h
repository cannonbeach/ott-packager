/*****************************************************************************
  Copyright (C) 2019 John William
 
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

#if !defined(_SIGNAL_H_)
#define _SIGNAL_H_

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "fillet.h"

#define SIGNAL_START_SERVICE         0x01
#define SIGNAL_STOP_SERVICE          0x02
#define SIGNAL_NO_INPUT_SIGNAL       0x03
#define SIGNAL_SERVICE_RESTART       0x04
#define SIGNAL_FAILOVER              0x05
#define SIGNAL_SCTE35_START          0x06
#define SIGNAL_SCTE35_END            0x07
#define SIGNAL_SEGMENT_PUBLISHED     0x08
#define SIGNAL_SEGMENT_FAILED        0x09
#define SIGNAL_HIGH_CPU              0x0a
#define SIGNAL_LOW_DISK_SPACE        0x0b
#define SIGNAL_INPUT_SIGNAL_LOCKED   0x0c
#define SIGNAL_INPUT_ERRORS          0x0d  //hit specific error threshold
#define SIGNAL_SEGMENT_WRITTEN       0x0f
#define SIGNAL_MANIFEST_WRITTEN      0x10
#define SIGNAL_FRAME_REPEAT          0x11
#define SIGNAL_INSERT_SILENCE        0x12
#define SIGNAL_DROP_AUDIO            0x13
#define SIGNAL_DECODE_ERROR          0x14
#define SIGNAL_ENCODE_ERROR          0x15
#define SIGNAL_PARSE_ERROR           0x16
#define SIGNAL_MALFORMED_DATA        0x17

#define SIGNAL_DIRECT_ERROR_AVSYNC   0xe0
#define SIGNAL_DIRECT_ERROR_MSGPOOL  0xe1
#define SIGNAL_DIRECT_ERROR_RAWPOOL  0xe2
#define SIGNAL_DIRECT_ERROR_NALPOOL  0xe3
#define SIGNAL_DIRECT_ERROR_UNKNOWN  0xe4
#define SIGNAL_DIRECT_ERROR_IP       0xe5
#define SIGNAL_DIRECT_ERROR_CPU      0xe6

#define SIGNAL_UNKNOWN_VIDEO_TYPE    0xf1
#define SIGNAL_UNKNOWN_AUDIO_TYPE    0xf2


#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

    int start_signal_thread(fillet_app_struct *core);
    int stop_signal_thread(fillet_app_struct *core);
    int send_signal(fillet_app_struct *core, int signal_type, const char *message);
    int send_direct_error(fillet_app_struct *core, int signal_type, const char *message);        
    
#if defined(__cplusplus)
}
#endif // __cplusplus

#endif // _SIGNAL_H_
