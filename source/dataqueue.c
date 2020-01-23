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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <malloc.h>

#include "dataqueue.h"
#include "mempool.h"

#define MAX_QUEUE_ENTRIES  16384

typedef struct _dataqueue_node_struct
{
    struct _dataqueue_node_struct  *prev;
    struct _dataqueue_node_struct  *next;
    dataqueue_message_struct       *message;
} dataqueue_node_struct;

typedef struct _queue_struct
{
    int                            count;
    dataqueue_node_struct          *tail;
    dataqueue_node_struct          *head;
    pthread_mutex_t                *reflock;
    void                           *pool;
} queue_struct;

int dataqueue_count(void *queue)
{
    int count;
    queue_struct *message_queue = (queue_struct *)queue;

    if (!message_queue) {
        return -1;
    }

    if (message_queue->reflock) {
        pthread_mutex_lock(message_queue->reflock);
        count = message_queue->count;
        pthread_mutex_unlock(message_queue->reflock);
        return count;
    }
    return -1;
}

int dataqueue_reset(void *queue)
{
    queue_struct *message_queue = (queue_struct *)queue;

    if (!message_queue) {
        return -1;
    }

    if (message_queue->reflock) {
        pthread_mutex_lock(message_queue->reflock);
        message_queue->count = 0;
        message_queue->head = NULL;
        message_queue->tail = NULL;
        memory_reset(message_queue->pool);
        pthread_mutex_unlock(message_queue->reflock);
        return 0;
    }

    return -1;
}

void *dataqueue_create(void)
{
    queue_struct *message_queue = (queue_struct *)malloc(sizeof(queue_struct));

    if (!message_queue) {
        return NULL;
    }

    memset(message_queue, 0, sizeof(queue_struct));

    message_queue->pool = memory_create(MAX_QUEUE_ENTRIES, sizeof(dataqueue_node_struct));
    message_queue->count = 0;
    message_queue->reflock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(message_queue->reflock, NULL);

    return message_queue;
}

int dataqueue_destroy(void *queue)
{
    queue_struct *message_queue = (queue_struct *)queue;
    if (!message_queue) {
        return -1;
    }

    pthread_mutex_destroy(message_queue->reflock);
    free(message_queue->reflock);
    message_queue->reflock = NULL;
    memory_destroy(message_queue->pool);
    free(message_queue);

    return 0;
}

int dataqueue_get_size(void *queue)
{
    queue_struct *message_queue = (queue_struct *)queue;
    int got_size = 0;

    if (!message_queue) {
        return -1;
    }
    pthread_mutex_lock(message_queue->reflock);
    got_size = message_queue->count;
    pthread_mutex_unlock(message_queue->reflock);

    return got_size;
}

int dataqueue_put_back(void *queue, dataqueue_message_struct *message)
{
    queue_struct *message_queue = (queue_struct *)queue;
    dataqueue_node_struct *new_node;
    //dataqueue_node_struct *new_node = (dataqueue_node_struct *)memory_take(message_queue->pool, 0xdeadface);

    if (!message_queue) {
        return -1;
    }

    new_node = (dataqueue_node_struct*)malloc(sizeof(dataqueue_node_struct));

    if (!new_node) {
        return -1;
    }

    pthread_mutex_lock(message_queue->reflock);
    if (message_queue->tail) {
        message_queue->tail->next = new_node;
        new_node->prev = message_queue->tail;
        new_node->next = NULL;
        new_node->message = message;
        message_queue->tail = new_node;
    } else {
        new_node->next = NULL;
        new_node->prev = NULL;
        new_node->message = message;
        message_queue->head = message_queue->tail = new_node;
    }
    message_queue->count++;
    pthread_mutex_unlock(message_queue->reflock);

    return 0;
}

int dataqueue_put_front(void *queue, dataqueue_message_struct *message)
{
    queue_struct *message_queue = (queue_struct *)queue;
    dataqueue_node_struct *new_node;

    if (!message_queue) {
        return -1;
    }

    new_node = (dataqueue_node_struct*)malloc(sizeof(dataqueue_node_struct));
    //new_node = (dataqueue_node_struct *)memory_take(message_queue->pool, 0xdeadface);
    if (!new_node) {
        return -1;
    }

    pthread_mutex_lock(message_queue->reflock);
    if (message_queue->head) {
        message_queue->head->prev = new_node;
        new_node->next = message_queue->head;
        new_node->prev = NULL;
        new_node->message = message;
        message_queue->head = new_node;
    } else {
        new_node->prev = NULL;
        new_node->next = NULL;
        new_node->message = message;
        message_queue->head = message_queue->tail = new_node;
    }
    message_queue->count++;
    pthread_mutex_unlock(message_queue->reflock);

    return 0;
}

dataqueue_message_struct *dataqueue_take_back(void *queue)
{
    queue_struct *message_queue = (queue_struct *)queue;
    dataqueue_node_struct *current_node = NULL;
    dataqueue_message_struct *return_message;

    if (!message_queue) {
        return NULL;
    }

    pthread_mutex_lock(message_queue->reflock);
    if (message_queue->tail) {
        current_node = message_queue->tail;
        message_queue->tail = current_node->prev;
        if (message_queue->tail) {
            message_queue->tail->next = NULL;
        } else {
            message_queue->head = NULL;
        }
        current_node->prev = NULL;
        message_queue->count--;
        return_message = current_node->message;
        //memory_return(message_queue->pool, current_node);
        free(current_node);
        pthread_mutex_unlock(message_queue->reflock);
        return return_message;
    }
    pthread_mutex_unlock(message_queue->reflock);

    return NULL;
}

dataqueue_message_struct *dataqueue_take_front(void *queue)
{
    queue_struct *message_queue = (queue_struct *)queue;
    dataqueue_node_struct *current_node = NULL;
    dataqueue_message_struct *return_message;

    if (!message_queue) {
        return NULL;
    }

    pthread_mutex_lock(message_queue->reflock);
    if (message_queue->head) {
        current_node = message_queue->head;
        message_queue->head = current_node->next;
        if (message_queue->head) {
            message_queue->head->prev = NULL;
        } else {
            message_queue->tail = NULL;
        }
        current_node->next = NULL;
        message_queue->count--;
        return_message = current_node->message;
        //memory_return(message_queue->pool, current_node);
        free(current_node);
        pthread_mutex_unlock(message_queue->reflock);
        return return_message;
    }
    pthread_mutex_unlock(message_queue->reflock);

    return NULL;
}
