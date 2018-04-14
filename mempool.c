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

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <syslog.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>

#include "mempool.h"

typedef struct _memory_struct
{  
    int                        disponible;
    int                        owner;
    uint8_t                    *memory;
} memory_struct;

typedef struct _memory_pool_struct
{
    int                        count;
    int                        size;
    int                        pos;
    pthread_mutex_t            *reflock;
    memory_struct              *refs;
    uint8_t                    *data;
} memory_pool_struct;

#define MEMORY_RESERVED      4

int memory_reset(void *pool)
{
    memory_pool_struct *memory_pool = (memory_pool_struct *)pool;
    if (memory_pool) {
	uint8_t *magicptr8;
	uint32_t *magicptr32;
        int count = memory_pool->count;
	int size = memory_pool->size;
	int i;

	memory_pool->pos = 0;
	magicptr8 = memory_pool->data;
	
	memset(magicptr8, 0, count * (size+MEMORY_RESERVED));
	
	for (i = 0; i < count; i++) {
	    magicptr32 = (uint32_t*)magicptr8;
	    *magicptr32 = i;
	    memory_pool->refs[i].memory = (uint8_t*)magicptr8;
	    if (i != (count-1)) {
	        magicptr8 += (size+MEMORY_RESERVED);          
	    }
	    memory_pool->refs[i].disponible = 1;
	}
	return 0;
    }    
    return -1;
}

void *memory_create(int count, int size)
{
    uint32_t chunk_size;
    uint8_t *magicptr8;
    uint32_t *magicptr32;
    int i;
    
    memory_pool_struct *memory_pool = (memory_pool_struct *)malloc(sizeof(memory_pool_struct));
    if (!memory_pool) {
        return NULL;
    }

    chunk_size = count * (size+MEMORY_RESERVED);
    memset(memory_pool, 0, sizeof(memory_pool_struct));

    memory_pool->refs = (memory_struct*)malloc(sizeof(memory_struct)*count);
    if (!memory_pool->refs) {
	free(memory_pool);
	return NULL;
    }
    memset(memory_pool->refs, 0, sizeof(memory_struct)*count);
    
    memory_pool->count = count;
    memory_pool->pos = 0;
    memory_pool->size = size;
    memory_pool->reflock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (!memory_pool->reflock) {
	free(memory_pool->refs);
	free(memory_pool);
	return NULL;
    }    
    memory_pool->data = (uint8_t*)malloc(chunk_size);
    if (!memory_pool->data) {
	free(memory_pool->refs);
	free(memory_pool->reflock);
	free(memory_pool);
	return NULL;
    }
    
    memset(memory_pool->data, 0, chunk_size);

    pthread_mutex_init(memory_pool->reflock, NULL);
    
    magicptr8 = memory_pool->data;
    for (i = 0; i < count; i++) {
        magicptr32 = (uint32_t*)magicptr8;	
        *magicptr32 = i;
        memory_pool->refs[i].memory = (uint8_t*)magicptr8;
        magicptr8 += (size+MEMORY_RESERVED);    
        memory_pool->refs[i].disponible = 1;
    }
    
    return memory_pool;
}

int memory_destroy(void *pool)
{
    int i;
    memory_pool_struct *memory_pool = (memory_pool_struct *)pool;

    if (!memory_pool) {
        return -1;
    }

    free(memory_pool->data);
    pthread_mutex_destroy(memory_pool->reflock);
    free(memory_pool->reflock);
    memory_pool->reflock = NULL;

    for (i = 0; i < memory_pool->count; i++)
    {       
        memory_pool->refs[i].memory = NULL;
    }

    free(memory_pool->refs);
    free(memory_pool);
    memory_pool = NULL;

    return 0;
}

void *memory_take(void *pool, int owner)
{
    uint8_t *taken = NULL;
    int pos;
    int count = 0;
    int i;
    memory_pool_struct *memory_pool = (memory_pool_struct *)pool;
    
    if (!memory_pool) {
        return NULL;
    }

    pthread_mutex_lock(memory_pool->reflock);
    count = memory_pool->count;
    for (i = 0; i < count; i++) {
        pos = memory_pool->pos;
        if (memory_pool->refs[pos].disponible) {
            memory_pool->refs[pos].disponible = 0;
            memory_pool->pos = (memory_pool->pos + 1) % count;
	    memory_pool->refs[pos].owner = owner;

	    taken = memory_pool->refs[pos].memory + MEMORY_RESERVED;           
	    pthread_mutex_unlock(memory_pool->reflock);
	    
	    return taken;
        }
	memory_pool->pos = (memory_pool->pos + 1) % count;
    }    
    pthread_mutex_unlock(memory_pool->reflock);
    return NULL;
}

int memory_return(void *pool, void *memory)
{
    uint8_t *returned;
    uint32_t *magicptr32;
    uint32_t idx;    
    memory_pool_struct *memory_pool;

    if (!memory) {
	return -1;
    }
    
    memory_pool = (memory_pool_struct *)pool;
    if (!memory_pool) {
        return -1;
    }

    returned = (uint8_t*)memory - MEMORY_RESERVED;
    magicptr32 = (uint32_t*)returned;
    idx = *magicptr32; 

    pthread_mutex_lock(memory_pool->reflock);
    if (idx > memory_pool->count) {
	pthread_mutex_unlock(memory_pool->reflock);
	return -1;
    }
    
    if (memory_pool->refs[idx].disponible != 0) {
	pthread_mutex_unlock(memory_pool->reflock);
	return -1;
    } else {
	memory_pool->refs[idx].disponible = 1;
    }
    
    pthread_mutex_unlock(memory_pool->reflock);

    return 0;
}

int memory_unused(void *pool)
{
    memory_pool_struct *memory_pool = (memory_pool_struct *)pool;

    if (memory_pool) {
	int count;
	int unused = 0;
	int i;
	
	pthread_mutex_lock(memory_pool->reflock);
	count = memory_pool->count;
	for (i = 0; i < count; i++) {
	    unused += memory_pool->refs[i].disponible;
	}
	pthread_mutex_unlock(memory_pool->reflock);
	
	return unused;
    } 
    return 0;
}


