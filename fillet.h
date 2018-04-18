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

#if !defined(_FILLET_H_)
#define _FILLET_H_

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <math.h>
#include <syslog.h>
#include "fgetopt.h"
#include "dataqueue.h"
#include "mempool.h"
#include "udpsource.h"
#include "tsdecode.h"
#include "mp4core.h"

#define MAX_STR_SIZE               512
#define MAX_AUDIO_SOURCES          5
#define MAX_VIDEO_SOURCES          5
#define MAX_FRAME_DATA_SYNC_AUDIO  512
#define MAX_FRAME_DATA_SYNC_VIDEO  256
#define MAX_MUX_SOURCES            10
#define MAX_VIDEO_MUX_BUFFER       1024*1024*4
#define MAX_AUDIO_MUX_BUFFER       1024*64
#define MAX_VIDEO_PES_BUFFER       1024*1024*4
#define MAX_WINDOW_SIZE            25
#define MIN_WINDOW_SIZE            3
#define DEFAULT_WINDOW_SIZE        5
#define MAX_SEGMENT_LENGTH         10
#define MIN_SEGMENT_LENGTH         1
#define DEFAULT_SEGMENT_LENGTH     5
#define MAX_ROLLOVER_SIZE          128
#define MIN_ROLLOVER_SIZE          32
#define OVERFLOW_DTS               8589100000

#define FRAME_TYPE_VIDEO     0x01
#define FRAME_TYPE_AUDIO     0x02

#define FRAME_TYPE_SYNC      0x01

typedef struct _ip_config_struct_ {
    char             active_ip[UDP_MAX_IFNAME];
    int              active_port;
} ip_config_struct;

typedef struct _config_options_struct_ {
    int              active_sources;
    char             active_interface[UDP_MAX_IFNAME];
    ip_config_struct active_source[MAX_MUX_SOURCES];

    char             manifest_directory[MAX_STR_SIZE];

    int              window_size;
    int              segment_length;
    int              rollover_size;
    int              identity;

    int              enable_ts_output;
    int              enable_fmp4_output;
} config_options_struct;

typedef struct _packet_struct_ {
    int64_t                  pts;
    int64_t                  dts;
    int                      sync;
    int                      disflag;
    int                      count;
} packet_struct;

typedef struct _stream_struct_ {
    int                      sources;
    uint8_t                  *muxbuffer;
    uint8_t                  *pesbuffer;
    packet_struct            *packettable;
    int                      packet_count;
    int64_t                  file_sequence_number;
    int64_t                  media_sequence_number;
    int64_t                  fragments_published;
    FILE                     *output_ts_file;
    FILE                     *output_fmp4_file;

    void                     *source_queue;
    int                      cnt;
    int                      prev_cnt;

    int                      pat_cnt;
    int                      pmt_cnt;

    fragment_file_struct     *fmp4;    
} stream_struct;

typedef struct _hlsmux_struct_ {
    void                     *input_queue;
    
    stream_struct            audio[MAX_AUDIO_SOURCES];
    stream_struct            video[MAX_VIDEO_SOURCES];
    
    pthread_t                hlsmux_thread_id;
} hlsmux_struct;

typedef struct _sorted_frame_struct_
{
    uint8_t                *buffer;
    int                    buffer_size;
    int64_t                pts;
    int64_t                dts;
    int64_t                full_time;
    int64_t                first_timestamp;
    int                    source;
    int                    sub_stream;
    int                    sync_frame;
    int                    frame_type;    
    int64_t                time_received;
    char                   lang_tag[4];
} sorted_frame_struct;

typedef struct _audio_stream_struct_
{
    int64_t                current_receive_count;
    int64_t                first_timestamp;
    int64_t                last_timestamp_pts;
    int64_t                overflow_pts;
    int64_t                audio_bitrate;
    int64_t                total_audio_bytes;
    struct timespec        audio_clock_start;
    void                   *audio_queue;    
} audio_stream_struct;

typedef struct _video_stream_struct_
{
    int64_t                current_receive_count;
    int64_t                last_intra_count;
    int                    found_key_frame;
    int64_t                first_timestamp;
    int64_t                last_timestamp_pts;
    int64_t                last_timestamp_dts;
    int64_t                overflow_pts;
    int64_t                overflow_dts;
    int64_t                video_bitrate;
    int64_t                total_video_bytes;
    struct timespec        video_clock_start;
    void                   *video_queue;    
} video_stream_struct;

typedef struct _source_stream_struct_
{    
    video_stream_struct    *video_stream;
    audio_stream_struct    *audio_stream[MAX_AUDIO_SOURCES];

    int                    udp_source_socket;
    char                   udp_source_ipaddr[UDP_MAX_IFNAME];
    pthread_t              udp_source_thread_id;
} source_stream_struct;

typedef struct _input_struct_ {
    char                   interface[UDP_MAX_IFNAME];
    char                   udp_source_ipaddr[UDP_MAX_IFNAME];
    int                    udp_source_port;
} input_struct;

typedef struct _fillet_app_struct_
{
    int                    num_sources;
    source_stream_struct   *source_stream;
    int                    source_running;
    
    void                   *video_frame_pool;
    void                   *audio_frame_pool;    

    sorted_frame_struct    *video_frame_data[MAX_FRAME_DATA_SYNC_VIDEO];
    sorted_frame_struct    *audio_frame_data[MAX_FRAME_DATA_SYNC_AUDIO];

    hlsmux_struct          *hlsmux;

    config_options_struct  *cd;

    input_struct           fillet_input[MAX_MUX_SOURCES];
} fillet_app_struct;

#endif // _FILLET_H_
