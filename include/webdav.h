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

#if !defined(_WEBDAV_H_)
#define _WEBDAV_H_

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#define WEBDAV_UPLOAD 0x11
#define WEBDAV_CREATE 0x22
#define WEBDAV_DELETE 0x33

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

    int start_webdav_threads(fillet_app_struct *core);
    int stop_webdav_threads(fillet_app_struct *core);

#if defined(__cplusplus)
}
#endif // __cplusplus

#endif // _WEBDAV_H_
