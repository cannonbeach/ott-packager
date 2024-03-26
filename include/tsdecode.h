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

#if !defined(_TSDECODE_H_)
#define _TSDECODE_H_

#define RECEIVE_TIMEOUT      1000
#define MAX_PMT_PIDS         256
#define MAX_ACTUAL_PIDS      65535
#define MAX_PIDS             1024
#define MAX_DATA_STREAMS     16384
#define MAX_BUFFER_SIZE      4096*1024
#define MAX_TABLE_SIZE       1024
#define MAX_ERROR_SIZE       1024
#define MAX_STREAMS          64

#define STREAM_TYPE_UNKNOWN  0x00
#define STREAM_TYPE_MPEG2    0x01
#define STREAM_TYPE_H264     0x02
#define STREAM_TYPE_HEVC     0x03
#define STREAM_TYPE_AC3      0x04
#define STREAM_TYPE_EAC3     0x05
#define STREAM_TYPE_AAC      0x06
#define STREAM_TYPE_MPEG     0x07
#define STREAM_TYPE_SCTE35   0x08
#define STREAM_TYPE_AV1      0x09
#define STREAM_TYPE_PASS     0xff

#define TABLE_ID_TVCT        200
#define TABLE_ID_CVCT        201
#define TABLE_ID_STT         205
#define TABLE_ID_CAT         1
#define TABLE_ID_MGT         199

#define PMT_DESCRIPTOR_PRIVATE1        0xA3
#define PMT_DESCRIPTOR_PRIVATE2        0x87
#define PMT_DESCRIPTOR_MAX_BITRATE     0x0E
#define PMT_DESCRIPTOR_SYSTEM_CLOCK    0x0B
#define PMT_DESCRIPTOR_SMOOTH_BUFFER   0x10
#define PMT_DESCRIPTOR_REGISTRATION    0x05
#define PMT_DESCRIPTOR_TELETEXT        0x56
#define PMT_DESCRIPTOR_ALIGNMENT       0x06
#define PMT_DESCRIPTOR_STD             0x11
#define PMT_DESCRIPTOR_MUX_BUFFER      0x0C
#define PMT_DESCRIPTOR_CA              0x09
#define PMT_DESCRIPTOR_AC3             0x6A

#define STREAM_DESCRIPTOR_IDENTIFIER   0x52
#define STREAM_DESCRIPTOR_VIDEO        0x02
#define STREAM_DESCRIPTOR_ALIGNMENT    0x06
#define STREAM_DESCRIPTOR_MAX_BITRATE  0x0e
#define STREAM_DESCRIPTOR_AVC          0x28
#define STREAM_DESCRIPTOR_SUBTITLE1    0x59
#define STREAM_DESCRIPTOR_SUBTITLE2    0x56
#define STREAM_DESCRIPTOR_EAC3         0x7a
#define STREAM_DESCRIPTOR_STD          0x11
#define STREAM_DESCRIPTOR_SMOOTH       0x10
#define STREAM_DESCRIPTOR_CAPTION      0x86
#define STREAM_DESCRIPTOR_REGISTRATION 0x05
#define STREAM_DESCRIPTOR_AC3          0x81
#define STREAM_DESCRIPTOR_LANGUAGE     0x0a
#define STREAM_DESCRIPTOR_APPLICATION  0x6f
#define STREAM_DESCRIPTOR_MPEGAUDIO    0x03
#define STREAM_DESCRIPTOR_AC3_2        0x6a

typedef struct _scte35_data_struct_ {
    int             splice_command_type;
    int64_t         splice_event_id;
    int64_t         pts_time;
    int64_t         pts_duration;
    int64_t         pts_adjustment;
    int             auto_return;
    int             splice_immediate;
    int             program_id;
    int             cancel;
    int             out_of_network_indicator;
} scte35_data_struct;

typedef struct _packet_table_struct_ {
     int            pid;
     int64_t        input_packets;
     int            input_percent;
     int            valid;
     int64_t        report_count;
     struct timeval last_seen;
} packet_table_struct;

typedef struct _error_struct_ {
     int64_t        packet_number;
     int            pid;
     char           error_message[MAX_ERROR_SIZE];
} error_struct;

typedef struct _data_engine_struct_ {
     int            actual_data_size;
     int            wanted_data_size;
     int            data_index;
     unsigned char  *buffer;
     int64_t        pts;
     int64_t        dts;
     int            last_cc;
     int            current_cc;
     void           *context;
     int32_t        flags;
     int64_t        corruption_count;
     int            pes_aligned;
     int            current_bitrate;
     int            current_framerate;
     int            width;
     int            height;
     int            aspect_ratio;
     int            profile_level_id;
     int            seqtype;
     int            chromatype;
     int64_t        video_frame_count;
     struct timeval start_data_time;
     struct timeval end_data_time;
} data_engine_struct;

typedef struct _tvct_table_struct_ {
     unsigned char tvct_data[MAX_TABLE_SIZE];
     int tvct_data_size;
     int previous_table_id;

     int64_t max_tvct_time;
     int64_t min_tvct_time;
     int64_t avg_tvct_time;
     struct timeval start_tvct_time;
     struct timeval end_tvct_time;
} tvct_table_struct;

typedef struct _lang_struct_ {
     char   lang_tag[4];
} lang_struct;

typedef struct _pmt_table_struct_ {
     unsigned char pmt_data[MAX_TABLE_SIZE];
     int pmt_data_size;

     int pmt_pid;
     int pmt_program_number;
     int pmt_version;
     int pmt_valid;
     int current_pmt_section;
     int previous_pmt_section;
     int pcr_pid;
     int program_info_length;
     int audio_stream_count;
     int scte35_stream_count;

     int descriptor_id[MAX_STREAMS];
     int descriptor_size[MAX_STREAMS];
     int descriptor_data[MAX_STREAMS][MAX_TABLE_SIZE];
     int descriptor_count;

     int stream_pid[MAX_STREAMS];
     int stream_type[MAX_STREAMS];
     int decoded_stream_type[MAX_STREAMS];
     lang_struct decoded_language_tag[MAX_STREAMS];
     int stream_data[MAX_STREAMS][MAX_TABLE_SIZE];
     int stream_count;

     int64_t first_pts[MAX_STREAMS];
     int64_t first_dts[MAX_STREAMS];
     int64_t last_pts[MAX_STREAMS];
     int64_t last_dts[MAX_STREAMS];

     int audio_stream_index[MAX_STREAMS];

     int64_t max_pmt_time;
     int64_t min_pmt_time;
     int64_t avg_pmt_time;
     struct timeval start_pmt_time;
     struct timeval end_pmt_time;

     data_engine_struct data_engine[MAX_DATA_STREAMS];
} pmt_table_struct;

typedef struct _pat_table_struct_ {
     unsigned char pat_data[MAX_TABLE_SIZE];
     int pat_data_size;

     int pat_version;
     int pat_valid;
     int current_pat_section;
     int previous_pat_section;

     struct timeval start_pat_time;
     struct timeval end_pat_time;
     int64_t max_pat_time;
     int64_t min_pat_time;
     int64_t avg_pat_time;

     int pmt_table_entries;
     int pmt_table_pid[MAX_PMT_PIDS];
} pat_table_struct;

typedef struct _transport_data_struct_ {
     pat_table_struct master_pat_table;
     pmt_table_struct master_pmt_table[MAX_PMT_PIDS];
     packet_table_struct master_packet_table[MAX_PIDS];

     int64_t received_ts_packets;
     struct timeval pid_start_time;
     struct timeval pid_stop_time;
     int64_t initial_pcr_base[MAX_ACTUAL_PIDS];
     int64_t initial_pcr_ext;
     struct timeval pcr_start_time;
     struct timeval pcr_stop_time;
     struct timeval pcr_update_start_time;
     int tvct_decoded;
     int tvct_version[MAX_PMT_PIDS];
     unsigned long last_tvct_crc[MAX_PMT_PIDS];
     unsigned char tvct_data[MAX_TABLE_SIZE];
     int tvct_data_size;
     int tvct_table_acquired;
     int tvct_table_expected;
     int pmt_position;
     int pmt_data_size;
     int pmt_table_acquired;
     int pmt_table_expected;
     int pmt_pid_count;
     int pmt_pid_index[MAX_PMT_PIDS];
     int pmt_decoded[MAX_PMT_PIDS];
     int pmt_version[MAX_PMT_PIDS];
     unsigned long last_pmt_crc[MAX_PMT_PIDS];
     unsigned char pmt_data[MAX_TABLE_SIZE];
     int pat_program_count;
     int pat_version_number;
     int pat_position;
     int pat_transport_stream_id;
     int eit0_present;
     int eit1_present;
     int eit2_present;
     int eit3_present;
     int first_frame_intra;
     int source;
} transport_data_struct;

#if defined(__cplusplus)
extern "C" {
#endif // cplusplus

    void register_frame_callback(int (*cbfn)(uint8_t *sample, int sample_size, int sample_type, uint32_t sample_flags, int64_t pts, int64_t dts, int64_t last_pcr, int source, int sub_source, char *lang_tag, void *context), void *context);
    void register_message_callback(int (*cbfn)(int p1,int64_t p2,int64_t p3,int64_t p4, int64_t p5, int source, void* context), void*context);
    int decode_packets(uint8_t *transport_packet_data, int packet_count, transport_data_struct *tsdata, int stream_select);

#if defined(__cplusplus)
}
#endif // cplusplus

#endif // _TSDECODE_H_
