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

#if !defined(_MEMORY_POOL_H_)
#define _MEMORY_POOL_H_

#if defined(__cplusplus)
extern "C" {
#endif
    
    void *memory_create(int buffer_count, int buffer_size);
    int memory_destroy(void *pool);
    void *memory_take(void *pool, int owner);
    int memory_return(void *pool, void *buffer);
    int memory_reset(void *pool);
    int memory_count(void *pool);

#if defined(__cplusplus)
}
#endif

#endif  // MEMORY_POOL_H_
