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
#define MAX_AUDIO_STREAMS          5
#define MAX_VIDEO_SOURCES          8
#define MAX_FRAME_DATA_SYNC_AUDIO  2048
#define MAX_FRAME_DATA_SYNC_VIDEO  1024
#define MAX_MUX_SOURCES            10
#define MAX_TRANS_OUTPUTS          MAX_VIDEO_SOURCES
#define MAX_VIDEO_MUX_BUFFER       1024*1024*4
#define MAX_AUDIO_MUX_BUFFER       1024*64
#define MAX_VIDEO_PES_BUFFER       1024*1024*4
#define MAX_TEXT_BUFFER            1024*1024
#define MAX_WINDOW_SIZE            25
#define MIN_WINDOW_SIZE            3
#define DEFAULT_WINDOW_SIZE        5
#define MAX_SEGMENT_LENGTH         10
#define MIN_SEGMENT_LENGTH         1
#define DEFAULT_SEGMENT_LENGTH     5
#define MAX_ROLLOVER_SIZE          128
#define MIN_ROLLOVER_SIZE          32
#define OVERFLOW_DTS               8589100000
#define MAX_SESSIONS               5
#define MAX_SOURCES                8
#define TBD                        0xff

#define MAX_MSG_BUFFERS                   8192
#define MAX_FRAME_BUFFERS                 (MAX_FRAME_DATA_SYNC_VIDEO+MAX_FRAME_DATA_SYNC_AUDIO)*2
#define MAX_VIDEO_COMPRESSED_BUFFERS      4096
#define MAX_VIDEO_COMPRESSED_BUFFER_SIZE  0
#define MAX_AUDIO_COMPRESSED_BUFFERS      4096
#define MAX_AUDIO_COMPRESSED_BUFFER_SIZE  0
#define MAX_VIDEO_RAW_BUFFERS             512
#define MAX_VIDEO_RAW_BUFFER_SIZE         0
#define MAX_AUDIO_RAW_BUFFERS             2048
#define MAX_AUDIO_RAW_BUFFER_SIZE         0

#define FRAME_TYPE_VIDEO           0x01
#define FRAME_TYPE_AUDIO           0x02

#define FRAME_TYPE_SYNC            0x01

#define MSG_START                  0xa1
#define MSG_STOP                   0xa2
#define MSG_RESTART                0xa3
#define MSG_RESPAWN                0xa4
#define MSG_PING                   0xb1
#define MSG_STATUS                 0xb2

#define ENCODER_QUALITY_BASIC      0x00
#define ENCODER_QUALITY_MEDIUM     0x01
#define ENCODER_QUALITY_HIGH       0x02
#define ENCODER_QUALITY_CRAZY      0x03

#define ENCODER_PROFILE_BASE       66
#define ENCODER_PROFILE_MAIN       77
#define ENCODER_PROFILE_HIGH       100

#if defined(ENABLE_TRANSCODE)
typedef struct _trans_video_output_struct_ {
    int                    video_codec;
    int                    width;
    int                    height;
    int                    video_bitrate;
    int                    aspect_num;
    int                    aspect_den;
    int                    encoder_quality;
    int                    encoder_profile;
    int                    encoder_level;
    char                   encoder_string[MAX_STR_SIZE]; // this is the avc1/hvc1...
} trans_video_output_struct;

typedef struct _trans_audio_output_struct_ {
    int                    audio_codec;
    int                    audio_bitrate;
} trans_audio_output_struct;

typedef struct _preparevideo_internal_struct_ {
    void                   *input_queue;
} preparevideo_internal_struct;

typedef struct _transvideo_internal_struct_ {
    void                   *input_queue;
} transvideo_internal_struct;

typedef struct _transaudio_internal_struct_ {
    void                   *input_queue;
} transaudio_internal_struct;

typedef struct _encodevideo_internal_struct_ {
    void                   *input_queue[MAX_TRANS_OUTPUTS];
    void                   *thumbnail_queue;
} encodevideo_internal_struct;

typedef struct _scalevideo_internal_struct_ {
    void                   *input_queue;
} scalevideo_internal_struct;

typedef struct _encodeaudio_internal_struct_ {
    void                   *input_queue;
} encodeaudio_internal_struct;

#endif // ENABLE_TRANSCODE

typedef struct _ip_config_struct_ {
    char             active_ip[UDP_MAX_IFNAME];
    int              active_port;
} ip_config_struct;

typedef struct _config_options_struct_ {
    int              active_sources;
    char             active_interface[UDP_MAX_IFNAME];
    ip_config_struct active_source[MAX_MUX_SOURCES];

    char             manifest_directory[MAX_STR_SIZE];
    char             youtube_cid[MAX_STR_SIZE];
    char             manifest_dash[MAX_STR_SIZE];
    char             manifest_hls[MAX_STR_SIZE];
    char             manifest_fmp4[MAX_STR_SIZE];

    char             cdn_username[MAX_STR_SIZE];
    char             cdn_password[MAX_STR_SIZE];
    char             cdn_server[MAX_STR_SIZE];
    char             management_server[MAX_STR_SIZE];

    int              window_size;
    int              segment_length;
    int              rollover_size;
    int              identity;

    int              enable_ts_output;
    int              enable_fmp4_output;
    int              enable_youtube_output;
    int              audio_source_index;

    int              enable_scte35;
    int              enable_stereo;
    int              enable_webvtt;
#if defined(ENABLE_TRANSCODE)
    int                           num_outputs;
    trans_video_output_struct     transvideo_info[MAX_TRANS_OUTPUTS];
    trans_audio_output_struct     transaudio_info[MAX_AUDIO_SOURCES];
#endif // ENABLE_TRANSCODE
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
    char                     *textbuffer;
    packet_struct            *packettable;
    int                      packet_count;
    int64_t                  file_sequence_number;
    int64_t                  media_sequence_number;
    int64_t                  fragments_published;
    int64_t                  discontinuity_adjustment;
    int64_t                  last_segment_time;
    FILE                     *output_ts_file;
    FILE                     *output_fmp4_file;
    FILE                     *output_webvtt_file;

    void                     *source_queue;
    int                      cnt;
    int                      prev_cnt;

    int                      pat_cnt;
    int                      pmt_cnt;

    fragment_file_struct     *fmp4;
} stream_struct;

typedef struct _hlsmux_struct_ {
    void                     *input_queue;

    stream_struct            audio[MAX_VIDEO_SOURCES][MAX_AUDIO_STREAMS];
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
    int                    duration;
    int                    source;
    int                    sub_stream;
    int                    sync_frame;
    int                    frame_type;
    int                    media_type;
    int                    splice_point;
    int64_t                splice_duration;
    int64_t                splice_duration_remaining;
    int64_t                time_received;
    char                   lang_tag[4];
} sorted_frame_struct;

typedef struct _audio_stream_struct_
{
    int64_t                current_receive_count;
    int64_t                first_timestamp;
    int64_t                last_timestamp_pts;
    int64_t                last_full_time;
    int64_t                overflow_pts;
    int64_t                audio_bitrate;
    int64_t                total_audio_bytes;
    int                    audio_samples_to_add;
    int                    audio_samples_to_drop;
    struct timespec        audio_clock_start;
    int                    audio_channels;
    int                    audio_object_type;
    int                    audio_samplerate;
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
    int64_t                last_full_time;
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
    audio_stream_struct    *audio_stream[MAX_AUDIO_STREAMS];

    int                    udp_source_socket;
    char                   udp_source_ipaddr[UDP_MAX_IFNAME];
    pthread_t              udp_source_thread_id;
} source_stream_struct;

typedef struct _input_struct_ {
    char                   interface[UDP_MAX_IFNAME];
    char                   udp_source_ipaddr[UDP_MAX_IFNAME];
    int                    udp_source_port;
} input_struct;

typedef struct _decoded_source_info_struct_ {
    int                    source_pmt_pid;
    int                    source_video_pid;
    int                    source_audio_pid[MAX_AUDIO_STREAMS];

    int                    decoded_width;
    int                    decoded_height;
    int                    decoded_fps_num;
    int                    decoded_fps_den;
    int                    decoded_aspect_num;
    int                    decoded_aspect_den;
    int                    decoded_video_media_type;
    int                    decoded_audio_media_type[MAX_AUDIO_STREAMS];
    int                    decoded_audio_channels_input[MAX_AUDIO_STREAMS];
    int                    decoded_audio_channels_output[MAX_AUDIO_STREAMS];
    int                    decoded_audio_sample_rate[MAX_AUDIO_STREAMS];
} decoded_source_info_struct;

typedef struct _fillet_app_struct_
{
    int                           session_id;

    int                           num_sources;
    source_stream_struct          *source_stream;
    int                           source_running;

    decoded_source_info_struct    decoded_source_info;  // only valid on transcode mode

    void                          *video_frame_pool;
    void                          *audio_frame_pool;

    sorted_frame_struct           *video_frame_data[MAX_FRAME_DATA_SYNC_VIDEO];
    sorted_frame_struct           *audio_frame_data[MAX_FRAME_DATA_SYNC_AUDIO];

    void                          *event_queue;
    void                          *webdav_queue;
    void                          *signal_queue;

    hlsmux_struct                 *hlsmux;

    config_options_struct         *cd;

    input_struct                  fillet_input[MAX_MUX_SOURCES];

    time_t                        t_avail;
    int                           timeset;

    int                           transcode_enabled;

    // these are some basic runtime stats- should move to different data structure
    int64_t                       uptime;
    int64_t                       error_count;
    char                          last_error[MAX_STR_SIZE];
    int                           input_signal;
    int                           source_interruptions;
    int                           sync_thread_restart_count;

    int                           video_receive_time_set;
    struct timespec               video_receive_time;

    int                           video_decode_time_set;
    struct timespec               video_decode_time;

    int                           video_encode_time_set;
    struct timespec               video_encode_time;

    int                           video_output_time_set;
    struct timespec               video_output_time;

    int                           scte35_ready;
    int                           scte35_triggered;
    int64_t                       scte35_pts;
    int64_t                       scte35_duration;
    int64_t                       scte35_duration_remaining;
    int64_t                       scte35_last_pts_diff;

    void                          *fillet_msg_pool;
    void                          *frame_msg_pool;
    void                          *compressed_video_pool;
    void                          *compressed_audio_pool;
    void                          *raw_video_pool;
    void                          *raw_audio_pool;

#if defined(ENABLE_TRANSCODE)
    preparevideo_internal_struct  *preparevideo;
    transvideo_internal_struct    *transvideo;
    encodevideo_internal_struct   *encodevideo;
    scalevideo_internal_struct    *scalevideo;
    transaudio_internal_struct    *transaudio[MAX_AUDIO_SOURCES];
    encodeaudio_internal_struct   *encodeaudio[MAX_AUDIO_SOURCES];
#endif // ENABLE_TRANSCODE

} fillet_app_struct;

#endif // _FILLET_H_
