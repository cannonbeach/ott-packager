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
  information, please contact us at cbfillet@gmail.com

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
    	void            *buffer;
} dataqueue_message_struct;

#if defined(__cplusplus)
extern "C" {
#endif

    void *dataqueue_create();
    int dataqueue_destroy(void *queue);
    int dataqueue_put_front(void *queue, dataqueue_message_struct *message);
    dataqueue_message_struct *dataqueue_take_back(void *queue);
	
#if defined(__cplusplus)
}
#endif

#endif // _DATAQUEUE_H_
