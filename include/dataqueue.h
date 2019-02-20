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

#if !defined(_DATAQUEUE_H_)
#define _DATAQUEUE_H_

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

typedef struct _dataqueue_message_struct_ {
	int             buffer_type;
	unsigned long   buffer_size;
        int64_t         pts;
	int64_t         dts;
        uint32_t        flags;
        int             source_discontinuity;
        uint8_t         tff;
        uint8_t         interlaced;
        int             fps_num;
        int             fps_den;
        int             aspect_num;
        int             aspect_den;
        int             width;
        int             height;
        uint8_t         stream_index;
        int             channels;
        int             sample_rate;
        int64_t         first_pts;
        int             caption_size;
        int             splice_point;
        int64_t         splice_duration;
        int64_t         splice_duration_remaining;
        uint8_t         *caption_buffer;
    	void            *buffer;
} dataqueue_message_struct;

#if defined(__cplusplus)
extern "C" {
#endif

    void *dataqueue_create();
    int dataqueue_destroy(void *queue);
    int dataqueue_get_size(void *queue);
    int dataqueue_put_front(void *queue, dataqueue_message_struct *message);
    dataqueue_message_struct *dataqueue_take_back(void *queue);
	
#if defined(__cplusplus)
}
#endif

#endif // _DATAQUEUE_H_
