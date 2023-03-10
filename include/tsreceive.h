/*****************************************************************************
  Copyright (C) 2018-2023 John William

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

#if !defined(_TS_RECEIVE_H_)
#define _TS_RECEIVE_H_

typedef struct _udp_thread_data_struct_ {
    fillet_app_struct *core;
    int               source_index;
    char              udp_source_ipaddr[MAX_STR_SIZE];
    int               udp_source_port;
} udp_thread_data_struct;

void *udp_source_thread(void *context);

#endif // _TS_RECEIVE_H_
