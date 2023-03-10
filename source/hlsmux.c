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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "fillet.h"
#include "dataqueue.h"
#include "mempool.h"
#include "tsdecode.h"
#include "crc.h"
#include "mp4core.h"
#include "hlsmux.h"
#include "webdav.h"
#include "esignal.h"

#define MAX_STREAM_NAME       256
#define MAX_TEXT_SIZE         512

#define VIDEO_PID             480
#define AUDIO_BASE_PID        481
#define PAT_PID               0
#define PMT_PID               2457

#define CODEC_H264            0x1b
#define CODEC_AAC             0x0f
#define CODEC_AC3             0x81

#define MAX_SOURCE_STREAMS    16

#define MDSIZE_AUDIO          32
#define MDSIZE_VIDEO          150

#define IS_VIDEO              1
#define IS_AUDIO              0
#define NO_SUBSTREAM          -1

#define VIDEO_OFFSET          150000
#define AUDIO_OFFSET          20000
#define OVERFLOW_PTS          8589934592   // 2^33

#define AUDIO_CLOCK           90000
#define VIDEO_CLOCK           90000

//#define DEBUG_MP4

#if defined(DEBUG_MP4)
static FILE *debug_video_mp4 = NULL;
static FILE *debug_audio_mp4 = NULL;
#endif  // DEBUG_MP4

typedef struct _source_context_struct_ {
    int64_t       start_time_video;
    double        total_video_duration;
    double        expected_video_duration;

    int64_t       start_time_audio[MAX_AUDIO_STREAMS];
    double        total_audio_duration[MAX_AUDIO_STREAMS];
    double        expected_audio_duration[MAX_AUDIO_STREAMS];

    int           video_fragment_ready[MAX_AUDIO_STREAMS];
    double        segment_lengths_video[MAX_ROLLOVER_SIZE];
    double        segment_lengths_audio[MAX_ROLLOVER_SIZE][MAX_AUDIO_STREAMS];
    int           discontinuity[MAX_ROLLOVER_SIZE];
    int64_t       splice_duration[MAX_ROLLOVER_SIZE];
    int64_t       full_time_video[MAX_ROLLOVER_SIZE];
    int64_t       full_duration_video[MAX_ROLLOVER_SIZE];
    int64_t       full_time_audio[MAX_ROLLOVER_SIZE][MAX_AUDIO_STREAMS];
    int64_t       full_duration_audio[MAX_ROLLOVER_SIZE][MAX_AUDIO_STREAMS];
    int64_t       pto_video;
    int64_t       pto_audio;
    int           source_discontinuity;
    int64_t       source_splice_duration;

    int           h264_sps_decoded;
    int           h264_profile;
    int           h264_level;

    uint8_t       h264_sps[MAX_PRIVATE_DATA_SIZE];
    int           h264_sps_size;

    uint8_t       h264_pps[MAX_PRIVATE_DATA_SIZE];
    int           h264_pps_size;

    int           hevc_sps_decoded;
    int           hevc_profile;
    int           hevc_level;

    uint8_t       hevc_sps[MAX_PRIVATE_DATA_SIZE];
    int           hevc_sps_size;

    uint8_t       hevc_pps[MAX_PRIVATE_DATA_SIZE];
    int           hevc_pps_size;

    uint8_t       hevc_vps[MAX_PRIVATE_DATA_SIZE];
    int           hevc_vps_size;

    int           width;
    int           height;
    uint8_t       midbyte;
    char          lang_tag[4];
    char          caption_text[MAX_TEXT_SIZE];
    int           caption_index;
    int64_t       text_start_time;
} source_context_struct;

static uint8_t aac_quiet_2[23] = {  0xde, 0x02, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x35, 0x36, 0x2e, 0x36, 0x30, 0x2e, 0x31, 0x30, 0x30, 0x00, 0x42, 0x20, 0x08, 0xc1, 0x18, 0x38 };
static uint8_t aac_quiet_6[36] = {  0xde, 0x02, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x35, 0x36, 0x2e, 0x36, 0x30, 0x2e, 0x31, 0x30, 0x30, 0x00, 0x02, 0x30, 0x40, 0x02, 0x11, 0x00,
                                    0x46, 0x08, 0xc0, 0x46, 0x20, 0x08, 0xc1, 0x18, 0x18, 0x46, 0x00, 0x01, 0xc0 };

typedef struct _decode_struct_ {
    uint8_t       *start;
    uint32_t       bitsize;
    int            cb;
} decode_struct;

static int quit_mux_pump_thread = 0;
static void *mux_pump_thread(void *context);

uint32_t getbit(decode_struct *d)
{
    int idx = d->cb / 8;
    int ofs = d->cb % 8 + 1;

    d->cb++;
    return (d->start[idx] >> (8 - ofs)) & 0x01;
}

uint32_t getbits(decode_struct *d, int n)
{
    int r = 0;
    int i;

    for (i = 0; i < n; i++) {
        r |= (getbit(d) << (n - i - 1));
    }
    return r;
}

uint32_t getegc(decode_struct *d)
{
    int r = 0;
    int i = 0;

    while ((getbit(d) == 0) && (i < 32)) {
        i++;
    }

    r = getbits(d,i);
    r += (1 << i) - 1;

    return r;
}

uint32_t getse(decode_struct *d)
{
    int r = getegc(d);
    if (r & 0x01) {
        r = (r+1)/2;
    } else {
        r = -(r/2);
    }
    return r;
}

static int get_hevc_sps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
{
    int i;
    uint8_t *sps_buffer = buffer;
    int sps_size = 0;
    int parsing_sps = 0;

    for (i = 0; i < buffer_size - 3; i++) {
        if (buffer[i+0] == 0x00 &&
            buffer[i+1] == 0x00 &&
            buffer[i+2] == 0x01) {
            int nal_type = (buffer[i+3] & 0x7f) >> 1;
            if (parsing_sps) {
                sps_size = buffer + i - sps_buffer;
                sdata->hevc_sps_size = sps_size;
                memcpy(sdata->hevc_sps, sps_buffer, sps_size);
                return 0;
            }
            if (nal_type == 33) {
                sps_buffer = buffer + i + 3;
                parsing_sps = 1;
            }
        }
    }
    return 0;
}

static int get_hevc_pps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
{
    int i;
    uint8_t *pps_buffer = buffer;
    int pps_size = 0;
    int parsing_pps = 0;

    for (i = 0; i < buffer_size - 3; i++) {
        if (buffer[i+0] == 0x00 &&
            buffer[i+1] == 0x00 &&
            buffer[i+2] == 0x01) {
            int nal_type = (buffer[i+3] & 0x7f) >> 1;
            if (parsing_pps) {
                pps_size = buffer + i - pps_buffer;
                sdata->hevc_pps_size = pps_size;
                memcpy(sdata->hevc_pps, pps_buffer, pps_size);
                return 0;
            }
            if (nal_type == 34) {
                pps_buffer = buffer + i + 3;
                parsing_pps = 1;
            }
        }
    }
    return 0;
}

static int get_hevc_vps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
{
    int i;
    uint8_t *vps_buffer = buffer;
    int vps_size = 0;
    int parsing_vps = 0;

    for (i = 0; i < buffer_size - 3; i++) {
        if (buffer[i+0] == 0x00 &&
            buffer[i+1] == 0x00 &&
            buffer[i+2] == 0x01) {
            int nal_type = (buffer[i+3] & 0x7f) >> 1;
            if (parsing_vps) {
                vps_size = buffer + i - vps_buffer;
                sdata->hevc_vps_size = vps_size;
                memcpy(sdata->hevc_vps, vps_buffer, vps_size);
                return 0;
            }
            if (nal_type == 32) {
                vps_buffer = buffer + i + 3;
                parsing_vps = 1;
            }
        }
    }
    return 0;
}

static int get_h264_sps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
{
    int i;
    uint8_t *sps_buffer = buffer;
    int sps_size = 0;
    int parsing_sps = 0;

    for (i = 0; i < buffer_size - 3; i++) {
        if (buffer[i+0] == 0x00 &&
            buffer[i+1] == 0x00 &&
            buffer[i+2] == 0x01) {
            int nal_type = buffer[i+3] & 0x1f;
            if (parsing_sps) {
                sps_size = buffer + i - sps_buffer;
                sdata->h264_sps_size = sps_size;
                memcpy(sdata->h264_sps, sps_buffer, sps_size);
                return 0;
            }
            if (nal_type == 7) {
                sps_buffer = buffer + i + 3;
                parsing_sps = 1;
            }
        }
    }
    return 0;
}

static int get_h264_pps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
{
    int i;
    uint8_t *pps_buffer = buffer;
    int pps_size = 0;
    int parsing_pps = 0;

    for (i = 0; i < buffer_size - 3; i++) {
        if (buffer[i+0] == 0x00 &&
            buffer[i+1] == 0x00 &&
            buffer[i+2] == 0x01) {
            int nal_type = buffer[i+3] & 0x1f;
            if (parsing_pps) {
                pps_size = buffer + i - pps_buffer;
                sdata->h264_pps_size = pps_size;
                memcpy(sdata->h264_pps, pps_buffer, pps_size);
                return 0;
            }
            if (nal_type == 8) {
                pps_buffer = buffer + i + 3;
                parsing_pps = 1;
            }
        }
    }
    return 0;
}

static int find_and_decode_h264_sps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
{
    int i;
    int m;

    for (i = 0; i < buffer_size - 3; i++) {
        if (buffer[i+0] == 0x00 &&
            buffer[i+1] == 0x00 &&
            buffer[i+2] == 0x01) {
            int nal_type = buffer[i+3] & 0x1f;

            if (nal_type == 7) {
                decode_struct d;
                int crop_left = 0;
                int crop_right = 0;
                int crop_top = 0;
                int crop_bottom = 0;
                int profile_idc;
                int level_idc;
                int width_mb1;
                int height_mb1;
                int frame_mb;
                int cropping = 0;
                int poc = 0;
                int decoded_width = 0;
                int decoded_height;
                int midbyte;

                fprintf(stderr,"STATUS: FOUND H.264 SPS - DECODING\n");

                d.start = buffer+i+4;
                d.cb = 0;

                profile_idc = getbits(&d, 8);
                fprintf(stderr,"PROFILE: %d\n", profile_idc);

                midbyte = getbits(&d, 8);

                level_idc = getbits(&d, 8);

                getegc(&d); // sps_id

                fprintf(stderr,"LEVELIDC: %d\n", level_idc);

                if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
                    profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
                    profile_idc == 86  || profile_idc == 118) {
                    int chroma_format = getegc(&d); // chroma format
                    int scaling_matrix;

                    if (chroma_format == 3) {
                        getbit(&d);  // color_transform_flag
                    }
                    getegc(&d); // luma bit depth
                    getegc(&d); // chroma bit depth
                    getbit(&d); // transform_bypass_flag
                    scaling_matrix = getbit(&d);
                    if (scaling_matrix) {
                        for (m = 0; m < 8; m++) {
                            int scaling_list = getbit(&d);
                            if (scaling_list) {
                                int list_size = (m < 6) ? 16 : 64;
                                int ls = 8;
                                int ns = 8;
                                int ds;
                                int k;
                                for (k = 0; k < list_size; k++) {
                                    if (ns) {
                                        ds = getse(&d);
                                        ns = (ls + ds + 256) % 256;
                                    }
                                    ls = (ns == 0) ? ls : ns;
                                }
                            }
                        }
                    }
                }
                getegc(&d); // max_frame_num...
                poc = getegc(&d); // poc
                if (poc == 0) {
                    getegc(&d); // max_pic_orer_cnt...
                } else if (poc == 1) {
                    int refs;

                    getbit(&d); // delta_pic_order
                    getse(&d); // offset_non_ref_pic
                    getse(&d); // offset_top_bottom_field
                    refs = getegc(&d);
                    for (m = 0; m < refs; m++) {
                        getse(&d);
                    }
                }
                getegc(&d); // ref frames
                getbit(&d); // gaps_allowed;
                width_mb1 = getegc(&d);
                height_mb1 = getegc(&d);
                frame_mb = getbit(&d);
                if (!frame_mb) {
                    getbit(&d); // mbaff?
                }
                getbit(&d); // direct_8x8
                cropping = getbit(&d); // cropping
                if (cropping) {
                    crop_left = getegc(&d);
                    crop_right = getegc(&d);
                    crop_top = getegc(&d);
                    crop_bottom = getegc(&d);
                }
                getbit(&d); // vui present

                decoded_width = ((width_mb1 + 1) * 16) - (crop_right*4) - (crop_left*4);
                decoded_height = ((2 - frame_mb) * (height_mb1 + 1) * 16) - (crop_bottom*4) - (crop_top*4);

                fprintf(stderr,"crop_bottom:%d crop_top:%d  crop_right:%d crop_left:%d  wmb:%d hmb:%d\n",
                        crop_bottom, crop_top, crop_right, crop_left,
                        width_mb1,
                        height_mb1);

                fprintf(stderr,"STREAM RESOLUTION: %d x %d\n", decoded_width, decoded_height);

                sdata->h264_sps_decoded = 1;
                sdata->h264_profile = profile_idc;
                sdata->h264_level = level_idc;
                sdata->width = decoded_width;
                sdata->height = decoded_height;
                sdata->midbyte = midbyte;

                break;
            }
        }
    }
    return 0;
}

void *hlsmux_create(fillet_app_struct *core)
{
    hlsmux_struct *hlsmux;

    hlsmux = (hlsmux_struct*)malloc(sizeof(hlsmux_struct));
    if (!hlsmux) {
        return NULL;
    }

    memset(hlsmux, 0, sizeof(hlsmux_struct));
    hlsmux->input_queue = dataqueue_create();
    core->hlsmux = hlsmux;
    pthread_create(&hlsmux->hlsmux_thread_id, NULL, mux_pump_thread, (void*)core);

    return (void*)hlsmux;
}

void hlsmux_destroy(void *hlsmux)
{
    hlsmux_struct *hlsmux1 = (hlsmux_struct*)hlsmux;
    if (hlsmux1) {
        quit_mux_pump_thread = 1;
        pthread_join(hlsmux1->hlsmux_thread_id, NULL);

        if (hlsmux1->input_queue) {
            dataqueue_destroy(hlsmux1->input_queue);
            hlsmux1->input_queue = NULL;
        }
        if (hlsmux1) {
            free(hlsmux1);
            hlsmux1 = NULL;
        }
    }
    return;
}

static void hlsmux_save_state(fillet_app_struct *core, source_context_struct *sdata)
{
    FILE *state_file;
    int num_sources = core->num_sources;
    int i;
    char state_filename[MAX_STREAM_NAME];

    snprintf(state_filename,MAX_STREAM_NAME-1,"/var/tmp/hlsmux_state_%d", core->cd->identity);

    state_file = fopen(state_filename,"wb");

    for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
        fwrite(sdata, sizeof(source_context_struct), 1, state_file);
        sdata++;
    }

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    for (i = 0; i < num_sources; i++) {
        int j;

        hlsmux_struct *hlsmux = (hlsmux_struct*)core->hlsmux;

        fwrite(&hlsmux->video[i].file_sequence_number, sizeof(int64_t), 1, state_file);
        fwrite(&hlsmux->video[i].media_sequence_number, sizeof(int64_t), 1, state_file);
        fwrite(&hlsmux->video[0].fragments_published, sizeof(int64_t), 1, state_file);  //the 0 is not a typo-need to line up if restart occurs
        fwrite(&hlsmux->video[0].last_segment_time, sizeof(int64_t), 1, state_file);
        fwrite(&hlsmux->video[0].discontinuity_adjustment, sizeof(int64_t), 1, state_file);

        for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
            fwrite(&hlsmux->audio[i][j].file_sequence_number, sizeof(int64_t), 1, state_file);
            fwrite(&hlsmux->audio[i][j].media_sequence_number, sizeof(int64_t), 1, state_file);
            fwrite(&hlsmux->video[0].fragments_published, sizeof(int64_t), 1, state_file);  //yes,video - so they match
            fwrite(&hlsmux->audio[i][j].last_segment_time, sizeof(int64_t), 1, state_file);
            fwrite(&hlsmux->audio[i][j].discontinuity_adjustment, sizeof(int64_t), 1, state_file);
        }
    }
    fwrite(&core->t_avail, sizeof(time_t), 1, state_file);
    fwrite(&core->timeset, sizeof(int), 1, state_file);

    fclose(state_file);
    return;
}

static int hlsmux_load_state(fillet_app_struct *core, source_context_struct *sdata)
{
    FILE *state_file;
    int num_sources = core->num_sources;
    int i;
    int r;
    char state_filename[MAX_STREAM_NAME];
    //int s64 = sizeof(int64_t);

    snprintf(state_filename,MAX_STREAM_NAME-1,"/var/tmp/hlsmux_state_%d", core->cd->identity);

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    state_file = fopen(state_filename,"rb");
    if (state_file) {
        for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
            r = fread(sdata, sizeof(source_context_struct), 1, state_file);
            sdata++;
        }

        for (i = 0; i < num_sources; i++) {
            hlsmux_struct *hlsmux = (hlsmux_struct*)core->hlsmux;
            int j;

            r = fread(&hlsmux->video[i].file_sequence_number, sizeof(int64_t), 1, state_file);
            r = fread(&hlsmux->video[i].media_sequence_number, sizeof(int64_t), 1, state_file);
            r = fread(&hlsmux->video[i].fragments_published, sizeof(int64_t), 1, state_file);
            r = fread(&hlsmux->video[i].last_segment_time, sizeof(int64_t), 1, state_file);
            r = fread(&hlsmux->video[i].discontinuity_adjustment, sizeof(int64_t), 1, state_file);

            for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                r = fread(&hlsmux->audio[i][j].file_sequence_number, sizeof(int64_t), 1, state_file);
                r = fread(&hlsmux->audio[i][j].media_sequence_number, sizeof(int64_t), 1, state_file);
                r = fread(&hlsmux->audio[i][j].fragments_published, sizeof(int64_t), 1, state_file);
                r = fread(&hlsmux->audio[i][j].last_segment_time, sizeof(int64_t), 1, state_file);
                r = fread(&hlsmux->audio[i][j].discontinuity_adjustment, sizeof(int64_t), 1, state_file);
            }
            /*if (r < s64*6) {
                fclose(state_file);
                return 0;
            }*/
        }
        r = fread(&core->t_avail, sizeof(time_t), 1, state_file);
        r = fread(&core->timeset, sizeof(int), 1, state_file);

        fclose(state_file);
        return 1;
    }
    return 0;
}

static int apply_pts(uint8_t *header, int64_t ts)
{
    header[0] = ((ts >> 29) & 0x0f) | 0x01;
    header[1] = (ts >> 22) & 0xff;
    header[2] = ((ts >> 14) & 0xff) | 0x01;
    header[3] = (ts >> 7) & 0xff;
    header[4] = ((ts << 1) & 0xff) | 0x01;
    return 0;
}

static int muxpatsample(fillet_app_struct *core, stream_struct *stream, uint8_t *pat)
{
    uint16_t *save16;
    uint32_t *save32;
    int crcsize = 12;
    uint32_t calculated_crc;

    if (!pat) {
        return -1;
    }

    memset(pat, 0xff, 188);

    pat[0] = 0x47;
    pat[1] = 0x40;
    pat[2] = 0x00;
    pat[3] = (stream->pat_cnt & 0x0f) | 0x10;
    stream->pat_cnt = (stream->pat_cnt + 1) & 0x0f;
    pat[4] = 0x00;
    pat[5] = 0x00;  // PAT table id

    save16 = (uint16_t*)&pat[6];
    *save16 = htons(0xb00d);
    save16 = (uint16_t*)&pat[8];
    *save16 = htons(1);    // transport stream id

    pat[10] = 0xc3;
    pat[11] = 0x00;
    pat[12] = 0x00;

    save16 = (uint16_t*)&pat[13];
    *save16 = htons(1);    // program number

    save16 = (uint16_t*)&pat[15];
    *save16 = htons(0xe000 | PMT_PID);

    save32 = (uint32_t*)&pat[17];

    calculated_crc = getcrc32(&pat[5], crcsize);
    calculated_crc = htonl(calculated_crc);

    *save32 = calculated_crc;

    return 0;
}

static int muxpmtsample(fillet_app_struct *core, stream_struct *stream, uint8_t *pmt, int pid, int codec_type)
{
    uint16_t *save16;
    uint32_t *save32;
    uint32_t calculated_crc;
    int crcsize = 17;

    if (!pmt) {
        return -1;
    }

    memset(pmt, 0xff, 188);

    pmt[0] = 0x47;
    save16 = (uint16_t*)&pmt[1];
    *save16 = htons(0x4000 | PMT_PID);  // pmt pid

    pmt[3] = stream->pmt_cnt | 0x10;
    stream->pmt_cnt = (stream->pmt_cnt + 1) & 0x0f;

    pmt[4] = 0x00;
    pmt[5] = 0x02; // PMT table id

    save16 = (uint16_t*)&pmt[6];
    *save16 = htons(0xb000 | 18);

    save16 = (uint16_t*)&pmt[8];
    *save16 = htons(1); // program number
    pmt[10] = 0xc3;
    pmt[11] = 0x00;
    pmt[12] = 0x00;

    save16 = (uint16_t*)&pmt[13];
    *save16 = htons(0xe000 | pid);  // video pid - pcr pid
    save16 = (uint16_t*)&pmt[15];
    *save16 = htons(0xf000);

    pmt[17] = codec_type;
    save16 = (uint16_t*)&pmt[18];
    *save16 = htons(0xe000 | pid);  // actual video pid
    pmt[20] = 0xf0;
    pmt[21] = 0x00;

    save32 = (uint32_t*)&pmt[22];

    calculated_crc = getcrc32(&pmt[5], crcsize);
    calculated_crc = htonl(calculated_crc);

    *save32 = calculated_crc;

    return 0;
}

static int muxvideosample(fillet_app_struct *core, stream_struct *stream, sorted_frame_struct *frame)
{
    int header_size;
    uint8_t *muxbuffer = stream->muxbuffer;
    uint8_t *pesbuffer = stream->pesbuffer;
    uint8_t *buffer = muxbuffer;
    packet_struct *packettable = stream->packettable;
    packet_struct *ptable = packettable;
    uint8_t *header = pesbuffer;
    uint8_t *muxdata;
    int video_size;
    int widx = 0;
    uint16_t video_pid = VIDEO_PID;
    uint16_t firstdata;
    uint16_t *save16;
    uint16_t firstflag;
    int s;
    int packetcount = 0;

    header[0] = 0x00;
    header[1] = 0x00;
    header[2] = 0x01;        // start code
    header[3] = 0xe0;
    header[4] = 0x00;
    header[5] = 0x00;
    header[6] = 0x85;
    if (frame->pts > 0 && frame->dts > 0 && frame->pts != frame->dts) {
        header[7] = 0xc0;    // 0xc0 (both pts+dts)
        header[8] = 10;      // 10-bytes
        apply_pts(&header[9], frame->pts);
        header[9] = header[9] & 0x0f;
        header[9] = header[9] | 0x30;
        apply_pts(&header[14], frame->dts);
        header[14] = header[14] & 0x0f;
        header[14] = header[14] | 0x10;
        header_size = 19;
    } else {
        header[7] = 0x80;    // 0x80 (only pts)
        header[8] = 5;       // 5-bytes
        apply_pts(&header[9], frame->pts);
        header[9] = header[9] & 0x0f;
        header[9] = header[9] | 0x20;
        header_size = 14;
    }

    memcpy(pesbuffer + header_size, frame->buffer, frame->buffer_size);
    muxdata = pesbuffer;

    video_size = header_size + frame->buffer_size;

    firstdata = 0x4000 | video_pid;
    firstflag = 0x10;
    if (frame->sync_frame) {
        firstflag = 0x20;
    }

    while (video_size > 0) {
        uint8_t *sp;

        widx = packetcount * 188;
        sp = buffer + widx;

        buffer[widx++] = 0x47;
        save16 = (uint16_t*)&buffer[widx];
        *save16 = htons(firstdata);
        widx += 2;
        firstdata = video_pid;  // overwrite from first time
        buffer[widx] = (stream->cnt & 0x0f) | firstflag;
        firstflag = 0x10;
        stream->prev_cnt = stream->cnt;
        stream->cnt = (stream->cnt + 1) & 0x0f;
        if (packetcount == 0 && video_size >= MDSIZE_VIDEO) {
            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 188 - MDSIZE_VIDEO - 5;
            buffer[widx] = 0x10;  // PCR flag
            if (frame->sync_frame) {
                buffer[widx] = 0x50; // RAI, PCR flag
            }
            widx++;

            widx += 6; // PCR

            // null filler
            for (s = 12; s < 188 - MDSIZE_VIDEO; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - MDSIZE_VIDEO, muxdata, MDSIZE_VIDEO);
            widx += MDSIZE_VIDEO;           // this is the 188-byte output stream
            muxdata += MDSIZE_VIDEO;        // this is the source data

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            video_size -= MDSIZE_VIDEO;
        } else if (video_size == 183) {
            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 91;
            buffer[widx++] = 0;
            for (s = 6; s < 188 - 92; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - 92, muxdata, 92);
            widx += 92;
            muxdata += 92;

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            widx = packetcount * 188;
            sp = buffer + widx;
            buffer[widx++] = 0x47;
            save16 = (uint16_t*)&buffer[widx];
            *save16 = htons(video_pid);
            widx += 2;

            buffer[widx] = (stream->cnt & 0x0f) | 0x10;
            stream->prev_cnt = stream->cnt;
            stream->cnt = (stream->cnt + 1) & 0x0f;

            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 92;
            buffer[widx++] = 0;
            for (s = 6; s < 188 - 91; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - 91, muxdata, 91);
            widx += 91;
            muxdata += 91;

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            video_size = 0;
        } else if (video_size < 184) {
            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 188 - video_size - 5;
            buffer[widx++] = 0;
            for (s = 6; s < 188 - video_size; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - video_size, muxdata, video_size);

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            muxdata += video_size;
            widx += video_size;

            video_size = 0;
        } else {
            memcpy(sp + 4, muxdata, 184);
            muxdata += 184;
            widx += 184;

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            video_size -= 184;
        }
    }

    return packetcount;
}

static int muxaudiosample(fillet_app_struct *core, stream_struct *stream, sorted_frame_struct *frame, int audio_stream)
{
    int header_size;
    uint8_t *muxbuffer = stream->muxbuffer;
    uint8_t *pesbuffer = stream->pesbuffer;
    uint8_t *buffer = muxbuffer;
    packet_struct *packettable = stream->packettable;
    packet_struct *ptable = packettable;
    uint8_t *header = pesbuffer;
    uint8_t *muxdata;
    int audio_size;
    int widx = 0;
    uint16_t audio_pid = AUDIO_BASE_PID+audio_stream;
    uint16_t firstdata;
    uint16_t *save16;
    uint16_t firstflag;
    int s;
    int packetcount = 0;

    header[0] = 0x00;
    header[1] = 0x00;
    header[2] = 0x01;
    header[3] = 192;

    save16 = (uint16_t*)&header[4];
    *save16 = htons(frame->buffer_size + 8);

    header[6] = 0x85;
    header[7] = 0x80;
    header[8] = 5;
    apply_pts(&header[9], frame->pts);
    header[9] = header[9] & 0x0f;
    header[9] = header[9] | 0x20;
    header_size = 14;

    memcpy(pesbuffer + header_size, frame->buffer, frame->buffer_size);
    muxdata = pesbuffer;

    audio_size = header_size + frame->buffer_size;

    firstdata = 0x4000 | audio_pid;
    firstflag = 0x10;
    if (frame->sync_frame) {
        firstflag = 0x20;
    }

    while (audio_size > 0) {
        uint8_t *sp;

        widx = packetcount * 188;
        sp = buffer + widx;

        buffer[widx++] = 0x47;
        save16 = (uint16_t*)&buffer[widx];
        *save16 = htons(firstdata);
        widx += 2;
        firstdata = audio_pid;  // overwrite from first time
        buffer[widx] = (stream->cnt & 0x0f) | firstflag;
        firstflag = 0x10;
        stream->prev_cnt = stream->cnt;
        stream->cnt = (stream->cnt + 1) & 0x0f;
        if (packetcount == 0 && audio_size >= MDSIZE_AUDIO) {
            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 188 - MDSIZE_AUDIO - 5;
            buffer[widx] = 0x10;  // PCR flag
            if (frame->sync_frame) {
                buffer[widx] = 0x50; // RAI, PCR flag
            }
            widx++;

            widx += 6; // PCR

            // null filler
            for (s = 12; s < 188 - MDSIZE_AUDIO; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - MDSIZE_AUDIO, muxdata, MDSIZE_AUDIO);
            widx += MDSIZE_AUDIO;           // this is the 188-byte output stream
            muxdata += MDSIZE_AUDIO;        // this is the source data

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            audio_size -= MDSIZE_AUDIO;
        } else if (audio_size == 183) {
            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 91;
            buffer[widx++] = 0;
            for (s = 6; s < 188 - 92; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - 92, muxdata, 92);
            widx += 92;
            muxdata += 92;

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            widx = packetcount * 188;
            sp = buffer + widx;
            buffer[widx++] = 0x47;
            save16 = (uint16_t*)&buffer[widx];
            *save16 = htons(audio_pid);
            widx += 2;

            buffer[widx] = (stream->cnt & 0x0f) | 0x10;
            stream->prev_cnt = stream->cnt;
            stream->cnt = (stream->cnt + 1) & 0x0f;

            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 92;
            buffer[widx++] = 0;
            for (s = 6; s < 188 - 91; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - 91, muxdata, 91);
            widx += 91;
            muxdata += 91;

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            audio_size = 0;
        } else if (audio_size < 184) {
            buffer[widx] = buffer[widx] | 0x30;
            widx++;
            buffer[widx++] = 188 - audio_size - 5;
            buffer[widx++] = 0;
            for (s = 6; s < 188 - audio_size; s++) {
                buffer[widx++] = 0xff;
            }
            memcpy(sp + 188 - audio_size, muxdata, audio_size);

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            muxdata += audio_size;
            widx += audio_size;

            audio_size = 0;
        } else {
            memcpy(sp + 4, muxdata, 184);
            muxdata += 184;
            widx += 184;

            ptable->pts = frame->pts;
            ptable->dts = frame->dts;
            ptable->sync = frame->sync_frame;
            ptable->disflag = 0;
            ptable->count = ++packetcount;
            ptable++;

            audio_size -= 184;
        }
    }

    return packetcount;
}

static int start_ts_fragment(fillet_app_struct *core, stream_struct *stream, int source, int sub_stream, int video)
{
    if (!stream->output_ts_file) {
        char stream_name[MAX_STREAM_NAME];
        if (video) {
            snprintf(stream_name, MAX_STREAM_NAME-1, "%s/video_stream%d_%ld.ts", core->cd->manifest_directory, source, stream->file_sequence_number);
        } else {
            snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio_stream%d_substream_%d_%ld.ts", core->cd->manifest_directory, source, sub_stream, stream->file_sequence_number);
        }
        stream->output_ts_file = fopen(stream_name,"w");
    }

    return 0;
}

static int start_init_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source, int video, int sub_stream)
{
    struct stat sb;

    if (!stream->output_fmp4_file) {
        char stream_name[MAX_STREAM_NAME];
        char local_dir[MAX_STREAM_NAME];

        snprintf(local_dir, MAX_STREAM_NAME-1, "%s", core->cd->manifest_directory);
        if (stat(local_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            fprintf(stderr,"STATUS: fMP4 manifest directory exists: %s\n", local_dir);
        } else {
            fprintf(stderr,"STATUS: fMP4 manifest directory does not exist: %s (CREATING)\n", local_dir);
            mkdir(local_dir, 0700);
            fprintf(stderr,"STATUS: Done creating fMP4 manifest directory\n");
        }

        if (video) {
            snprintf(local_dir, MAX_STREAM_NAME-1, "%s/video%d", core->cd->manifest_directory, source);
        } else {
            snprintf(local_dir, MAX_STREAM_NAME-1, "%s/audio%d_substream%d", core->cd->manifest_directory, source, sub_stream);
        }
        if (stat(local_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            fprintf(stderr,"STATUS: fMP4 manifest directory exists: %s\n", local_dir);
        } else {
            fprintf(stderr,"STATUS: fMP4 manifest directory does not exist: %s (CREATING)\n", local_dir);
            mkdir(local_dir, 0700);
            fprintf(stderr,"STATUS: Done creating fMP4 manifest directory\n");
        }

        snprintf(stream_name, MAX_STREAM_NAME-1, "%s/init.mp4", local_dir);
        stream->output_fmp4_file = fopen(stream_name,"w");
    }
    return 0;
}

static int end_init_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source)
{
    if (stream->output_fmp4_file) {
        fclose(stream->output_fmp4_file);
        stream->output_fmp4_file = NULL;
    }

    return 0;
}

static int start_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source, int video, int sub_stream)
{
    struct stat sb;

    if (!stream->output_fmp4_file) {
        char stream_name[MAX_STREAM_NAME];
        char local_dir[MAX_STREAM_NAME];
        if (video) {
            snprintf(local_dir, MAX_STREAM_NAME-1, "%s/video%d", core->cd->manifest_directory, source);
        } else {
            snprintf(local_dir, MAX_STREAM_NAME-1, "%s/audio%d_substream%d", core->cd->manifest_directory, source, sub_stream);
        }
        if (stat(local_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            fprintf(stderr,"STATUS: fMP4 manifest directory exists: %s\n", local_dir);
        } else {
            fprintf(stderr,"STATUS: fMP4 manifest directory does not exist: %s (CREATING)\n", local_dir);
            mkdir(local_dir, 0700);
            fprintf(stderr,"STATUS: Done creating fMP4 manifest directory\n");
        }

        snprintf(stream_name, MAX_STREAM_NAME-1, "%s/segment%ld.mp4", local_dir, stream->file_sequence_number);
        stream->output_fmp4_file = fopen(stream_name,"w");
    }
    return 0;
}

static int end_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source, int sub_stream, int video, int64_t segment_time)
{
    if (stream->output_fmp4_file) {
        fclose(stream->output_fmp4_file);
        stream->output_fmp4_file = NULL;
        {
            char stream_name[MAX_STREAM_NAME];
            char stream_name_link[MAX_STREAM_NAME];
            char local_dir[MAX_STREAM_NAME];
            if (video) {
                snprintf(local_dir, MAX_STREAM_NAME-1, "%s/video%d", core->cd->manifest_directory, source);
            } else {
                snprintf(local_dir, MAX_STREAM_NAME-1, "%s/audio%d_substream%d", core->cd->manifest_directory, source, sub_stream);
            }
            snprintf(stream_name, MAX_STREAM_NAME-1, "%s/segment%ld.mp4", local_dir, stream->file_sequence_number);
            snprintf(stream_name_link, MAX_STREAM_NAME-1, "%s/segment%ld.mp4", local_dir, segment_time); //stream->media_sequence_number);
            syslog(LOG_INFO,"HLSMUX: WRITING OUT %s (%s)\n", stream_name_link, stream_name);
            symlink(stream_name, stream_name_link);
            send_signal(core, SIGNAL_SEGMENT_WRITTEN, stream_name_link);

            /*
            if ((strlen(core->cd->cdn_server) > 0) && (strlen(core->cd->cdn_username) > 0) && (strlen(core->cd->cdn_password) > 0)) {
                dataqueue_message_struct *msg;
                //this is also done in the start_end_fragment
                //send message to webdav thread for upload
                //base url is: core->cd->cdn_server        - it doesn't need to match the manifest directory tag since it can be uploaded anywhere and we are keeping the
                //                                           manifest in the same directory as the transport files right now. this could be changed, to accommodate archival
                //                                           but for now we'll keep it simple and straightforward to setup
                msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                if (msg) {
                    snprintf(msg->smallbuf, MAX_SMALLBUF_SIZE-1, "%s", stream_name); // this is the directory on the local server which is what we want
                    msg->buffer = NULL;
                    msg->buffer_type = WEBDAV_UPLOAD;

                    dataqueue_put_front(core->webdav_queue, msg);
                } else {
                    fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                            core->session_id);
                    exit(0);
                }
            }// end checking for cdn availability
            */
        }
    }

    return 0;
}

static int start_webvtt_fragment(fillet_app_struct *core, stream_struct *stream, int source)
{
    struct stat sb;
    if (!stream->output_webvtt_file) {
        char stream_name[MAX_STREAM_NAME];
        char local_dir[MAX_STREAM_NAME];

        snprintf(local_dir, MAX_STREAM_NAME-1, "%s/webvtt%d", core->cd->manifest_directory, source);

        if (stat(local_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            fprintf(stderr,"STATUS: fMP4 manifest directory exists: %s\n", local_dir);
        } else {
            fprintf(stderr,"STATUS: fMP4 manifest directory does not exist: %s (CREATING)\n", local_dir);
            mkdir(local_dir, 0700);
            fprintf(stderr,"STATUS: Done creating fMP4 manifest directory\n");
        }

        snprintf(stream_name, MAX_STREAM_NAME-1, "%s/segment%ld.vtt", local_dir, stream->file_sequence_number);
        stream->output_webvtt_file = fopen(stream_name,"w");
    }
    return 0;
}

static int end_webvtt_fragment(fillet_app_struct *core, stream_struct *stream, int source, int64_t segment_time)
{
    if (stream->output_webvtt_file) {
        fclose(stream->output_webvtt_file);
        stream->output_webvtt_file = NULL;
        {
            char stream_name[MAX_STREAM_NAME];
            char stream_name_link[MAX_STREAM_NAME];
            char local_dir[MAX_STREAM_NAME];
            snprintf(local_dir, MAX_STREAM_NAME-1, "%s/webvtt%d", core->cd->manifest_directory, source);
            snprintf(stream_name, MAX_STREAM_NAME-1, "%s/segment%ld.vtt", local_dir, stream->file_sequence_number);
            snprintf(stream_name_link, MAX_STREAM_NAME-1, "%s/segment%ld.vtt", local_dir, segment_time);
            syslog(LOG_INFO,"HLSMUX: WRITING OUT %s (%s)\n", stream_name_link, stream_name);
            symlink(stream_name, stream_name_link);
        }
    }
    return 0;
}

static int update_ts_video_manifest(fillet_app_struct *core, stream_struct *stream, int source, int discontinuity, source_context_struct *sdata)
{
    FILE *video_manifest;
    char stream_name[MAX_STREAM_NAME];
    int i;
    int64_t starting_file_sequence_number;
    int64_t starting_media_sequence_number;

    starting_file_sequence_number = stream->file_sequence_number - core->cd->window_size;
    if (starting_file_sequence_number < 0) {
        starting_file_sequence_number += core->cd->rollover_size;
    }
    starting_media_sequence_number = stream->media_sequence_number - core->cd->window_size;

    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/video%d.m3u8", core->cd->manifest_directory, source);
    video_manifest = fopen(stream_name,"w");

    if (!video_manifest) {
        fprintf(stderr,"SESSION:%d (MAIN) ERROR: UNABLE TO WRITE VIDEO MANIFEST TO %s! UNRECOVERABLE ERROR!!!\n",
                core->session_id,
                stream_name);
        exit(0);
    }

    fprintf(video_manifest,"#EXTM3U\n");
    fprintf(video_manifest,"#EXT-X-VERSION:3\n");
    fprintf(video_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(video_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);

    for (i = 0; i < core->cd->window_size; i++) {
        int64_t next_sequence_number;

        next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;
        if (sdata->discontinuity[next_sequence_number] == 1) {
            fprintf(video_manifest,"#EXT-X-DISCONTINUITY\n");
        } else if (sdata->discontinuity[next_sequence_number] == 2) {
            fprintf(video_manifest,"#EXT-X-CUE-OUT:%ld\n", sdata->splice_duration[next_sequence_number]);
            fprintf(video_manifest,"#EXT-X-DISCONTINUITY\n");
        } else if (sdata->discontinuity[next_sequence_number] == 3) {
            fprintf(video_manifest,"#EXT-X-DISCONTINUITY\n");
            fprintf(video_manifest,"#EXT-X-CUE-IN\n");
        }

        fprintf(video_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths_video[next_sequence_number]);
        fprintf(video_manifest,"video_stream%d_%ld.ts\n", source, next_sequence_number);
    }

    fclose(video_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, stream_name);

    if ((strlen(core->cd->cdn_server) > 0) && (strlen(core->cd->cdn_username) > 0) && (strlen(core->cd->cdn_password) > 0)) {
        dataqueue_message_struct *msg;
        msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
        if (msg) {
            snprintf(msg->smallbuf, MAX_SMALLBUF_SIZE-1, "%s", stream_name); // this is the directory on the local server which is what we want
            msg->buffer = NULL;
            msg->buffer_type = WEBDAV_UPLOAD;
            dataqueue_put_front(core->webdav_queue, msg);
        } else {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }
    }// end checking for cdn availability

    return 0;
}

static int update_ts_audio_manifest(fillet_app_struct *core, stream_struct *stream, int source, int sub_stream, int discontinuity, source_context_struct *sdata)
{
    FILE *audio_manifest;
    char stream_name[MAX_STREAM_NAME];
    int i;
    int64_t starting_file_sequence_number;
    int64_t starting_media_sequence_number;

    starting_file_sequence_number = stream->file_sequence_number - core->cd->window_size;
    if (starting_file_sequence_number < 0) {
        starting_file_sequence_number += core->cd->rollover_size;
    }
    starting_media_sequence_number = stream->media_sequence_number - core->cd->window_size;

    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio%d_substream%d.m3u8", core->cd->manifest_directory, source, sub_stream);
    audio_manifest = fopen(stream_name,"w");

    fprintf(audio_manifest,"#EXTM3U\n");
    fprintf(audio_manifest,"#EXT-X-VERSION:3\n");
    fprintf(audio_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(audio_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);

    for (i = 0; i < core->cd->window_size; i++) {
        int64_t next_sequence_number;
        next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;

        if (sdata->discontinuity[next_sequence_number] == 1) {
            fprintf(audio_manifest,"#EXT-X-DISCONTINUITY\n");
        } else if (sdata->discontinuity[next_sequence_number] == 2) {
            fprintf(audio_manifest,"#EXT-X-CUE-OUT:%ld\n", sdata->splice_duration[next_sequence_number]);
            fprintf(audio_manifest,"#EXT-X-DISCONTINUITY\n");
        } else if (sdata->discontinuity[next_sequence_number] == 3) {
            fprintf(audio_manifest,"#EXT-X-DISCONTINUITY\n");
            fprintf(audio_manifest,"#EXT-X-CUE-IN\n");
        }

        fprintf(audio_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths_audio[next_sequence_number][sub_stream]);
        fprintf(audio_manifest,"audio_stream%d_substream_%d_%ld.ts\n", source, sub_stream, next_sequence_number);
    }

    fclose(audio_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, stream_name);

    if ((strlen(core->cd->cdn_server) > 0) && (strlen(core->cd->cdn_username) > 0) && (strlen(core->cd->cdn_password) > 0)) {
        dataqueue_message_struct *msg;
        msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
        if (msg) {
            snprintf(msg->smallbuf, MAX_SMALLBUF_SIZE-1, "%s", stream_name); // this is the directory on the local server which is what we want
            msg->buffer = NULL;
            msg->buffer_type = WEBDAV_UPLOAD;
            dataqueue_put_front(core->webdav_queue, msg);
        } else {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }
    }// end checking for cdn availability

    return 0;
}

static int write_ts_master_manifest(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    source_context_struct *lsdata;
    source_context_struct *origsdata;
    int i;
    int j;
    int num_sources = core->num_sources;
    int num_video_sources = core->active_video_sources;
    int create_dir = 0;

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    origsdata = sdata;

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
        create_dir = 0;
    } else {
        fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
        mkdir(core->cd->manifest_directory, 0700);
        fprintf(stderr,"STATUS: Done creating manifest directory\n");
        create_dir = 1;
    }

    snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/%s",core->cd->manifest_directory,core->cd->manifest_hls);

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
        fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
        return -1;
    }

    fprintf(master_manifest,"#EXTM3U\n");

    for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
        lsdata = sdata;
        if (lsdata->start_time_audio[j] != -1) {
            //audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[0].audio_stream[j];
            char yesno[4];

            // make the first audio stream the default/autoselect
            if (j == 0) {
                sprintf(yesno,"YES");
            } else {
                sprintf(yesno,"NO");
            }

            if (strlen(sdata->lang_tag) > 0) {
                fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",LANGUAGE=\"%s\",NAME=\"%s\",AUTOSELECT=%s,DEFAULT=%s,URI=\"audio0_substream%d.m3u8\"\n",
                        sdata->lang_tag,
                        sdata->lang_tag,
                        yesno, yesno,
                        j);
            } else {
                fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",LANGUAGE=\"eng\",NAME=\"eng\",AUTOSELECT=%s,DEFAULT=%s,URI=\"audio0_substream%d.m3u8\"\n",
                        yesno, yesno,
                        j);
            }
        }
        lsdata++;
    }

    for (i = 0; i < num_video_sources; i++) {
        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[i].video_stream;

        int video_bitrate;

#if defined(ENABLE_TRANSCODE)
        if (core->transcode_enabled) {
            video_bitrate = core->cd->transvideo_info[i].video_bitrate * 1000;
        } else {
            video_bitrate = vstream->video_bitrate;
        }
#else
        video_bitrate = vstream->video_bitrate;
#endif

        fprintf(master_manifest,"#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=%d,CODECS=\"avc1.%2x%02x%02x,mp4a.40.2\",RESOLUTION=%dx%d,AUDIO=\"audio\"\n",
                video_bitrate,
                sdata->h264_profile, //hex
                sdata->midbyte,
                sdata->h264_level,
                sdata->width, sdata->height);
        fprintf(master_manifest,"video%d.m3u8\n", i);
        sdata++;
    }

    /*
    sdata = origsdata;
    for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
        lsdata = sdata;
        if (lsdata->start_time_audio[j] != -1) {
            audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[0].audio_stream[j];
            int audio_bitrate;

#if defined(ENABLE_TRANSCODE)
            if (core->transcode_enabled) {
                audio_bitrate = core->cd->transaudio_info[j].audio_bitrate * 1000;
            } else {
                audio_bitrate = astream->audio_bitrate;
            }
#else
            audio_bitrate = astream->audio_bitrate;
#endif
            fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%d,CODECS=\"mp4a.40.2\",AUDIO=\"audio\"\n", audio_bitrate);
            fprintf(master_manifest,"audio0_substream%d.m3u8\n", j);
        }
        lsdata++;
    }
    */

    fclose(master_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, master_manifest_filename);

    if ((strlen(core->cd->cdn_server) > 0) && (strlen(core->cd->cdn_username) > 0) && (strlen(core->cd->cdn_password) > 0)) {
        dataqueue_message_struct *msg;

        if (create_dir) {
            //create the directory on the server via MKCOL
            //chance are if the directory doesn't exist locally
            //then it doesn't exist on the server
            //we don't want to try to recreate the directory each time though
            msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
            if (msg) {
                msg->buffer = NULL;
                msg->buffer_type = WEBDAV_CREATE; //we will create the cdn_server directory
                dataqueue_put_front(core->webdav_queue, msg);
            } else {
                fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                        core->session_id);
                exit(0);
            }
        }
        msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
        if (msg) {
            snprintf(msg->smallbuf, MAX_SMALLBUF_SIZE-1, "%s", master_manifest_filename); // this is the directory on the local server which is what we want
            msg->buffer = NULL;
            msg->buffer_type = WEBDAV_UPLOAD;
            dataqueue_put_front(core->webdav_queue, msg);
        } else {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }
    }// end checking for cdn availability

    return 0;
}

static int update_mp4_video_manifest(fillet_app_struct *core, stream_struct *stream, int source, int discontinuity, source_context_struct *sdata)
{
    FILE *video_manifest;
    char stream_name[MAX_STREAM_NAME];
    int i;
    int64_t starting_file_sequence_number;
    int64_t starting_media_sequence_number;

    starting_file_sequence_number = stream->file_sequence_number - core->cd->window_size;
    if (starting_file_sequence_number < 0) {
        starting_file_sequence_number += core->cd->rollover_size;
    }
    starting_media_sequence_number = stream->media_sequence_number - core->cd->window_size;

    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/video%dfmp4.m3u8", core->cd->manifest_directory, source);
    video_manifest = fopen(stream_name,"w");

    fprintf(video_manifest,"#EXTM3U\n");
    fprintf(video_manifest,"#EXT-X-VERSION:6\n");
    fprintf(video_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(video_manifest,"#EXT-X-INDEPENDENT-SEGMENTS\n");
    fprintf(video_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);
    fprintf(video_manifest,"#EXT-X-MAP:URI=\"video%d/init.mp4\"\n", source);

    for (i = 0; i < core->cd->window_size; i++) {
        int64_t next_sequence_number;

        next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;
        if (sdata->discontinuity[next_sequence_number]) {
            fprintf(video_manifest,"#EXT-X-DISCONTINUITY\n");
        }
        fprintf(video_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths_video[next_sequence_number]);
        fprintf(video_manifest,"video%d/segment%ld.mp4\n", source, next_sequence_number);
    }

    fclose(video_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, stream_name);

    return 0;
}

static int update_mp4_audio_manifest(fillet_app_struct *core, stream_struct *stream, int source, int sub_stream, int discontinuity, source_context_struct *sdata)
{
    FILE *audio_manifest;
    char stream_name[MAX_STREAM_NAME];
    int i;
    int64_t starting_file_sequence_number;
    int64_t starting_media_sequence_number;

    starting_file_sequence_number = stream->file_sequence_number - core->cd->window_size;
    if (starting_file_sequence_number < 0) {
        starting_file_sequence_number += core->cd->rollover_size;
    }
    starting_media_sequence_number = stream->media_sequence_number - core->cd->window_size;

    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio%d_substream%d_fmp4.m3u8", core->cd->manifest_directory, source, sub_stream);
    audio_manifest = fopen(stream_name,"w");

    fprintf(audio_manifest,"#EXTM3U\n");
    fprintf(audio_manifest,"#EXT-X-VERSION:6\n");
    fprintf(audio_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(audio_manifest,"#EXT-X-INDEPENDENT-SEGMENTS\n");
    fprintf(audio_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);
    fprintf(audio_manifest,"#EXT-X-MAP:URI=\"audio%d_substream%d/init.mp4\"\n", source, sub_stream);

    for (i = 0; i < core->cd->window_size; i++) {
        int64_t next_sequence_number;
        next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;
        if (sdata->discontinuity[next_sequence_number]) {
            fprintf(audio_manifest,"#EXT-X-DISCONTINUITY\n");
        }
        fprintf(audio_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths_audio[next_sequence_number][sub_stream]);
        fprintf(audio_manifest,"audio%d_substream%d/segment%ld.mp4\n", source, sub_stream, next_sequence_number);
    }

    fclose(audio_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, stream_name);

    return 0;
}

static int write_dash_master_manifest_youtube(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    int i;
    static struct tm tm_avail;
    time_t t_publish;
    struct tm tm_publish;
    int max_width = 0;
    int max_height = 0;
    source_context_struct *lsdata;
    stream_struct *stream = (stream_struct*)&core->hlsmux->video[0];
    int64_t starting_file_sequence_number;
    int64_t starting_media_sequence_number;
    char avail_time[MAX_STREAM_NAME];
    char publish_time[MAX_STREAM_NAME];
#define MAX_BASE64_SIZE 8192
    char init_string_base64[MAX_BASE64_SIZE];
    int num_sources = core->num_sources;

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    memset(init_string_base64, 0, sizeof(init_string_base64));

    if (!core->timeset) {
        core->timeset = 1;
        core->t_avail = time(NULL);
        gmtime_r(&core->t_avail, &tm_avail);
        strftime(avail_time,MAX_STREAM_NAME-1,"%Y-%m-%dT%H:%M:%SZ", &tm_avail);
    }
    t_publish = time(NULL);
    t_publish -= ((core->cd->window_size-1) * core->cd->segment_length);
    gmtime_r(&t_publish, &tm_publish);
    strftime(publish_time,MAX_STREAM_NAME-1,"%Y-%m-%dT%H:%M:%SZ", &tm_publish);

    starting_file_sequence_number = stream->file_sequence_number - core->cd->window_size;
    if (starting_file_sequence_number < 0) {
        starting_file_sequence_number += core->cd->rollover_size;
    }
    starting_media_sequence_number = stream->media_sequence_number - core->cd->window_size;

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
    } else {
        fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
        mkdir(core->cd->manifest_directory, 0700);
        fprintf(stderr,"STATUS: Done creating manifest directory\n");
    }

    snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/youtubedash.mpd",core->cd->manifest_directory);

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
        fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
        return -1;
    }

    lsdata = sdata;
    for (i = 0; i < num_sources; i++) {
        //video_stream_struct *vstream = (video_stream_struct*)core->source_stream[i].video_stream;

        if (lsdata->width > max_width && lsdata->height > max_height) {
            max_width = lsdata->width;
            max_height = lsdata->height;
        }
        lsdata++;
    }

    //When the MPD is updated, the value of MPD@availabilityStartTime shall be the same in the original and the updated MPD.
    //Segment availability start time = MPD@availabilityStartTime + PeriodStart + MediaSegment[i].startTime + MediaSegment[i].duration
    //urn:mpeg:dash:profile:isoff-live:2011,urn:com:dashif:dash264
    fprintf(master_manifest,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(master_manifest,"<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" type=\"dynamic\" minimumUpdatePeriod=\"PT%dS\" availabilityStartTime=\"%d-%02d-%02dT%02d:%02d:%02dZ\" minBufferTime=\"PT%dS\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011,urn:com:dashif:dash264\">\n",
            core->cd->segment_length,
            tm_avail.tm_year + 1900, tm_avail.tm_mon + 1, tm_avail.tm_mday, tm_avail.tm_hour, tm_avail.tm_min, tm_avail.tm_sec,
            core->cd->window_size * core->cd->segment_length);
    fprintf(master_manifest,"<Period id=\"1\" start=\"PT0S\">\n");
    fprintf(master_manifest,"<AdaptationSet id=\"0\" contentType=\"video\" segmentAlignment=\"true\" maxWidth=\"%d\" maxHeight=\"%d\" maxFrameRate=\"30000/1001\" par=\"16:9\" startWithSAP=\"1\">\n",
        max_width, max_height);

    lsdata = sdata;

    fprintf(master_manifest,"<AdaptationSet mimeType=\"video/mp4\" codecs=\"avc1.%02x%02x%02x,mp4a.40.2\">\n",
            lsdata->h264_profile,
            lsdata->midbyte,
            lsdata->h264_level);
    fprintf(master_manifest,"<ContentComponent contentType=\"video\" id=\"1\"/>\n");
    fprintf(master_manifest,"<ContentComponent contentType=\"audio\" id=\"2\"/>\n");
    fprintf(master_manifest,"<SegmentTemplate timescale=\"90000\" media=\"/dash_upload?cid=xxxx-xxxx-xxxx-xxxx&staging=1&copy=0&file=media$Number%09d$.mp4\" initialization=\"data:video/mp4;base64,%s\" duration=\"%d\" startNumber=\"%ld\"/>\n",
            1, // placeholder
            (char*)init_string_base64,
            core->cd->segment_length * 90000,
            starting_media_sequence_number);

    video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[0].video_stream;
    audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[0].audio_stream;
    int video_bitrate;
    int audio_bitrate;
#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        video_bitrate = core->cd->transvideo_info[0].video_bitrate * 1000;
    } else {
        video_bitrate = vstream->video_bitrate;
    }
#else
    video_bitrate = vstream->video_bitrate;
#endif
    fprintf(master_manifest,"<Representation id=\"1\" width=\"%d\" height=\"%d\" bandwidth=\"%d\">\n", lsdata->width, lsdata->height, video_bitrate);
    fprintf(master_manifest,"<SubRepresentation contentComponent=\"1\" bandwidth=\"%d\" codecs=\"avc1.%02x%02x%02x\"/>\n",
            video_bitrate,
            lsdata->h264_profile,
            lsdata->midbyte,
            lsdata->h264_level);
#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        audio_bitrate = core->cd->transaudio_info[0].audio_bitrate * 1000;
    } else {
        audio_bitrate = astream->audio_bitrate;
    }
#else
    audio_bitrate = astream->audio_bitrate;
#endif
    fprintf(master_manifest,"<SubRepresentation contentComponent=\"2\" bandwidth=\"%d\" codecs=\"mp4a.40.2\"/>\n",
            audio_bitrate);
    fprintf(master_manifest,"</Representation>\n");
    fprintf(master_manifest,"</AdaptationSet>\n");
    fprintf(master_manifest,"</Period>\n");
    fprintf(master_manifest,"</MPD>\n");

    return 0;
}

static int write_dash_master_manifest(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    int i;
    struct tm tm_avail;
    time_t t_publish;
    struct tm tm_publish;
    int max_width = 0;
    int max_height = 0;
    source_context_struct *lsdata;
    stream_struct *stream = (stream_struct*)&core->hlsmux->video[0];
    int64_t starting_file_sequence_number;
    int64_t starting_media_sequence_number;
    char avail_time[MAX_STREAM_NAME];
    char publish_time[MAX_STREAM_NAME];
    int num_sources = core->num_sources;

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    gmtime_r(&core->t_avail, &tm_avail);
    strftime(avail_time,MAX_STREAM_NAME-1,"%Y-%m-%dT%H:%M:%SZ", &tm_avail);

    t_publish = time(NULL);
    t_publish -= ((core->cd->window_size-1) * core->cd->segment_length);
    //t_publish -= ((core->cd->window_size-1) * core->cd->segment_length);
    //t_publish -= (core->cd->segment_length);
    gmtime_r(&t_publish, &tm_publish);
    strftime(publish_time,MAX_STREAM_NAME-1,"%Y-%m-%dT%H:%M:%SZ", &tm_publish);

    starting_file_sequence_number = stream->file_sequence_number - core->cd->window_size;
    if (starting_file_sequence_number < 0) {
        starting_file_sequence_number += core->cd->rollover_size;
    }
    starting_media_sequence_number = stream->media_sequence_number - core->cd->window_size;

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
    } else {
        fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
        mkdir(core->cd->manifest_directory, 0700);
        fprintf(stderr,"STATUS: Done creating manifest directory\n");
    }

    if (!core->timeset) {
        snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"/tmp/junkfile");
    } else {
        snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/%s",core->cd->manifest_directory,core->cd->manifest_dash);
    }

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
        fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
        return -1;
    }

    lsdata = sdata;
    for (i = 0; i < num_sources; i++) {
        //video_stream_struct *vstream = (video_stream_struct*)core->source_stream[i].video_stream;

        if (lsdata->width > max_width && lsdata->height > max_height) {
            max_width = lsdata->width;
            max_height = lsdata->height;
        }
        lsdata++;
    }

    //When the MPD is updated, the value of MPD@availabilityStartTime shall be the same in the original and the updated MPD.
    //Segment availability start time = MPD@availabilityStartTime + PeriodStart + MediaSegment[i].startTime + MediaSegment[i].duration
    {
        int64_t pre_sfsn = starting_file_sequence_number - 1;
        int segment;
        if ((starting_file_sequence_number-1) < 0) {
            pre_sfsn = core->cd->rollover_size-1;
        }
        for (segment = 0; segment < core->cd->window_size; segment++) {
            //int64_t pre_next_sequence_number = (pre_sfsn + segment) % core->cd->rollover_size;
            //int64_t media_time_passed = sdata->full_time_video[pre_next_sequence_number] / 90000;
            if (segment == core->cd->window_size - 1) {
                // why is it not media time vs system time
                //t_publish = t_avail + media_time_passed + (core->cd->segment_length*(core->cd->window_size-1));
                /*
                FILE *trimfile;
                int trim_count = 0;
                trimfile = fopen("/var/tmp/trim.dat","r");
                if (trimfile) {
                    char trim_data_line[32];
                    char *ptrim;
                    memset(trim_data_line,0,sizeof(trim_data_line));
                    ptrim = fgets(&trim_data_line[0], 8, trimfile);
                    fclose(trimfile);
                    trim_count = atoi(trim_data_line);
                }
                */
                //t_publish = t_avail + media_time_passed + ((core->cd->segment_length*core->cd->window_size-1)) + trim_count;//works
                t_publish = time(NULL) + (core->cd->segment_length*(core->cd->window_size-1));
                gmtime_r(&t_publish, &tm_publish);
                strftime(publish_time,MAX_STREAM_NAME-1,"%Y-%m-%dT%H:%M:%SZ", &tm_publish);
            }
        }
    }
    //http://dashif.org/guidelines/dash-if-simple

    fprintf(master_manifest,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(master_manifest,"<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" xmlns:cenc=\"urn:mpeg:cenc:2013\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" minBufferTime=\"PT5S\" type=\"dynamic\" publishTime=\"%d-%02d-%02dT%02d:%02d:%02dZ\" availabilityStartTime=\"%d-%02d-%02dT%02d:%02d:%02dZ\" minimumUpdatePeriod=\"PT5S\" timeShiftBufferDepth=\"PT%dS\">\n",
            tm_publish.tm_year + 1900, tm_publish.tm_mon + 1, tm_publish.tm_mday, tm_publish.tm_hour, tm_publish.tm_min, tm_publish.tm_sec,
            tm_avail.tm_year + 1900, tm_avail.tm_mon + 1, tm_avail.tm_mday, tm_avail.tm_hour, tm_avail.tm_min, tm_avail.tm_sec,
            core->cd->window_size * core->cd->segment_length);
    fprintf(master_manifest,"<Period id=\"0\" start=\"PT0S\">\n");

#if !defined(DISABLE_VIDEO) // disable video
#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        fprintf(master_manifest,"<AdaptationSet id=\"0\" contentType=\"video\" segmentAlignment=\"true\" maxWidth=\"%d\" maxHeight=\"%d\" par=\"%d:%d\">\n",
                max_width, max_height,
                core->decoded_source_info.decoded_aspect_num, core->decoded_source_info.decoded_aspect_den);
    } else {
        fprintf(master_manifest,"<AdaptationSet id=\"0\" contentType=\"video\" segmentAlignment=\"true\" maxWidth=\"%d\" maxHeight=\"%d\" maxFrameRate=\"30000/1001\" par=\"16:9\">\n",
                max_width, max_height);
    }
#else
    fprintf(master_manifest,"<AdaptationSet id=\"0\" contentType=\"video\" segmentAlignment=\"true\" maxWidth=\"%d\" maxHeight=\"%d\" maxFrameRate=\"30000/1001\" par=\"16:9\">\n",
            max_width, max_height);
#endif
    lsdata = sdata;
    for (i = 0; i < num_sources; i++) {
        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[i].video_stream;
        int segment;
        int video_bitrate;
        int fps_num;
        int fps_den;

        fps_num = 30000;
        fps_den = 1001;

#if defined(ENABLE_TRANSCODE)
        if (core->transcode_enabled) {
            video_bitrate = core->cd->transvideo_info[i].video_bitrate * 1000;
            fps_num = core->decoded_source_info.decoded_fps_num;
            fps_den = core->decoded_source_info.decoded_fps_den;
        } else {
            video_bitrate = vstream->video_bitrate;
        }
#else
        video_bitrate = vstream->video_bitrate;
#endif

#if defined(ENABLE_TRANSCODE)
        if (core->transcode_enabled) {
            if (core->cd->transvideo_info[i].video_codec == STREAM_TYPE_HEVC) {
                fprintf(master_manifest,"<Representation id=\"%d\" mimeType=\"video/mp4\" codecs=\"hev1.1.2.L93.B0\" width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" bandwidth=\"%d\">\n",
                        i,
                        lsdata->width, lsdata->height,
                        fps_num, fps_den,
                        video_bitrate);
            } else {
                fprintf(master_manifest,"<Representation id=\"%d\" mimeType=\"video/mp4\" codecs=\"avc1.%2x%02x%02x\" width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" bandwidth=\"%d\">\n",
                        i,
                        lsdata->h264_profile, //hex
                        lsdata->midbyte,
                        lsdata->h264_level,
                        lsdata->width, lsdata->height,
                        fps_num, fps_den,
                        video_bitrate);
            }
        } else {
            fprintf(master_manifest,"<Representation id=\"%d\" mimeType=\"video/mp4\" codecs=\"avc1.%2x%02x%02x\" width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" bandwidth=\"%d\">\n",
                    i,
                    lsdata->h264_profile, //hex
                    lsdata->midbyte,
                    lsdata->h264_level,
                    lsdata->width, lsdata->height,
                    fps_num, fps_den,
                    video_bitrate);
        }
#else

        if (lsdata->hevc_sps_size > 0 &&
            lsdata->hevc_pps_size > 0 &&
            lsdata->hevc_vps_size > 0) {
            fprintf(master_manifest,"<Representation id=\"%d\" mimeType=\"video/mp4\" codecs=\"hev1.1.2.L93.B0\" width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" bandwidth=\"%d\">\n",
                    i,
                    lsdata->width, lsdata->height,
                    fps_num, fps_den,
                    video_bitrate);
        } else {
            fprintf(master_manifest,"<Representation id=\"%d\" mimeType=\"video/mp4\" codecs=\"avc1.%2x%02x%02x\" width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" bandwidth=\"%d\">\n",
                    i,
                    lsdata->h264_profile, //hex
                    lsdata->midbyte,
                    lsdata->h264_level,
                    lsdata->width, lsdata->height,
                    fps_num, fps_den,
                    video_bitrate);
        }
#endif

        for (segment = 0; segment < core->cd->window_size; segment++) {
            int64_t next_sequence_number;
            //int64_t next_next_sequence_number;
            int64_t sfsn = starting_file_sequence_number - 1;
            if ((starting_file_sequence_number-1) < 0) {
                sfsn = core->cd->rollover_size-1;
            }

            next_sequence_number = (sfsn + segment) % core->cd->rollover_size;
            if (lsdata->discontinuity[next_sequence_number]) {
                // add discontinuity to file?
            }
            //next_next_sequence_number = (starting_file_sequence_number + segment) % core->cd->rollover_size;

            if (segment == 0 && i == 0) {
                /*
                if (lsdata->pto_video < 0) {
                    lsdata->pto_video++;
                } else if (lsdata->pto_video == 0) {
                    lsdata->pto_video = lsdata->full_time_video[next_sequence_number];
                }*/
                lsdata->pto_video = 0; // normalizing our time to 0

                if (core->timeset == 0) {
                    // reset the AST
                    core->timeset = 1;
                    fprintf(stderr,"STATUS: Resetting the availability time\n");
                    core->t_avail = time(NULL);
                    core->t_avail -= ((core->cd->window_size-1) * core->cd->segment_length);
                    //t_avail -= ((core->cd->window_size+1) * core->cd->segment_length);
                    //t_avail -= (core->cd->window_size * core->cd->segment_length);
                }
                gmtime_r(&core->t_avail, &tm_avail);
                strftime(avail_time,MAX_STREAM_NAME-1,"%Y-%m-%dT%H:%M:%SZ", &tm_avail);
            }
            lsdata->pto_video = 0; // normalizing our time to 0
            if (segment == 0) {
                fprintf(master_manifest,"<SegmentTemplate presentationTimeOffset=\"%ld\" timescale=\"%d\" initialization=\"video%d/init.mp4\" media=\"video%d/segment$Time$.mp4\">\n",
                        lsdata->pto_video, VIDEO_CLOCK, i, i);
                /*fprintf(master_manifest,"<SegmentTemplate presentationTimeOffset=\"%ld\" timescale=\"%d\" initialization=\"video%ld/init.mp4\" media=\"video%d/segment$Time$.mp4\" startNumber=\"%ld\">\n",
                  lsdata->pto_video, VIDEO_CLOCK, i, i, starting_media_sequence_number-1);*/
                fprintf(master_manifest,"<SegmentTimeline>\n");
            }

            fprintf(master_manifest,"<S t=\"%ld\" d=\"%ld\"/>\n",
                    lsdata->full_time_video[next_sequence_number],
                    lsdata->full_duration_video[next_sequence_number]);
            //lsdata->full_time_video[next_next_sequence_number] - lsdata->full_time_video[next_sequence_number]);
        }

        fprintf(master_manifest,"</SegmentTimeline>\n");
        fprintf(master_manifest,"</SegmentTemplate>\n");
        fprintf(master_manifest,"</Representation>\n");
        lsdata++;
    }
    fprintf(master_manifest,"</AdaptationSet>\n");
#endif      // disable video

#if !defined(DISABLE_AUDIO)     // disable audio
    // loop start
    int j;
    for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
        lsdata = sdata;
        if (lsdata->start_time_audio[j] != -1) {
            // FIX FIX FIX FIX FIX
            audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[0].audio_stream;//[j];
            int audio_bitrate;

            fprintf(master_manifest,"<AdaptationSet id=\"%d\" contentType=\"audio\" segmentAlignment=\"true\">\n", j+1);
#if defined(ENABLE_TRANSCODE)
            if (core->transcode_enabled) {
                audio_bitrate = core->cd->transaudio_info[j].audio_bitrate * 1000;
            } else {
                audio_bitrate = astream->audio_bitrate;
            }
#else
            audio_bitrate = astream->audio_bitrate;
#endif
            fprintf(master_manifest,"<Representation id=\"%d\" bandwidth=\"%d\" codecs=\"mp4a.40.2\" mimeType=\"audio/mp4\" audioSamplingRate=\"48000\">\n", i+j, audio_bitrate);
            fprintf(master_manifest,"<AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/>\n");

            int segment;
            for (segment = 0; segment < core->cd->window_size; segment++) {
                int64_t next_sequence_number;
                //int64_t next_next_sequence_number;
                int64_t sfsn = starting_file_sequence_number - 1;
                if ((starting_file_sequence_number-1) < 0) {
                    sfsn = core->cd->rollover_size-1;
                }

                next_sequence_number = (sfsn + segment) % core->cd->rollover_size;
                if (lsdata->discontinuity[next_sequence_number]) {
                    // add discontinuity to file?
                }
                //next_next_sequence_number = (starting_file_sequence_number + segment) % core->cd->rollover_size;

                if (segment == 0) {
                    /*if (lsdata->pto_audio < 0) {
                      lsdata->pto_audio++;
                      } else if (lsdata->pto_audio == 0) {
                      lsdata->pto_audio = lsdata->full_time_audio[next_sequence_number];
                      }*/
                    // we're actually normalizing our time to 0...
                    lsdata->pto_audio = 0;
                    fprintf(master_manifest,"<SegmentTemplate presentationTimeOffset=\"%ld\" timescale=\"%d\" initialization=\"audio0_substream%d/init.mp4\" media=\"audio0_substream%d/segment$Time$.mp4\">\n",
                            lsdata->pto_audio,
                            AUDIO_CLOCK,
                            j,
                            j);
                    /*fprintf(master_manifest,"<SegmentTemplate presentationTimeOffset=\"%ld\" timescale=\"%d\" initialization=\"audio0_substream%d/init.mp4\" media=\"audio0_substream%d/segment$Time$.mp4\" startNumber=\"%ld\">\n",
                            lsdata->pto_audio,
                            AUDIO_CLOCK,
                            j,
                            j,
                            starting_media_sequence_number-1);*/
                    fprintf(master_manifest,"<SegmentTimeline>\n");
                }

                fprintf(master_manifest,"<S t=\"%ld\" d=\"%ld\"/>\n",
                        lsdata->full_time_audio[next_sequence_number][j],
                        lsdata->full_duration_audio[next_sequence_number][j]);
                //lsdata->full_time_audio[next_next_sequence_number] - lsdata->full_time_audio[next_sequence_number]);
            }

            fprintf(master_manifest,"</SegmentTimeline>\n");
            fprintf(master_manifest,"</SegmentTemplate>\n");
            fprintf(master_manifest,"</Representation>\n");
            fprintf(master_manifest,"</AdaptationSet>\n");
        }
    } // loop end
#endif // disable audio

    fprintf(master_manifest,"</Period>\n");
    fprintf(master_manifest,"</MPD>\n");

    fclose(master_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, master_manifest_filename);

    return 0;
}

static int write_mp4_master_manifest(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    int i;
    int num_sources = core->num_sources;

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
    } else {
        fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
        mkdir(core->cd->manifest_directory, 0700);
        fprintf(stderr,"STATUS: Done creating manifest directory\n");
    }

    snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/%s",core->cd->manifest_directory,core->cd->manifest_fmp4);

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
        fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
        return -1;
    }

    // add support multiple audio substream in m3u8 manifest
    fprintf(master_manifest,"#EXTM3U\n");
    fprintf(master_manifest,"#EXT-X-VERSION:6\n");
    fprintf(master_manifest,"#EXT-X-INDEPENDENT-SEGMENTS\n");
    if (strlen(sdata->lang_tag) > 0) {
        fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"allaudio\",LANGUAGE=\"%s\",NAME=\"%s\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio%d_substream0_fmp4.m3u8\"\n",
                sdata->lang_tag,
                sdata->lang_tag,
                0);
    } else {
        fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"allaudio\",LANGUAGE=\"eng\",NAME=\"eng\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio%d_substream0_fmp4.m3u8\"\n",
                0);
    }

    for (i = 0; i < num_sources; i++) {
        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[i].video_stream;
        int video_bitrate;

#if defined(ENABLE_TRANSCODE)
        if (core->transcode_enabled) {
            video_bitrate = core->cd->transvideo_info[i].video_bitrate * 1000;
        } else {
            video_bitrate = vstream->video_bitrate;
        }
#else
        video_bitrate = vstream->video_bitrate;
#endif

#if defined(ENABLE_TRANSCODE)
        if (core->transcode_enabled) {
            if (core->cd->transvideo_info[i].video_codec == STREAM_TYPE_HEVC) {
                fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%d,CODECS=\"hev1.1.6.L63.90\",RESOLUTION=%dx%d,AUDIO=\"allaudio\"\n",
                        video_bitrate,
                        sdata->width, sdata->height);
            } else {
                fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%d,CODECS=\"avc1.%2x%02x%02x\",RESOLUTION=%dx%d,AUDIO=\"allaudio\"\n",
                        video_bitrate,
                        sdata->h264_profile, //hex
                        sdata->midbyte,
                        sdata->h264_level,
                        sdata->width, sdata->height);
            }
        } else {
            fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%d,CODECS=\"avc1.%2x%02x%02x\",RESOLUTION=%dx%d,AUDIO=\"allaudio\"\n",
                    video_bitrate,
                    sdata->h264_profile, //hex
                    sdata->midbyte,
                    sdata->h264_level,
                    sdata->width, sdata->height);
        }
#else
        fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%d,CODECS=\"avc1.%2x%02x%02x\",RESOLUTION=%dx%d,AUDIO=\"allaudio\"\n",
                video_bitrate,
                sdata->h264_profile, //hex
                sdata->midbyte,
                sdata->h264_level,
                sdata->width, sdata->height);
#endif
        fprintf(master_manifest,"video%dfmp4.m3u8\n", i);
        sdata++;
    }

    fclose(master_manifest);

    send_signal(core, SIGNAL_MANIFEST_WRITTEN, master_manifest_filename);

    return 0;
}


static int end_ts_fragment(fillet_app_struct *core, stream_struct *stream, int source, int sub_stream, int video)
{
    int cdn_upload = 0;
    char stream_name[MAX_STREAM_NAME];
    if (video) {
        snprintf(stream_name, MAX_STREAM_NAME-1, "%s/video_stream%d_%ld.ts", core->cd->manifest_directory, source, stream->file_sequence_number);
    } else {
        snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio_stream%d_substream_%d_%ld.ts", core->cd->manifest_directory, source, sub_stream, stream->file_sequence_number);
    }

    if (stream->output_ts_file) {
        fclose(stream->output_ts_file);
        stream->output_ts_file = NULL;
        stream->fragments_published++;
    }
    send_signal(core, SIGNAL_SEGMENT_WRITTEN, stream_name);

    if ((strlen(core->cd->cdn_server) > 0) && (strlen(core->cd->cdn_username) > 0) && (strlen(core->cd->cdn_password) > 0)) {
        dataqueue_message_struct *msg;
        //this is also done in the start_ts_fragment
        //send message to webdav thread for upload
        //base url is: core->cd->cdn_server        - it doesn't need to match the manifest directory tag since it can be uploaded anywhere and we are keeping the
        //                                           manifest in the same directory as the transport files right now. this could be changed, to accommodate archival
        //                                           but for now we'll keep it simple and straightforward to setup

        msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
        if (msg) {
            snprintf(msg->smallbuf, MAX_SMALLBUF_SIZE-1, "%s", stream_name); // this is the directory on the local server which is what we want
            msg->buffer = NULL;
            msg->buffer_type = WEBDAV_UPLOAD;

            dataqueue_put_front(core->webdav_queue, msg);
        } else {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }
    }// end checking for cdn availability

    return 0;
}

void *mux_pump_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    hlsmux_struct *hlsmux = (hlsmux_struct*)core->hlsmux;
    sorted_frame_struct *frame;
    dataqueue_message_struct *msg;
    int64_t fragment_length = core->cd->segment_length;
    int i;
    source_context_struct source_data[MAX_SOURCE_STREAMS];
    int num_sources = core->num_sources;
    int source_discontinuity = 0;
    int manifest_written = 0;
    int sub_manifest_ready = 0;
    int64_t splice_duration = 0;

    core->t_avail = 0;
    core->timeset = 0;

#if defined(ENABLE_TRANSCODE)
    if (core->transcode_enabled) {
        num_sources = core->cd->num_outputs;
    } else {
        num_sources = core->num_sources;
    }
#endif

    for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
        int j;
        source_data[i].start_time_video = -1;

        for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
            source_data[i].start_time_audio[j] = -1;
            source_data[i].total_audio_duration[j] = 0;
            source_data[i].expected_audio_duration[j] = 0;
            source_data[i].video_fragment_ready[j] = 0;
        }

        source_data[i].total_video_duration = 0;
        source_data[i].expected_video_duration = 0;
        source_data[i].source_discontinuity = 0;
        source_data[i].source_splice_duration = 0;
        memset(source_data[i].splice_duration, 0, sizeof(source_data[i].splice_duration));
        memset(source_data[i].discontinuity, 0, sizeof(source_data[i].discontinuity));
        source_data[i].h264_sps_decoded = 0;
        source_data[i].h264_sps_size = 0;
        source_data[i].h264_pps_size = 0;
        source_data[i].h264_profile = 0;
        source_data[i].h264_level = 0;
        source_data[i].hevc_sps_decoded = 0;
        source_data[i].hevc_sps_size = 0;
        source_data[i].hevc_pps_size = 0;
        source_data[i].hevc_vps_size = 0;
        source_data[i].hevc_profile = 0;
        source_data[i].hevc_level = 0;
        source_data[i].width = 0;
        source_data[i].height = 0;
        source_data[i].pto_video = -core->cd->window_size;
        source_data[i].pto_audio = -core->cd->window_size;
        source_data[i].caption_index = 0;
        source_data[i].text_start_time = 0;
        memset(source_data[i].caption_text, 0, MAX_TEXT_SIZE);
    }

    for (i = 0; i < MAX_VIDEO_SOURCES; i++) {
        int j;

        hlsmux->video[i].muxbuffer = (uint8_t*)malloc(MAX_VIDEO_MUX_BUFFER);
        hlsmux->video[i].pesbuffer = (uint8_t*)malloc(MAX_VIDEO_PES_BUFFER);
        hlsmux->video[i].packettable = (packet_struct*)malloc(sizeof(packet_struct)*(MAX_VIDEO_MUX_BUFFER/188));
        hlsmux->video[i].packet_count = 0;
        hlsmux->video[i].output_ts_file = NULL;
        hlsmux->video[i].output_fmp4_file = NULL;
        hlsmux->video[i].output_webvtt_file = NULL;
        hlsmux->video[i].file_sequence_number = 0;
        hlsmux->video[i].media_sequence_number = 0;
        hlsmux->video[i].fragments_published = 0;
        hlsmux->video[i].discontinuity_adjustment = 0;
        hlsmux->video[i].last_segment_time = 0;
        hlsmux->video[i].fmp4 = NULL;
        if (i == 0) {
            hlsmux->video[i].textbuffer = (char*)malloc(MAX_TEXT_BUFFER);
            memset(hlsmux->video[i].textbuffer, 0, MAX_TEXT_BUFFER);
            snprintf(hlsmux->video[i].textbuffer, MAX_TEXT_BUFFER-1, "WEBVTT\n\n");
        } else {
            hlsmux->video[i].textbuffer = NULL;
        }

        for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
            hlsmux->audio[i][j].muxbuffer = (uint8_t*)malloc(MAX_VIDEO_MUX_BUFFER);
            hlsmux->audio[i][j].pesbuffer = (uint8_t*)malloc(MAX_VIDEO_PES_BUFFER);
            hlsmux->audio[i][j].packettable = (packet_struct*)malloc(sizeof(packet_struct)*(MAX_VIDEO_MUX_BUFFER/188));
            hlsmux->audio[i][j].packet_count = 0;
            hlsmux->audio[i][j].output_ts_file = NULL;
            hlsmux->audio[i][j].output_fmp4_file = NULL;
            hlsmux->audio[i][j].fmp4 = NULL;
            hlsmux->audio[i][j].file_sequence_number = 0;
            hlsmux->audio[i][j].media_sequence_number = 0;
            hlsmux->audio[i][j].fragments_published = 0;
            hlsmux->audio[i][j].discontinuity_adjustment = 0;
            hlsmux->audio[i][j].last_segment_time = 0;
        }
    }

    while (1) {
        msg = dataqueue_take_back(hlsmux->input_queue);
        if (!msg) {
            if (quit_mux_pump_thread) {
                goto cleanup_mux_pump_thread;
            }
            usleep(1000);
            continue;
        }

        if (quit_mux_pump_thread) {
            frame = (sorted_frame_struct*)msg->buffer;
            if (frame) {
                if (frame->frame_type == FRAME_TYPE_VIDEO) {
                    memory_return(core->compressed_video_pool, frame->buffer);
                } else {
                    memory_return(core->compressed_audio_pool, frame->buffer);
                }
                frame->buffer = NULL;
                memory_return(core->frame_msg_pool, frame);
                frame = NULL;
            }
            memory_return(core->fillet_msg_pool, msg);
            msg = NULL;
            goto cleanup_mux_pump_thread;
        }

        frame = (sorted_frame_struct*)msg->buffer;

        if (frame->splice_point > 0 && frame->frame_type == FRAME_TYPE_VIDEO) {
            syslog(LOG_INFO,"HLSMUX: SCTE35 SPLICE POINT FOUND: %d\n", frame->splice_point);
            source_discontinuity = frame->splice_point+1;  // 2 for out and 3 for in
            splice_duration = frame->splice_duration;
        } else {
            source_discontinuity = 0;
            splice_duration = 0;
        }
        if (!source_discontinuity) {
            source_discontinuity = msg->source_discontinuity;
        }
        if (source_discontinuity) {
            int64_t first_video_file_sequence_number;
            int64_t first_video_media_sequence_number;
            int available = 0;

            syslog(LOG_INFO,"HLSMUX: SOURCE DISCONTINUITY ENCOUNTERED\n");
            available = hlsmux_load_state(core, &source_data[0]);

            for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
                int j;

                source_data[i].start_time_video = -1;

                for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                    source_data[i].start_time_audio[j] = -1;
                    source_data[i].total_audio_duration[j] = 0;
                    source_data[i].expected_audio_duration[j] = 0;
                    source_data[i].video_fragment_ready[j] = 0;
                }
                source_data[i].total_video_duration = 0;
                source_data[i].expected_video_duration = 0;
                source_data[i].h264_sps_decoded = 0;
                source_data[i].h264_sps_size = 0;
                source_data[i].h264_pps_size = 0;
                source_data[i].hevc_sps_decoded = 0;
                source_data[i].hevc_sps_size = 0;
                source_data[i].hevc_pps_size = 0;
                source_data[i].hevc_vps_size = 0;
                if (available) {
                    source_data[i].source_discontinuity = source_discontinuity;
                    source_data[i].source_splice_duration = splice_duration;
                }
            }

            first_video_file_sequence_number = hlsmux->video[0].file_sequence_number;
            first_video_media_sequence_number = hlsmux->video[0].media_sequence_number;

            fprintf(stderr,"FIRST FILE SEQ: %ld  FIRST MEDIA SEQ:%ld\n",
                    first_video_file_sequence_number,
                    first_video_media_sequence_number);

            for (i = 0; i < num_sources; i++) {
                int j;

                if (core->cd->enable_ts_output) {
                    hlsmux->video[i].packet_count = 0;
                    if (hlsmux->video[i].output_ts_file) {
                        fclose(hlsmux->video[i].output_ts_file);
                        hlsmux->video[i].output_ts_file = NULL;
                    }
                    for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                        hlsmux->audio[i][j].packet_count = 0;
                        if (hlsmux->audio[i][j].output_ts_file) {
                            fclose(hlsmux->audio[i][j].output_ts_file);
                            hlsmux->audio[i][j].output_ts_file = NULL;
                        }
                    }
                }
                if (core->cd->enable_fmp4_output) {
                    if (hlsmux->video[i].output_fmp4_file) {
                        fclose(hlsmux->video[i].output_fmp4_file);
                        hlsmux->video[i].output_fmp4_file = NULL;
                    }
                    if (i == 0) {
                        if (hlsmux->video[i].output_webvtt_file) {
                            fclose(hlsmux->video[i].output_webvtt_file);
                            hlsmux->video[i].output_webvtt_file = NULL;
                        }
                    }
                    if (hlsmux->video[i].fmp4) {
                        fmp4_file_finalize(hlsmux->video[i].fmp4);
                        hlsmux->video[i].fmp4 = NULL;
                    }
                    for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                        if (hlsmux->audio[i][j].output_fmp4_file) {
                            fclose(hlsmux->audio[i][j].output_fmp4_file);
                            hlsmux->audio[i][j].output_fmp4_file = NULL;
                        }
                        if (hlsmux->audio[i][j].fmp4) {
                            fmp4_file_finalize(hlsmux->audio[i][j].fmp4);
                            hlsmux->audio[i][j].fmp4 = NULL;
                        }
                    }
                }

                hlsmux->video[i].file_sequence_number = first_video_file_sequence_number;
                hlsmux->video[i].media_sequence_number = first_video_media_sequence_number;
                hlsmux->video[i].fmp4 = NULL;
                if (hlsmux->video[i].last_segment_time > 0) {
                    hlsmux->video[i].discontinuity_adjustment += hlsmux->video[i].last_segment_time;
                    hlsmux->video[i].last_segment_time = 0;
                }

                for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                    hlsmux->audio[i][j].file_sequence_number = first_video_file_sequence_number;
                    hlsmux->audio[i][j].media_sequence_number = first_video_media_sequence_number;
                    hlsmux->audio[i][j].fmp4 = NULL;
                    if (hlsmux->audio[i][j].last_segment_time > 0) {
                        hlsmux->audio[i][j].discontinuity_adjustment += hlsmux->audio[i][j].last_segment_time;
                        hlsmux->audio[i][j].last_segment_time = 0;
                    }
                }
            }
            source_discontinuity = 0;
            splice_duration = 0;
        }

        if (frame->frame_type == FRAME_TYPE_VIDEO && core->cd->enable_webvtt) {
            if (frame->media_type == MEDIA_TYPE_H264 && frame->source == 0) {
                // grab the sei caption message
                int sp;
                uint8_t *buffer;

                buffer = frame->buffer;
                for (sp = 0; sp < frame->buffer_size - 3; sp++) {
                    if (buffer[sp] == 0x00 &&
                        buffer[sp+1] == 0x00 &&
                        buffer[sp+2] == 0x01) {
                        int nal = buffer[sp+3] & 0x1f;
                        int sei = buffer[sp+4];
                        if (nal == 0x06 && sei == 4) {
                            if (buffer[sp+ 6] == 0xb5 && buffer[sp+ 7] == 0x00 &&
                                buffer[sp+ 8] == 0x31 && buffer[sp+ 9] == 0x47 &&
                                buffer[sp+10] == 0x41 && buffer[sp+11] == 0x39 &&
                                buffer[sp+12] == 0x34) {

                                int read_idx = 3;
                                uint8_t *cbuf = (uint8_t*)&buffer[sp+13];
                                int caption_count;
                                int current_idx;

                                caption_count = (*(cbuf+1) & 0x1f);
                                fprintf(stderr,"captions:%d\n", caption_count);

                                for (current_idx = 0; current_idx < caption_count; current_idx++) {
                                    uint8_t cc1 = *(cbuf+read_idx+1) & 0x7f;
                                    uint8_t cc2 = *(cbuf+read_idx+2) & 0x7f;

                                    int cckind = *(cbuf+read_idx) & 0x03;
                                    int isactive = *(cbuf+read_idx) & 0x04;
                                    int xds = (((cc1 & 0x70) == 0x00) & ((cc2 & 0x70) == 0x00));
                                    int tab = (((cc1 & 0x77) == 0x17) & ((cc2 & 0x7c) == 0x20));
                                    int space = (((cc1 & 0x77) == 0x11) & ((cc2 & 0x70) == 0x20));
                                    int other = (((cc1 & 0x77) == 0x11) & ((cc2 & 0x70) == 0x30));
                                    int ctrlcc = (((cc1 & 0x76) == 0x14) & ((cc2 & 0x70) == 0x20));
                                    int ctrl = ((cc1 & 0x70) == 0x10);

                                    if (!isactive) {
                                        break;
                                    }
                                    if (xds) {
                                        break;
                                    }
                                    if (tab) {
                                        read_idx += 3;
                                        continue;
                                    }
                                    if (other) {
                                        read_idx += 3;
                                        continue;
                                    }
                                    if (space && cckind == 0) {
                                        int caption_index = source_data[0].caption_index;
                                        if (caption_index == 0) {
                                            source_data[0].text_start_time = frame->full_time;
                                        }
                                        source_data[0].caption_text[caption_index++] = ' ';
                                        source_data[0].caption_index = caption_index;
                                        read_idx += 3;
                                        continue;
                                    }
                                    if (ctrl) {
                                        if (ctrlcc && !cckind) {
                                            int cm = cc2 & 0x0f;
                                            if (cm == 0) {
                                            } else if (cm == 1) {
                                            } else if (cm == 4) {
                                            } else if (cm == 5) {
                                                //fprintf(stderr,"rollup\n");
                                                int caption_index = source_data[0].caption_index;
                                                if (caption_index == 0) {
                                                    source_data[0].text_start_time = frame->full_time;
                                                }
                                                source_data[0].caption_text[caption_index++] = '\n';
                                                source_data[0].caption_index = caption_index;
                                            } else if (cm == 6) {
                                                //fprintf(stderr,"rollup\n");
                                                int caption_index = source_data[0].caption_index;
                                                if (caption_index == 0) {
                                                    source_data[0].text_start_time = frame->full_time;
                                                }
                                                source_data[0].caption_text[caption_index++] = '\n';
                                                source_data[0].caption_index = caption_index;
                                            } else if (cm == 7) {
                                                //fprintf(stderr,"rollup\n");
                                                int caption_index = source_data[0].caption_index;
                                                if (caption_index == 0) {
                                                    source_data[0].text_start_time = frame->full_time;
                                                }
                                                source_data[0].caption_text[caption_index++] = '\n';
                                                source_data[0].caption_index = caption_index;
                                            } else if (cm == 13 || cm == 15) {
                                                char temp_text[MAX_TEXT_SIZE];
                                                if (cm == 13) {
                                                    fprintf(stderr,"crlf\n");
                                                } else if (cm == 15) {
                                                    fprintf(stderr,"end!\n");
                                                }
                                                fprintf(stderr,"\n\n\ncaption: %s\n\n\n",
                                                        source_data[0].caption_text);
                                                syslog(LOG_INFO,"CAPTION:%s\n",
                                                       source_data[0].caption_text);

                                                double seconds_at_start = ((double)source_data[0].text_start_time - source_data[0].start_time_video) / (double)VIDEO_CLOCK;
                                                double seconds_at_end = ((double)frame->full_time - (double)source_data[0].start_time_video) / (double)VIDEO_CLOCK;
                                                int seconds_end;
                                                int seconds_start;

                                                seconds_end = (int)seconds_at_end % 60;
                                                seconds_start = (int)seconds_at_start % 60;
                                                if (seconds_end < 1) {
                                                    seconds_end = 1;
                                                }
                                                if (seconds_start < 0) {
                                                    seconds_start = 0;
                                                }

                                                snprintf(temp_text, MAX_TEXT_SIZE-1, "00:00:00:%02d.000 --> 00:00:00:%02d.000\n",
                                                         seconds_start,
                                                         seconds_end);
                                                strncat(hlsmux->video[0].textbuffer,
                                                        temp_text,
                                                        MAX_TEXT_BUFFER-1);

                                                snprintf(temp_text, MAX_TEXT_SIZE-1, "%s\n\n", source_data[0].caption_text);
                                                strncat(hlsmux->video[0].textbuffer,
                                                        temp_text,
                                                        MAX_TEXT_BUFFER-1);

                                                memset(source_data[0].caption_text,0,MAX_TEXT_SIZE);
                                                source_data[0].caption_index = 0;
                                            } else {
                                                syslog(LOG_INFO,"unhandled caption control code: %d 0x%d\n",
                                                       cm, cm);
                                            }
                                        }
                                        read_idx += 3;
                                        continue;
                                    }
                                    if (cckind == 0) { // field=0
                                        /*fprintf(stderr,"(caption:%d) chars: %c %c (0x%x 0x%x)   idx:%d  type:%d  active:%d\n",
                                                current_idx, cc1, cc2,
                                                cc1, cc2,
                                                source_data[0].caption_index,
                                                cckind,
                                                isactive);*/
                                        if (cc1 != 0x00 && cc2 != 0x00) {
                                            if (cc1 > 31 && cc1 < 128 &&
                                                cc2 > 31 && cc2 < 128) {
                                                int caption_index = source_data[0].caption_index;
                                                if (caption_index == 0) {
                                                    source_data[0].text_start_time = frame->full_time;
                                                }
                                                source_data[0].caption_text[caption_index++] = cc1;
                                                source_data[0].caption_text[caption_index++] = cc2;
                                                source_data[0].caption_index = caption_index;
                                            }
                                        }
                                        fprintf(stderr,"(%d)  current:%s\n",
                                                source_data[0].caption_index,
                                                source_data[0].caption_text);
                                        read_idx += 3;
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (frame->frame_type == FRAME_TYPE_VIDEO) {
            int source = frame->source;
            int err;
            int n;
            int manifest_ready = 0;

            if (frame->media_type == MEDIA_TYPE_H264) {
                if (!source_data[source].h264_sps_decoded) {
                    find_and_decode_h264_sps(&source_data[source], frame->buffer, frame->buffer_size);
                }

                if (source_data[source].h264_sps_size == 0) {
                    get_h264_sps(&source_data[source], frame->buffer, frame->buffer_size);
                    syslog(LOG_INFO,"HLSMUX: SAVING H264 SPS: SIZE:%d\n", source_data[source].h264_sps_size);
                }
                if (source_data[source].h264_pps_size == 0) {
                    get_h264_pps(&source_data[source], frame->buffer, frame->buffer_size);
                    syslog(LOG_INFO,"HLSMUX: SAVING H264 PPS: SIZE:%d\n", source_data[source].h264_pps_size);
                }

                for (n = 0; n < num_sources; n++) {
                    if (source_data[n].h264_sps_decoded) {
                        manifest_ready++;
                    }
                }
            } else if (frame->media_type == MEDIA_TYPE_HEVC) {
                if (!source_data[source].hevc_sps_decoded) {
#if defined(ENABLE_TRANSCODE)
                    if (core->transcode_enabled) {
                        source_data[source].width = core->cd->transvideo_info[source].width;
                        source_data[source].height = core->cd->transvideo_info[source].height;
                        source_data[source].midbyte = 0x00;
                        source_data[source].hevc_sps_decoded = 1;
                        source_data[source].hevc_level = 0;
                        source_data[source].hevc_profile = 0;
                    } else {
                        //find_and_decode_hevc_sps(&source_data[source], frame->buffer, frame->buffer_size);
                        fprintf(stderr,"ERROR: NOT YET SUPPORTED!! HEVC REPACKAGING WITHOUT TRANSCODING\n");
                        exit(0);
                    }
#else
                    source_data[source].width = 1920;//source_data[source].width;
                    source_data[source].height = 1080;//source_data[source].height;
                    source_data[source].midbyte = 0x00;
                    source_data[source].hevc_sps_decoded = 1;
                    source_data[source].hevc_level = 0;
                    source_data[source].hevc_profile = 0;
#endif
                }
                fprintf(stderr,"CHECKING SPS:%d PPS:%d VPS:%d\n",
                        source_data[source].hevc_sps_size,
                        source_data[source].hevc_pps_size,
                        source_data[source].hevc_vps_size);
                if (source_data[source].hevc_sps_size == 0) {
                    get_hevc_sps(&source_data[source], frame->buffer, frame->buffer_size);
                    syslog(LOG_INFO,"HLSMUX: SAVING HEVC SPS: SIZE:%d\n", source_data[source].hevc_sps_size);
                }
                if (source_data[source].hevc_pps_size == 0) {
                    get_hevc_pps(&source_data[source], frame->buffer, frame->buffer_size);
                    syslog(LOG_INFO,"HLSMUX: SAVING HEVC PPS: SIZE:%d\n", source_data[source].hevc_pps_size);
                }
                if (source_data[source].hevc_vps_size == 0) {
                    get_hevc_vps(&source_data[source], frame->buffer, frame->buffer_size);
                    syslog(LOG_INFO,"HLSMUX: SAVING HEVC VPS: SIZE:%d\n", source_data[source].hevc_vps_size);
                }

                for (n = 0; n < num_sources; n++) {
                    if (source_data[n].hevc_sps_decoded) {
                        manifest_ready++;
                    }
                }
            } else {
                fprintf(stderr,"HLSMUX: UNSUPPORTED MEDIA TYPE\n");
            }

            if (manifest_ready == num_sources && manifest_written == 0 && sub_manifest_ready) {
                manifest_written = 1;
                syslog(LOG_INFO,"HLSMUX: WRITING OUT MASTER MANIFEST FILE\n");
                if (core->cd->enable_ts_output) {
                    err = write_ts_master_manifest(core, &source_data[0]);
                    if (err < 0) {
                        //placeholder for error handling
                        goto skip_sample;
                        //continue;
                    }
                }

                if (core->cd->enable_fmp4_output) {
                    err = write_mp4_master_manifest(core, &source_data[0]);
                    if (err < 0) {
                        //placeholder for error handling
                        goto skip_sample;
                        //continue;
                    }
                }
            }

            if (frame->sync_frame) {
                double frag_delta;

                syslog(LOG_INFO,"HLSMUX(%d): SYNC FRAME FOUND: PTS: %ld DTS:%ld\n",
                       source, frame->pts, frame->dts);

                if (source_data[source].start_time_video == -1) {
                    source_data[source].start_time_video = frame->full_time;

                    if (core->cd->enable_youtube_output) {
                        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[source].video_stream;
                        audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[source].audio_stream;//[0];
                        int video_bitrate;
                        int audio_bitrate;

                        hlsmux->video[source].fmp4 = fmp4_file_create_youtube(MEDIA_TYPE_H264,
                                                                              MEDIA_TYPE_AAC,
                                                                              VIDEO_CLOCK, // timescale for H264 video
                                                                              0x15c7, // english language code
                                                                              fragment_length);  // fragment length in seconds
                        fmp4_video_set_sps(hlsmux->video[source].fmp4,
                                           source_data[source].h264_sps,
                                           source_data[source].h264_sps_size);
                        fmp4_video_set_pps(hlsmux->video[source].fmp4,
                                           source_data[source].h264_pps,
                                           source_data[source].h264_pps_size);

#if defined(ENABLE_TRANSCODE)
                        if (core->transcode_enabled) {
                            video_bitrate = core->cd->transvideo_info[source].video_bitrate;
                            audio_bitrate = core->cd->transaudio_info[source].audio_bitrate;
                        } else {
                            video_bitrate = vstream->video_bitrate / 1000;
                            audio_bitrate = astream->audio_bitrate;
                        }
#else
                        video_bitrate = vstream->video_bitrate / 1000;
                        audio_bitrate = astream->audio_bitrate;
#endif
                        fmp4_video_track_create(hlsmux->video[source].fmp4,
                                                source_data[source].width,
                                                source_data[source].height,
                                                video_bitrate);
                        fmp4_audio_track_create(hlsmux->video[source].fmp4,  // using a single fmp4 handle for both video+audio
                                                astream->audio_channels,
                                                astream->audio_samplerate,
                                                astream->audio_object_type,
                                                audio_bitrate);
                        fmp4_output_header(hlsmux->video[source].fmp4, IS_VIDEO);

                        // convert buffer into base64 data for inclusion into MPD file
                        // write out to file for debug mode purposes
                        //fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, hlsmux->video[source].output_fmp4_file);
#if 1 // DEBUG
                        {
                            FILE *debug_init_file = NULL;
                            debug_init_file = fopen("init_youtube.mp4","wb");
                            if (debug_init_file) {
                                fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, debug_init_file);
                                fclose(debug_init_file);
                            }
                        }
#endif // #if 1

                        fmp4_file_finalize(hlsmux->video[source].fmp4);
                        hlsmux->video[source].fmp4 = NULL;
                    }

                    if (core->cd->enable_fmp4_output) {
                        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[source].video_stream;

                        if (frame->media_type == MEDIA_TYPE_H264) {
                            int video_bitrate;

                            hlsmux->video[source].fmp4 = fmp4_file_create(MEDIA_TYPE_H264,
                                                                          VIDEO_CLOCK, // timescale for H264 video
                                                                          0x15c7, // english language code
                                                                          fragment_length);  // fragment length in seconds

                            // logic needed for multiple sps/pps
                            fmp4_video_set_sps(hlsmux->video[source].fmp4,
                                               source_data[source].h264_sps,
                                               source_data[source].h264_sps_size);

                            fmp4_video_set_pps(hlsmux->video[source].fmp4,
                                               source_data[source].h264_pps,
                                               source_data[source].h264_pps_size);

#if defined(ENABLE_TRANSCODE)
                            if (core->transcode_enabled) {
                                video_bitrate = core->cd->transvideo_info[source].video_bitrate;
                            } else {
                                video_bitrate = vstream->video_bitrate / 1000;
                            }
#else
                            video_bitrate = vstream->video_bitrate / 1000;
#endif

                            fmp4_video_track_create(hlsmux->video[source].fmp4,
                                                    source_data[source].width,
                                                    source_data[source].height,
                                                    video_bitrate);
                        } else if (frame->media_type == MEDIA_TYPE_HEVC) {
                            int video_bitrate;

                            hlsmux->video[source].fmp4 = fmp4_file_create(MEDIA_TYPE_HEVC,
                                                                          VIDEO_CLOCK,       // timescale for HEVC video
                                                                          0x15c7,            // english language code
                                                                          fragment_length);  // fragment length in seconds

                            // logic needed for multiple sps/pps/vps
                            fmp4_video_set_vps(hlsmux->video[source].fmp4,
                                               source_data[source].hevc_vps,
                                               source_data[source].hevc_vps_size);

                            fmp4_video_set_sps(hlsmux->video[source].fmp4,
                                               source_data[source].hevc_sps,
                                               source_data[source].hevc_sps_size);

                            fmp4_video_set_pps(hlsmux->video[source].fmp4,
                                               source_data[source].hevc_pps,
                                               source_data[source].hevc_pps_size);

#if defined(ENABLE_TRANSCODE)
                            if (core->transcode_enabled) {
                                video_bitrate = core->cd->transvideo_info[source].video_bitrate;
                            } else {
                                video_bitrate = vstream->video_bitrate / 1000;
                            }
#else
                            video_bitrate = vstream->video_bitrate / 1000;
#endif

                            fmp4_video_track_create(hlsmux->video[source].fmp4,
                                                    source_data[source].width,
                                                    source_data[source].height,
                                                    video_bitrate);
                        } else {
                            fprintf(stderr,"HLSMUX: UNSUPPORTED MEDIA TYPE\n");
                        }

                        start_init_mp4_fragment(core, &hlsmux->video[source], source, IS_VIDEO, NO_SUBSTREAM);  // no substream =-1

                        fmp4_output_header(hlsmux->video[source].fmp4, IS_VIDEO);

                        syslog(LOG_INFO,"HLSMUX: WRITING OUT VIDEO INIT FILE - FMP4(%d): %ld\n",
                               source,
                               hlsmux->video[source].fmp4->buffer_offset);

#if defined(DEBUG_MP4)
                        if (source == 0) {
                            if (!debug_video_mp4) {
                                debug_video_mp4 = fopen("debugvideo.mp4","w");
                            }
                            if (debug_video_mp4) {
                                fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, debug_video_mp4);
                                fflush(debug_video_mp4);
                            }
                        }
#endif // DEBUG_MP4
                        fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, hlsmux->video[source].output_fmp4_file);
                        end_init_mp4_fragment(core, &hlsmux->video[source], source);

                        fmp4_file_finalize(hlsmux->video[source].fmp4);
                        hlsmux->video[source].fmp4 = NULL;
                    }
                }
                frag_delta = (frame->full_time - source_data[source].start_time_video) / (double)VIDEO_CLOCK;

                /*syslog(LOG_INFO,"HLSMUX: VIDEO(%d): TIME:%ld FRAG_DELTA:%f  TVD:%f  EVD:%f FRAGLENGTH:%ld \n",
                       source,
                       frame->full_time,
                       frag_delta,
                       source_data[source].total_video_duration,
                       source_data[source].expected_video_duration,
                       fragment_length);*/

                if (source_data[source].total_video_duration+frag_delta >= source_data[source].expected_video_duration+fragment_length) {
                    int64_t sidx_time;
                    int64_t sidx_duration;
                    int64_t segment_time;
                    int64_t duration_time;
                    int j;

                    sidx_time = 0;
                    sidx_duration = 0;
                    if (core->cd->enable_ts_output) {
                        end_ts_fragment(core, &hlsmux->video[source], source, 0, IS_VIDEO); // substream is 0 for video
                    } else {
                        hlsmux->video[source].fragments_published++;
                    }

                    if (core->video_output_time_set == 0) {
                        core->video_output_time_set = 1;
                        clock_gettime(CLOCK_MONOTONIC, &core->video_output_time);
                    }

                    segment_time = (int64_t)((double)source_data[source].total_video_duration * (double)VIDEO_CLOCK) + hlsmux->video[source].discontinuity_adjustment;
                    hlsmux->video[source].last_segment_time = segment_time - hlsmux->video[source].discontinuity_adjustment;
                    duration_time = (int64_t)((double)frag_delta * (double)VIDEO_CLOCK);
                    if (core->cd->enable_fmp4_output) {
                        if (hlsmux->video[source].fmp4) {
                            fmp4_fragment_end(hlsmux->video[source].fmp4, &sidx_time, &sidx_duration,
                                              source_data[source].total_video_duration * (double)VIDEO_CLOCK + hlsmux->video[source].discontinuity_adjustment,
                                              frag_delta * (double)VIDEO_CLOCK,
                                              hlsmux->video[source].media_sequence_number,
                                              VIDEO_FRAGMENT);

                            /*syslog(LOG_INFO,"HLSMUX: ENDING PREVIOUS fMP4 VIDEO FRAGMENT(%d): SIZE:%d TF:%d\n",
                                   source,
                                   hlsmux->video[source].fmp4->buffer_offset,
                                   hlsmux->video[source].fmp4->fragment_count);*/

#if defined(DEBUG_MP4)
                            if (source == 0) {
                                if (!debug_video_mp4) {
                                    debug_video_mp4 = fopen("debugvideo.mp4","w");
                                }
                                if (debug_video_mp4) {
                                    fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, debug_video_mp4);
                                    fflush(debug_video_mp4);
                                }
                            }
#endif // DEBUG_MP4

                            fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, hlsmux->video[source].output_fmp4_file);
                            end_mp4_fragment(core, &hlsmux->video[source], source, NO_SUBSTREAM, IS_VIDEO, segment_time);

                            if (source == 0 && core->cd->enable_webvtt) { // webvtt
                                if (strlen(hlsmux->video[0].textbuffer) > 8) {
                                    fprintf(stderr,"WEBVTT CAPTION DATA\n");
                                    fprintf(stderr,"%s", hlsmux->video[0].textbuffer);
                                }
                                fprintf(hlsmux->video[source].output_webvtt_file, "%s", hlsmux->video[0].textbuffer);
                                memset(hlsmux->video[source].textbuffer, 0, MAX_TEXT_BUFFER);
                                snprintf(hlsmux->video[source].textbuffer, MAX_TEXT_BUFFER-1, "WEBVTT\n\n");
                                end_webvtt_fragment(core, &hlsmux->video[source], source, segment_time);
                            }

                        }
                    }

                    hlsmux_save_state(core, &source_data[0]);

                    source_data[source].segment_lengths_video[hlsmux->video[source].file_sequence_number] = frag_delta;
                    source_data[source].discontinuity[hlsmux->video[source].file_sequence_number] = source_data[source].source_discontinuity;
                    source_data[source].splice_duration[hlsmux->video[source].file_sequence_number] = source_data[source].source_splice_duration;
                    source_data[source].full_time_video[hlsmux->video[source].file_sequence_number] = segment_time;
                    source_data[source].full_duration_video[hlsmux->video[source].file_sequence_number] = duration_time;
                    //source_data[source].full_time[hlsmux->video[source].file_sequence_number] = frame->full_time;

                    if (hlsmux->video[source].fragments_published > core->cd->window_size) {
                        if (core->cd->enable_ts_output) {
                            update_ts_video_manifest(core, &hlsmux->video[source], source,
                                                     source_data[source].source_discontinuity,
                                                     &source_data[source]);
                        }
                        if (core->cd->enable_fmp4_output) {
                            update_mp4_video_manifest(core, &hlsmux->video[source], source,
                                                      source_data[source].source_discontinuity,
                                                      &source_data[source]);
                            /*
                            if (source == 0 && hlsmux->video[source].fragments_published > (core->cd->window_size+1)) {
                                write_dash_master_manifest(core, &source_data[0]);
                            }
                            */
                        }
                        source_data[source].source_discontinuity = 0;
                        source_data[source].source_splice_duration = 0;
                        sub_manifest_ready = 1;
                    }

                    hlsmux->video[source].file_sequence_number = (hlsmux->video[source].file_sequence_number + 1) % core->cd->rollover_size;
                    hlsmux->video[source].media_sequence_number = (hlsmux->video[source].media_sequence_number + 1);

                    source_data[source].start_time_video = frame->full_time;

                    syslog(LOG_INFO,"HLSMUX: STARTING NEW VIDEO FRAGMENT(%d): LENGTH:%.2f  TOTAL:%.2f (NEW START:%ld)  FILESEQ:%ld MEDIASEQ:%ld PUBLISHED:%ld\n",
                           source,
                           frag_delta,
                           source_data[source].total_video_duration,
                           source_data[source].start_time_video,
                           hlsmux->video[source].file_sequence_number,
                           hlsmux->video[source].media_sequence_number,
                           hlsmux->video[source].fragments_published);

                    source_data[source].total_video_duration += frag_delta;
                    source_data[source].expected_video_duration += fragment_length;

                    for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                        source_data[source].video_fragment_ready[j] = 1;
                    }

                    if (core->cd->enable_ts_output) {
                        start_ts_fragment(core, &hlsmux->video[source], source, 0, IS_VIDEO);  // start first video fragment // 0=video substream
                    }
                    if (core->cd->enable_youtube_output) {
                        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[source].video_stream;
                        audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[source].audio_stream;
                        int video_bitrate;
                        int audio_bitrate;

                        hlsmux->video[source].fmp4 = fmp4_file_create_youtube(MEDIA_TYPE_H264,
                                                                              MEDIA_TYPE_AAC,
                                                                              VIDEO_CLOCK, // timescale for H264 video
                                                                              0x157c, // english language code
                                                                              fragment_length);  // fragment length in seconds


#if defined(ENABLE_TRANSCODE)
                        if (core->transcode_enabled) {
                            video_bitrate = core->cd->transvideo_info[source].video_bitrate;
                            audio_bitrate = core->cd->transaudio_info[source].audio_bitrate;
                        } else {
                            video_bitrate = vstream->video_bitrate / 1000;
                            audio_bitrate = astream->audio_bitrate;
                        }
#else
                        video_bitrate = vstream->video_bitrate / 1000;
                        audio_bitrate = astream->audio_bitrate;
#endif
                        fmp4_video_track_create(hlsmux->video[source].fmp4,
                                                source_data[source].width,
                                                source_data[source].height,
                                                video_bitrate);
                        fmp4_audio_track_create(hlsmux->video[source].fmp4,  // using a single fmp4 handle for both video+audio
                                                astream->audio_channels,
                                                astream->audio_samplerate,
                                                astream->audio_object_type,
                                                audio_bitrate);
                    }

                    if (core->cd->enable_fmp4_output) {
                        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[source].video_stream;

                        if (source == 0 && core->cd->enable_webvtt) {
                            start_webvtt_fragment(core, &hlsmux->video[source], source);
                        }
                        start_mp4_fragment(core, &hlsmux->video[source], source, IS_VIDEO, NO_SUBSTREAM);
                        if (hlsmux->video[source].fmp4 == NULL) {
                            if (frame->media_type == MEDIA_TYPE_H264) {
                                int video_bitrate;

                                hlsmux->video[source].fmp4 = fmp4_file_create(MEDIA_TYPE_H264,
                                                                              VIDEO_CLOCK, // timescale for H264 video
                                                                              0x157c, // english language code
                                                                              fragment_length);  // fragment length in seconds

#if defined(ENABLE_TRANSCODE)
                                if (core->transcode_enabled) {
                                    video_bitrate = core->cd->transvideo_info[source].video_bitrate;
                                } else {
                                    video_bitrate = vstream->video_bitrate / 1000;
                                }
#else
                                video_bitrate = vstream->video_bitrate / 1000;
#endif

                                fmp4_video_track_create(hlsmux->video[source].fmp4,
                                                        source_data[source].width,
                                                        source_data[source].height,
                                                        video_bitrate);
                            } else if (frame->media_type == MEDIA_TYPE_HEVC) {
                                int video_bitrate;

                                hlsmux->video[source].fmp4 = fmp4_file_create(MEDIA_TYPE_HEVC,
                                                                              VIDEO_CLOCK, // timescale for HEVC video
                                                                              0x157c, // english language code
                                                                              fragment_length);  // fragment length in seconds

#if defined(ENABLE_TRANSCODE)
                                if (core->transcode_enabled) {
                                    video_bitrate = core->cd->transvideo_info[source].video_bitrate;
                                } else {
                                    video_bitrate = vstream->video_bitrate / 1000;
                                }
#else
                                video_bitrate = vstream->video_bitrate / 1000;
#endif

                                fmp4_video_track_create(hlsmux->video[source].fmp4,
                                                        source_data[source].width,
                                                        source_data[source].height,
                                                        video_bitrate);

                            } else {
                                fprintf(stderr,"HLSMUX: UNSUPPORTED MEDIA TYPE\n");
                            }
                        }
                    }
                }
            }
            if (core->cd->enable_youtube_output) {
                if (hlsmux->video[source].fmp4) {
                    double fragment_timestamp;
                    int fragment_duration;
                    int64_t fragment_composition_time;

                    if (frame->dts > 0) {
                        //placeholder: check for overflow issue when one goes back to zero
                        fragment_composition_time = frame->pts - frame->dts;
                        fragment_timestamp = frame->dts;
                    } else {
                        fragment_composition_time = 0;
                        fragment_timestamp = frame->pts;
                    }

                    fragment_duration = frame->duration;

                    /*syslog(LOG_INFO,"HLSMUX: ADDING VIDEO FRAGMENT(%d):%d OFFSET:%d  DURATION:%ld CTS:%ld\n",
                      source,
                      hlsmux->video[source].fmp4->fragment_count,
                      hlsmux->video[source].fmp4->buffer_offset,
                      fragment_duration,
                      fragment_composition_time);*/

                    fmp4_video_fragment_add(hlsmux->video[source].fmp4,
                                            frame->buffer,
                                            frame->buffer_size,
                                            fragment_timestamp,
                                            fragment_duration,
                                            fragment_composition_time);
                }
            }
            if (core->cd->enable_fmp4_output) {
                if (hlsmux->video[source].output_fmp4_file != NULL) {
                    if (hlsmux->video[source].fmp4) {
                        double fragment_timestamp;
                        int fragment_duration;
                        int64_t fragment_composition_time;

                        if (frame->dts > 0) {
                            //placeholder: check for overflow issue when one goes back to zero
                            fragment_composition_time = frame->pts - frame->dts;
                            fragment_timestamp = frame->dts;
                        } else {
                            fragment_composition_time = 0;
                            fragment_timestamp = frame->pts;
                        }

                        fragment_duration = frame->duration;

                        /*syslog(LOG_INFO,"HLSMUX: ADDING VIDEO FRAGMENT(%d):%d OFFSET:%d  DURATION:%ld CTS:%ld\n",
                               source,
                               hlsmux->video[source].fmp4->fragment_count,
                               hlsmux->video[source].fmp4->buffer_offset,
                               fragment_duration,
                               fragment_composition_time);*/

                        fmp4_video_fragment_add(hlsmux->video[source].fmp4,
                                                frame->buffer,
                                                frame->buffer_size,
                                                fragment_timestamp,
                                                fragment_duration,
                                                fragment_composition_time);
                    }
                }
            }
            if (core->cd->enable_ts_output) {
                if (hlsmux->video[source].output_ts_file != NULL) {
                    int s;
                    uint8_t pat[188];
                    uint8_t pmt[188];
                    int64_t pc;

                    muxpatsample(core, &hlsmux->video[source], &pat[0]);
                    fwrite(pat, 1, 188, hlsmux->video[source].output_ts_file);
                    muxpmtsample(core, &hlsmux->video[source], &pmt[0], VIDEO_PID, CODEC_H264);
                    fwrite(pmt, 1, 188, hlsmux->video[source].output_ts_file);

                    pc = muxvideosample(core, &hlsmux->video[source], frame);

                    hlsmux->video[source].packet_count += pc;
                    for (s = 0; s < pc; s++) {
                        uint8_t *muxbuffer;
                        muxbuffer = hlsmux->video[source].muxbuffer;
                        if (s == 0 && (muxbuffer[5] == 0x10 || muxbuffer[5] == 0x50)) {
                            int64_t base;
                            int64_t ext;
                            int64_t full;
                            int64_t total_pc = hlsmux->video[source].packet_count;
                            int64_t offset_count;
                            int64_t timestamp;
                            int64_t timestamp_offset;

                            if (frame->dts > 0) {
                                timestamp = frame->dts;
                            } else {
                                timestamp = frame->pts;
                            }
                            timestamp_offset = timestamp - VIDEO_OFFSET;
                            if (timestamp_offset < 0) {
                                timestamp_offset += 8589934592;
                            }

                            offset_count = (int64_t)(((double)(timestamp_offset)*(double)300.0*(double)20.0/(double)216) - (double)10)/(double)188;
                            total_pc = offset_count;

                            full = (int64_t)((int64_t)total_pc * (int64_t)40608) / (int64_t)20;
                            full = full % (8589934592 * 300);
                            base = full / 300;
                            ext = full % 300;

                            muxbuffer[6] = (0xff & (base >> 25));
                            muxbuffer[7] = (0xff & (base >> 17));
                            muxbuffer[8] = (0xff & (base >> 9));
                            muxbuffer[9] = (0xff & (base >> 1));
                            muxbuffer[10] = ((0x01 & base) << 7) | 0x7e | ((0x100 & ext) >> 8);
                            muxbuffer[11] = (0xff & ext);
                        }
                        fwrite(&muxbuffer[s*188], 1, 188, hlsmux->video[source].output_ts_file);
                    }
                }
            }
        } else if (frame->frame_type == FRAME_TYPE_AUDIO) {
            double frag_delta;
            int source = frame->source;
            int sub_stream = frame->sub_stream;

            if (strlen(frame->lang_tag) == 3) {
                source_data[0].lang_tag[0] = frame->lang_tag[0];
                source_data[0].lang_tag[1] = frame->lang_tag[1];
                source_data[0].lang_tag[2] = frame->lang_tag[2];
                source_data[0].lang_tag[3] = frame->lang_tag[3];
            }

            //fprintf(stderr,"AUDIO(%d): AUDIO RECEIVED SUB:%d\n", source, sub_stream);

            if (source_data[source].start_time_audio[sub_stream] == -1) {
                int64_t start_delta;

                if (source_data[source].start_time_video == -1) {
                    fprintf(stderr,"HLSMUX: VIDEO START TIME NOT SET YET\n");
                    goto skip_sample;
                }

                source_data[source].start_time_audio[sub_stream] = frame->full_time;

                if (core->cd->enable_fmp4_output) {
                    // FIX FIX FIX FIX FIX FIX FIX !!
                    audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[source].audio_stream;//[sub_stream];
                    int fragment_duration;

                    if (frame->media_type == MEDIA_TYPE_AAC) {
                        // decode the initial parameters
                        // 0xfff - 12-bits of sync code
                        uint8_t *srcdata = frame->buffer;
                        int audio_object_type = (*(srcdata+2) & 0xC0) >> 6;
                        int sample_freq_index = (*(srcdata+2) & 0x3C) >> 2;
                        int channel_config0 = (*(srcdata+2) & 0x01) << 2;
                        int channel_config1 = (*(srcdata+3) & 0xC0) >> 6;
                        int audio_channels = channel_config0 | channel_config1;

                        audio_object_type++;

                        fprintf(stderr,"AUDIO(%d): AUDIO CHANNELS:%d\n", source, audio_channels);
                        fprintf(stderr,"AUDIO(%d): AUDIO OBJECT TYPE:%d\n", source, audio_object_type);
                        fprintf(stderr,"AUDIO(%d): SAMPLE FREQ INDEX:%d\n", source, sample_freq_index);

                        astream->audio_object_type = audio_object_type;

                        astream->audio_channels = audio_channels;
                        if (sample_freq_index == 3) {
                            astream->audio_samplerate = 48000;
                        } else if (sample_freq_index == 4) {
                            astream->audio_samplerate = 44100;
                        } else if (sample_freq_index == 5) {
                            astream->audio_samplerate = 32000;
                        } else if (sample_freq_index == 6) {
                            astream->audio_samplerate = 24000;
                        } else if (sample_freq_index == 7) {
                            astream->audio_samplerate = 22050;
                        } else {
                            fprintf(stderr,"UNSUPPORTED SAMPLERATE\n");
                        }
                    } else if (frame->media_type == MEDIA_TYPE_AC3) {
                        int pos;
                        // still needs to be finished
                        for (pos = 0; pos < frame->buffer_size; pos++) {
                            uint8_t *curaudio = frame->buffer;
                            if (curaudio[pos] == 0x0b && curaudio[pos+1] == 0x77) {  // ac3 sync code
                                uint8_t *syncdata;
                                int acmod;
                                int audiovalid = curaudio[pos+4] & 0x3f;   // todo-check out of bounds
                                if (audiovalid > 37) {
                                    continue;
                                }
                                syncdata = (uint8_t*)&curaudio[pos+5];
                                syncdata++;

                                acmod = *syncdata >> 5;
                                if (acmod == 7) {
                                    astream->audio_channels = 6;
                                } else {
                                    astream->audio_channels = 2;
                                }
                                break;
                            }
                        }
                        astream->audio_samplerate = 48000;  // todo-fixme
                    }

                    fprintf(stderr,"HLSMUX: CREATING FMP4 AUDIO: SOURCE:%d SUB_STREAM:%d AUDIOCH:%d FRAG_LENGTH:%ld\n",
                            source,
                            sub_stream,
                            astream->audio_channels,
                            fragment_length);

                    hlsmux->audio[source][sub_stream].fmp4 = fmp4_file_create(frame->media_type,
                                                                              AUDIO_CLOCK, //astream->audio_samplerate, // timescale for AAC audio (48kHz)
                                                                              0x15c7,                    // english language code
                                                                              fragment_length);          // fragment length in seconds

                    // todo-ac3 version of this is not finished
                    fmp4_audio_track_create(hlsmux->audio[source][sub_stream].fmp4,
                                            astream->audio_channels,
                                            astream->audio_samplerate,
                                            astream->audio_object_type,
                                            astream->audio_bitrate);

                    start_init_mp4_fragment(core, &hlsmux->audio[source][sub_stream], source, IS_AUDIO, sub_stream);

                    fmp4_output_header(hlsmux->audio[source][sub_stream].fmp4, IS_AUDIO);

                    syslog(LOG_INFO,"HLSMUX: WRITING OUT AUDIO INIT FILE - FMP4(%d): %ld\n",
                           source,
                           hlsmux->audio[source][sub_stream].fmp4->buffer_offset);

#if defined(DEBUG_MP4)
                    if (source == 0 && sub_stream == 0) {
                        if (!debug_audio_mp4) {
                            debug_audio_mp4 = fopen("debugaudio.mp4","w");
                        }
                        if (debug_audio_mp4) {
                            fwrite(hlsmux->audio[source][sub_stream].fmp4->buffer, 1, hlsmux->audio[source][sub_stream].fmp4->buffer_offset, debug_audio_mp4);
                            fflush(debug_audio_mp4);
                        }
                    }
#endif // DEBUG_MP4

                    fwrite(hlsmux->audio[source][sub_stream].fmp4->buffer, 1, hlsmux->audio[source][sub_stream].fmp4->buffer_offset, hlsmux->audio[source][sub_stream].output_fmp4_file);
                    end_init_mp4_fragment(core, &hlsmux->audio[source][sub_stream], source);

                    fmp4_file_finalize(hlsmux->audio[source][sub_stream].fmp4);
                    hlsmux->audio[source][sub_stream].fmp4 = NULL;

                    start_delta = (int64_t)source_data[source].start_time_video - (int64_t)source_data[source].start_time_audio[sub_stream];
                    fragment_duration = frame->duration * astream->audio_samplerate / AUDIO_CLOCK;
                    if (astream->audio_object_type == 5) { // sbr
                        fragment_duration = fragment_duration << 1;
                    }
                    syslog(LOG_INFO,"HLSMUX: START DELTA: %ld (START_VIDEO:%ld START_AUDIO:%ld)\n",
                           start_delta,
                           source_data[source].start_time_video,
                           source_data[source].start_time_audio[0]);
                    if (start_delta < 0) {
                        // add silence audio to align samples for fmp4
                        //aac_quiet_6
                        //aac_quiet_2
                        astream->audio_samples_to_add = (int)(((abs(start_delta) * astream->audio_samplerate / (double)VIDEO_CLOCK) / fragment_duration)+0.5);
                        syslog(LOG_INFO,"HLSMUX: ADDING %d SILENT SAMPLES TO MAINTAIN A/V SYNC\n", astream->audio_samples_to_add);
                    } else {
                        astream->audio_samples_to_drop = (int)(((abs(start_delta) * astream->audio_samplerate / (double)VIDEO_CLOCK) / fragment_duration)+0.5);
                        syslog(LOG_INFO,"HLSMUX: REMOVING %d SAMPLES TO MAINTAIN A/V SYNC\n", astream->audio_samples_to_drop);
                    }
                }
            }
            frag_delta = (frame->full_time - source_data[source].start_time_audio[sub_stream]) / (double)AUDIO_CLOCK;

            /*syslog(LOG_INFO,"HLSMUX: AUDIO(%d): TIME:%ld FRAG_DELTA:%f  STA:%ld TAD:%f  EAD:%f FRAGLENGTH:%ld \n",
                   source,
                   frame->full_time,
                   frag_delta,
                   source_data[source].start_time_audio[sub_stream],
                   source_data[source].total_audio_duration[sub_stream],
                   source_data[source].expected_audio_duration[sub_stream],
                   fragment_length);*/

            if (source_data[source].total_audio_duration[sub_stream]+frag_delta >= source_data[source].total_video_duration && source_data[source].video_fragment_ready[sub_stream]) {
                int64_t sidx_time = 0;
                int64_t sidx_duration = 0;
                int64_t segment_time;
                int64_t duration_time;

                if (core->cd->enable_ts_output) {
                    end_ts_fragment(core, &hlsmux->audio[source][sub_stream], source, sub_stream, IS_AUDIO);
                } else {
                    hlsmux->audio[source][sub_stream].fragments_published++;
                }

                segment_time = (int64_t)((double)source_data[source].total_audio_duration[sub_stream] * (double)AUDIO_CLOCK) + hlsmux->video[source].discontinuity_adjustment;
                hlsmux->audio[source][sub_stream].last_segment_time = segment_time - hlsmux->video[source].discontinuity_adjustment;
                duration_time = (int64_t)((double)frag_delta * (double)AUDIO_CLOCK);
                if (core->cd->enable_fmp4_output) {
                    if (hlsmux->audio[source][sub_stream].fmp4) {
                        fmp4_fragment_end(hlsmux->audio[source][sub_stream].fmp4, &sidx_time, &sidx_duration,
                                          source_data[source].total_audio_duration[sub_stream] * (double)AUDIO_CLOCK + hlsmux->video[source].discontinuity_adjustment,
                                          frag_delta * (double)AUDIO_CLOCK,
                                          hlsmux->audio[source][sub_stream].media_sequence_number,
                                          AUDIO_FRAGMENT);

                        syslog(LOG_INFO,"HLSMUX: ENDING PREVIOUS fMP4 AUDIO FRAGMENT(%d): SIZE:%ld\n",
                               source,
                               hlsmux->audio[source][sub_stream].fmp4->buffer_offset);

#if defined(DEBUG_MP4)
                        if (source == 0 && sub_stream == 0) {
                            if (!debug_audio_mp4) {
                                debug_audio_mp4 = fopen("debugaudio.mp4","w");
                            }
                            if (debug_audio_mp4) {
                                fwrite(hlsmux->audio[source][sub_stream].fmp4->buffer, 1, hlsmux->audio[source][sub_stream].fmp4->buffer_offset, debug_audio_mp4);
                                fflush(debug_audio_mp4);
                            }
                        }
#endif // DEBUG_MP4

                        fwrite(hlsmux->audio[source][sub_stream].fmp4->buffer, 1, hlsmux->audio[source][sub_stream].fmp4->buffer_offset, hlsmux->audio[source][sub_stream].output_fmp4_file);
                        end_mp4_fragment(core, &hlsmux->audio[source][sub_stream], source, sub_stream, IS_AUDIO, segment_time);
                    }
                }

                source_data[source].discontinuity[hlsmux->video[source].file_sequence_number] = source_data[source].source_discontinuity;
                source_data[source].splice_duration[hlsmux->video[source].file_sequence_number] = source_data[source].source_splice_duration;

                source_data[source].segment_lengths_audio[hlsmux->audio[source][sub_stream].file_sequence_number][sub_stream] = frag_delta;
                source_data[source].full_time_audio[hlsmux->audio[source][sub_stream].file_sequence_number][sub_stream] = segment_time;
                source_data[source].full_duration_audio[hlsmux->audio[source][sub_stream].file_sequence_number][sub_stream] = duration_time;
                //source_data[source].full_time[hlsmux->audio[source][sub_stream].file_sequence_number] = frame->full_time;

                if (hlsmux->audio[source][sub_stream].fragments_published > core->cd->window_size) {
                    fprintf(stderr,"\n\n\n\n\nHLSMUX: UPDATING AUDIO MANIFEST: SOURCE:%d SUB_STREAM:%d\n\n\n\n", source, sub_stream);
                    update_ts_audio_manifest(core, &hlsmux->audio[source][sub_stream], source, sub_stream,
                                             source_data[source].source_discontinuity,
                                             &source_data[source]);
                    if (core->cd->enable_fmp4_output) {
                        update_mp4_audio_manifest(core, &hlsmux->audio[source][sub_stream], source,
                                                  sub_stream, source_data[source].source_discontinuity,
                                                  &source_data[source]);
                        if (source == 0 && hlsmux->video[source].fragments_published > (core->cd->window_size+2)) { // was+1?
                            write_dash_master_manifest(core, &source_data[0]);
                        }
                    }
                    source_data[source].source_discontinuity = 0;
                    source_data[source].source_splice_duration = 0;
                    sub_manifest_ready = 1;
                }

                hlsmux->audio[source][sub_stream].file_sequence_number = (hlsmux->audio[source][sub_stream].file_sequence_number + 1) % core->cd->rollover_size;
                hlsmux->audio[source][sub_stream].media_sequence_number = (hlsmux->audio[source][sub_stream].media_sequence_number + 1);

                source_data[source].video_fragment_ready[sub_stream] = 0;
                source_data[source].start_time_audio[sub_stream] = frame->full_time;

                syslog(LOG_INFO,"HLSMUX: STARTING NEW AUDIO FRAGMENT(SOURCE:%d/SUB:%d): LENGTH:%.2f  TOTAL:%.2f EXPECTED:%.2f (NEW START:%ld) FILESEQ:%ld MEDIASEQ:%ld PUBLISHED:%ld\n",
                       source,
                       sub_stream,
                       frag_delta,
                       source_data[source].total_audio_duration[sub_stream],
                       source_data[source].expected_audio_duration[sub_stream],
                       source_data[source].start_time_audio[sub_stream],
                       hlsmux->audio[source][sub_stream].file_sequence_number,
                       hlsmux->audio[source][sub_stream].media_sequence_number,
                       hlsmux->audio[source][sub_stream].fragments_published);

                source_data[source].total_audio_duration[sub_stream] += frag_delta;
                source_data[source].expected_audio_duration[sub_stream] += fragment_length;

                if (core->cd->enable_ts_output) {
                    start_ts_fragment(core, &hlsmux->audio[source][sub_stream], source, sub_stream, IS_AUDIO);  // start first audio fragment
                }
                if (core->cd->enable_fmp4_output) {
                    // FIX FIX FIX FIX FIX FIX
                    audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[source].audio_stream; //[sub_stream];

                    start_mp4_fragment(core, &hlsmux->audio[source][sub_stream], source, IS_AUDIO, sub_stream);
                    if (hlsmux->audio[source][sub_stream].fmp4 == NULL) {
                        int audio_bitrate;

                        hlsmux->audio[source][sub_stream].fmp4 = fmp4_file_create(frame->media_type,
                                                                                  AUDIO_CLOCK, //astream->audio_samplerate,
                                                                                  0x157c, // english language code
                                                                                  fragment_length);  // fragment length in seconds


#if defined(ENABLE_TRANSCODE)
                        if (core->transcode_enabled) {
                            audio_bitrate = core->cd->transaudio_info[source].audio_bitrate;
                        } else {
                            audio_bitrate = astream->audio_bitrate;
                        }
#else
                        audio_bitrate = astream->audio_bitrate;
#endif

                        fmp4_audio_track_create(hlsmux->audio[source][sub_stream].fmp4,
                                                astream->audio_channels,
                                                astream->audio_samplerate,
                                                astream->audio_object_type,
                                                audio_bitrate);

                    }
                }
            }

            /*
            if (core->cd->enable_youtube_output) {
                audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[sub_stream];
                double fragment_timestamp = frame->pts;
                int fragment_duration = frame->duration * astream->audio_samplerate / (double)AUDIO_CLOCK;

                if (astream->audio_object_type == 5) { // sbr
                    fragment_duration = fragment_duration << 1;
                }
                fmp4_audio_fragment_add(hlsmux->video[source].fmp4,
                                        frame->buffer,
                                        frame->buffer_size,
                                        fragment_timestamp,
                                        fragment_duration);
            }
            */

            if (core->cd->enable_fmp4_output) {
                if (hlsmux->audio[source][sub_stream].output_fmp4_file != NULL) {
                    if (hlsmux->audio[source][sub_stream].fmp4) {
                        // FIX FIX FIX FIX FIX
                        audio_stream_struct *astream = (audio_stream_struct*)core->source_audio_stream[source].audio_stream;//[sub_stream];
                        double fragment_timestamp = frame->pts;
                        int fragment_duration = frame->duration * astream->audio_samplerate / (double)AUDIO_CLOCK;

                        if (astream->audio_object_type == 5) { // sbr
                            fragment_duration = fragment_duration << 1;
                        }

                        /*syslog(LOG_INFO,"HLSMUX: ADDING AUDIO FRAGMENT(%d): %d  BUFFER:%p  OFFSET:%d   DURATION:%d\n",
                               source,
                               hlsmux->audio[source][sub_stream].fmp4->fragment_count,
                               hlsmux->audio[source][sub_stream].fmp4->buffer,
                               hlsmux->audio[source][sub_stream].fmp4->buffer_offset,
                               fragment_duration);*/

                        if (frame->media_type == MEDIA_TYPE_AAC) {
                            if (astream->audio_samples_to_add > 0) {
                                astream->audio_samples_to_add--;

                                if (astream->audio_channels == 2) {
                                    fmp4_audio_fragment_add(hlsmux->audio[source][sub_stream].fmp4,
                                                            aac_quiet_2,
                                                            sizeof(aac_quiet_2),
                                                            fragment_timestamp,
                                                            fragment_duration);
                                } else if (astream->audio_channels == 6) {
                                    fmp4_audio_fragment_add(hlsmux->audio[source][sub_stream].fmp4,
                                                            aac_quiet_6,
                                                            sizeof(aac_quiet_6),
                                                            fragment_timestamp,
                                                            fragment_duration);
                                }
                            }
                        } else {
                            //placeholder for adding ac3 audio silence samples 2/6 channel
                        }

                        /*fprintf(stderr,"\n\nMP4 AUDIO FRAGMENT ADD (SOURCE=%d SUBSTREAM=%d): SIZE:%d TIMESTAMP:%f DURATION:%d\n\n",
                                source,
                                sub_stream,
                                frame->buffer_size,
                                fragment_timestamp,
                                fragment_duration);
                        */
                        fmp4_audio_fragment_add(hlsmux->audio[source][sub_stream].fmp4,
                                                frame->buffer,
                                                frame->buffer_size,
                                                fragment_timestamp,
                                                fragment_duration);
                    }
                }
            }

            if (core->cd->enable_ts_output) {
                if (hlsmux->audio[source][sub_stream].output_ts_file != NULL) {
                    int s;
                    uint8_t pat[188];
                    uint8_t pmt[188];
                    int64_t pc;

                    muxpatsample(core, &hlsmux->audio[source][sub_stream], &pat[0]);
                    fwrite(pat, 1, 188, hlsmux->audio[source][sub_stream].output_ts_file);
                    if (frame->media_type == MEDIA_TYPE_AAC) {
                        muxpmtsample(core, &hlsmux->audio[source][sub_stream], &pmt[0], AUDIO_BASE_PID, CODEC_AAC);
                    } else if (frame->media_type == MEDIA_TYPE_AC3) {
                        muxpmtsample(core, &hlsmux->audio[source][sub_stream], &pmt[0], AUDIO_BASE_PID, CODEC_AC3);
                    }
                    fwrite(pmt, 1, 188, hlsmux->audio[source][sub_stream].output_ts_file);

                    pc = muxaudiosample(core, &hlsmux->audio[source][sub_stream], frame, 0);

                    hlsmux->audio[source][sub_stream].packet_count += pc;
                    for (s = 0; s < pc; s++) {
                        uint8_t *muxbuffer;
                        muxbuffer = hlsmux->audio[source][sub_stream].muxbuffer;
                        if (s == 0 && (muxbuffer[5] == 0x10 || muxbuffer[5] == 0x50)) {
                            int64_t base;
                            int64_t ext;
                            int64_t full;
                            int64_t total_pc = hlsmux->audio[source][sub_stream].packet_count;
                            int64_t offset_count;
                            int64_t timestamp;
                            int64_t timestamp_offset;

                            timestamp = frame->pts;
                            timestamp_offset = timestamp - AUDIO_OFFSET;
                            if (timestamp_offset < 0) {
                                timestamp_offset += 8589934592;
                            }
                            offset_count = (int64_t)(((double)(timestamp_offset)*(double)300.0*(double)20.0/(double)216) - (double)10)/(double)188;
                            total_pc = offset_count;

                            full = (int64_t)((int64_t)total_pc * (int64_t)40608) / (int64_t)20;
                            full = full % (8589934592 * 300);
                            base = full / 300;
                            ext = full % 300;

                            muxbuffer[6] = (0xff & (base >> 25));
                            muxbuffer[7] = (0xff & (base >> 17));
                            muxbuffer[8] = (0xff & (base >> 9));
                            muxbuffer[9] = (0xff & (base >> 1));
                            muxbuffer[10] = ((0x01 & base) << 7) | 0x7e | ((0x100 & ext) >> 8);
                            muxbuffer[11] = (0xff & ext);
                        }
                        fwrite(&muxbuffer[s*188], 1, 188, hlsmux->audio[source][sub_stream].output_ts_file);
                    }
                }
            }
        }
skip_sample:
        if (frame->frame_type == FRAME_TYPE_VIDEO) {
            memory_return(core->compressed_video_pool, frame->buffer);
        } else {
            memory_return(core->compressed_audio_pool, frame->buffer);
        }
        frame->buffer = NULL;
        memory_return(core->frame_msg_pool, frame);
        frame = NULL;
        memory_return(core->fillet_msg_pool, msg);
        msg = NULL;
    }

cleanup_mux_pump_thread:
    // free all buffers in the queue
    msg = dataqueue_take_back(hlsmux->input_queue);
    while (msg) {
        frame = (sorted_frame_struct*)msg->buffer;
        if (frame) {
            if (frame->frame_type == FRAME_TYPE_VIDEO) {
                memory_return(core->compressed_video_pool, frame->buffer);
            } else {
                memory_return(core->compressed_audio_pool, frame->buffer);
            }
            frame->buffer = NULL;
            memory_return(core->frame_msg_pool, frame);
            frame = NULL;
        }
        memory_return(core->fillet_msg_pool, msg);
        msg = dataqueue_take_back(hlsmux->input_queue);
    }

    for (i = 0; i < num_sources; i++) {
        int j;

        if (core->cd->enable_ts_output) {
            hlsmux->video[i].packet_count = 0;
            if (hlsmux->video[i].output_ts_file) {
                fclose(hlsmux->video[i].output_ts_file);
                hlsmux->video[i].output_ts_file = NULL;
            }
            for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                hlsmux->audio[i][j].packet_count = 0;
                if (hlsmux->audio[i][j].output_ts_file) {
                    fclose(hlsmux->audio[i][j].output_ts_file);
                    hlsmux->audio[i][j].output_ts_file = NULL;
                }
            }
        }
        if (core->cd->enable_fmp4_output) {
            if (hlsmux->video[i].output_fmp4_file) {
                fclose(hlsmux->video[i].output_fmp4_file);
                hlsmux->video[i].output_fmp4_file = NULL;
            }
            if (hlsmux->video[i].fmp4) {
                fmp4_file_finalize(hlsmux->video[i].fmp4);
                hlsmux->video[i].fmp4 = NULL;
            }
            for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
                if (hlsmux->audio[i][j].output_fmp4_file) {
                    fclose(hlsmux->audio[i][j].output_fmp4_file);
                    hlsmux->audio[i][j].output_fmp4_file = NULL;
                }
                if (hlsmux->audio[i][j].fmp4) {
                    fmp4_file_finalize(hlsmux->audio[i][j].fmp4);
                    hlsmux->audio[i][j].fmp4 = NULL;
                }
            }
        }
    }

    for (i = 0; i < MAX_VIDEO_SOURCES; i++) {
        int j;

        free(hlsmux->video[i].pesbuffer);
        hlsmux->video[i].pesbuffer = NULL;
        free(hlsmux->video[i].muxbuffer);
        hlsmux->video[i].muxbuffer = NULL;
        free(hlsmux->video[i].packettable);
        hlsmux->video[i].packettable = NULL;
        if (i == 0) {
            free(hlsmux->video[i].textbuffer);
            hlsmux->video[i].textbuffer = NULL;
        }

        for (j = 0; j < MAX_AUDIO_STREAMS; j++) {
            free(hlsmux->audio[i][j].pesbuffer);
            hlsmux->audio[i][j].pesbuffer = NULL;
            free(hlsmux->audio[i][j].muxbuffer);
            hlsmux->audio[i][j].muxbuffer = NULL;
            free(hlsmux->audio[i][j].packettable);
            hlsmux->audio[i][j].packettable = NULL;
        }
    }

    quit_mux_pump_thread = 0;

    return NULL;
}
