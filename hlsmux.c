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

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "fillet.h"
#include "dataqueue.h"
#include "mempool.h"
#include "tsdecode.h"
#include "crc.h"
#include "mp4core.h"
#include "hlsmux.h"

#define MAX_STREAM_NAME       256

#define VIDEO_PID             480
#define AUDIO_BASE_PID        481
#define PAT_PID               0
#define PMT_PID               2457

#define CODEC_H264            0x1b
#define CODEC_AAC             0x0f

#define MAX_SOURCE_STREAMS    16

#define MDSIZE_AUDIO          32
#define MDSIZE_VIDEO          150

#define VIDEO_OFFSET          150000
#define AUDIO_OFFSET          20000
#define OVERFLOW_PTS          8589934592   // 2^33

typedef struct _source_context_struct_ {
    int64_t       start_time_video;
    int64_t       start_time_audio;
    double        total_video_duration;
    double        total_audio_duration;
    double        expected_audio_duration;
    double        expected_video_duration;
    int           video_fragment_ready;
    double        segment_lengths[MAX_ROLLOVER_SIZE];
    int           discontinuity[MAX_ROLLOVER_SIZE];
    int           source_discontinuity;
    
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
} source_context_struct;

static const uint8_t aac_quiet_2[23] = {  0xde, 0x02, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x35, 0x36, 0x2e, 0x36, 0x30, 0x2e, 0x31, 0x30, 0x30, 0x00, 0x42, 0x20, 0x08, 0xc1, 0x18, 0x38 };
static const uint8_t aac_quiet_6[36] = {  0xde, 0x02, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x35, 0x36, 0x2e, 0x36, 0x30, 0x2e, 0x31, 0x30, 0x30, 0x00, 0x02, 0x30, 0x40, 0x02, 0x11, 0x00,
					  0x46, 0x08, 0xc0, 0x46, 0x20, 0x08, 0xc1, 0x18, 0x18, 0x46, 0x00, 0x01, 0xc0 };

typedef struct _decode_struct_ {
    uint8_t       *start;
    uint32_t       bitsize;
    int            cb;
} decode_struct;

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

int find_and_decode_sps(source_context_struct *sdata, uint8_t *buffer, int buffer_size)
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
	dataqueue_destroy(hlsmux1->input_queue);
	hlsmux1->input_queue = NULL;
	free(hlsmux1);
	hlsmux1 = NULL;
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

    for (i = 0; i < num_sources; i++) {
	hlsmux_struct *hlsmux = (hlsmux_struct*)core->hlsmux;	

	fwrite(&hlsmux->video[i].file_sequence_number, sizeof(int64_t), 1, state_file);
	fwrite(&hlsmux->video[i].media_sequence_number, sizeof(int64_t), 1, state_file);
	fwrite(&hlsmux->video[0].fragments_published, sizeof(int64_t), 1, state_file);  //the 0 is not a typo-need to line up if restart occurs
	fwrite(&hlsmux->audio[i].file_sequence_number, sizeof(int64_t), 1, state_file);
	fwrite(&hlsmux->audio[i].media_sequence_number, sizeof(int64_t), 1, state_file);
	fwrite(&hlsmux->video[0].fragments_published, sizeof(int64_t), 1, state_file);  //yes,video - so they match
    }

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
    int s64 = sizeof(int64_t);

    snprintf(state_filename,MAX_STREAM_NAME-1,"/var/tmp/hlsmux_state_%d", core->cd->identity);

    state_file = fopen(state_filename,"rb");
    if (state_file) {
	for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
	    r = fread(sdata, sizeof(source_context_struct), 1, state_file);
	    sdata++;
	}
	
	for (i = 0; i < num_sources; i++) {
	    hlsmux_struct *hlsmux = (hlsmux_struct*)core->hlsmux;	
	    
	    r = fread(&hlsmux->video[i].file_sequence_number, sizeof(int64_t), 1, state_file);
	    r = fread(&hlsmux->video[i].media_sequence_number, sizeof(int64_t), 1, state_file);
	    r = fread(&hlsmux->video[i].fragments_published, sizeof(int64_t), 1, state_file);	    
	    r = fread(&hlsmux->audio[i].file_sequence_number, sizeof(int64_t), 1, state_file);
	    r = fread(&hlsmux->audio[i].media_sequence_number, sizeof(int64_t), 1, state_file);
	    r = fread(&hlsmux->audio[i].fragments_published, sizeof(int64_t), 1, state_file);
	    /*if (r < s64*6) {
		fclose(state_file);
		return 0;
	    }*/
	}
	
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
    if (frame->pts > 0 && frame->dts > 0) {
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

static int start_ts_fragment(fillet_app_struct *core, stream_struct *stream, int source, int video)
{
    if (!stream->output_ts_file) {
	char stream_name[MAX_STREAM_NAME];
	if (video) {
	    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/video_stream%d_%ld.ts", core->cd->manifest_directory, source, stream->file_sequence_number);
	} else {
	    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio_stream%d_%ld.ts", core->cd->manifest_directory, source, stream->file_sequence_number);	    
	}
	stream->output_ts_file = fopen(stream_name,"w");
    }
    
    return 0;
}

static int start_init_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source, int video)
{
    struct stat sb;
    
    if (!stream->output_fmp4_file) {
	char stream_name[MAX_STREAM_NAME];
	char local_dir[MAX_STREAM_NAME];
	if (video) {
	    snprintf(local_dir, MAX_STREAM_NAME-1, "%s/video%d", core->cd->manifest_directory, source);	    
	} else {
	    snprintf(local_dir, MAX_STREAM_NAME-1, "%s/audio%d", core->cd->manifest_directory, source);	    
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

static int start_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source, int video)
{
    struct stat sb;
    
    if (!stream->output_fmp4_file) {
	char stream_name[MAX_STREAM_NAME];
	char local_dir[MAX_STREAM_NAME];
	if (video) {
	    snprintf(local_dir, MAX_STREAM_NAME-1, "%s/video%d", core->cd->manifest_directory, source);	    
	} else {
	    snprintf(local_dir, MAX_STREAM_NAME-1, "%s/audio%d", core->cd->manifest_directory, source);	    
	}
	if (stat(local_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
	    fprintf(stderr,"STATUS: fMP4 manifest directory exists: %s\n", local_dir);
	} else {
	    fprintf(stderr,"STATUS: fMP4 manifest directory does not exist: %s (CREATING)\n", local_dir);
	    mkdir(local_dir, 0700);
	    fprintf(stderr,"STATUS: Done creating fMP4 manifest directory\n");
	}

	snprintf(stream_name, MAX_STREAM_NAME-1, "%s/segment%d.mp4", local_dir, stream->file_sequence_number);
	stream->output_fmp4_file = fopen(stream_name,"w");
    }
    return 0;
}

static int end_mp4_fragment(fillet_app_struct *core, stream_struct *stream, int source)
{
    if (stream->output_fmp4_file) {
	fclose(stream->output_fmp4_file);
	stream->output_fmp4_file = NULL;
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

    fprintf(video_manifest,"#EXTM3U\n");
    fprintf(video_manifest,"#EXT-X-VERSION:6\n");
    fprintf(video_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(video_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);
    
    for (i = 0; i < core->cd->window_size; i++) {
	int64_t next_sequence_number;

	next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;
        if (sdata->discontinuity[next_sequence_number]) {
	    fprintf(video_manifest,"#EXT-X-DISCONTINUITY\n");
	}
        fprintf(video_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths[next_sequence_number]);
	fprintf(video_manifest,"video_stream%d_%ld.ts\n", source, next_sequence_number);
    }

    fclose(video_manifest);    
    
    return 0;
}

static int update_ts_audio_manifest(fillet_app_struct *core, stream_struct *stream, int source, int discontinuity, source_context_struct *sdata)
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
    
    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio%d.m3u8", core->cd->manifest_directory, source);
    audio_manifest = fopen(stream_name,"w");

    fprintf(audio_manifest,"#EXTM3U\n");
    fprintf(audio_manifest,"#EXT-X-VERSION:6\n");
    fprintf(audio_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(audio_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);
    
    for (i = 0; i < core->cd->window_size; i++) {
	int64_t next_sequence_number;	
	next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;
        if (sdata->discontinuity[next_sequence_number]) {
	    fprintf(audio_manifest,"#EXT-X-DISCONTINUITY\n");
	}	
        fprintf(audio_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths[next_sequence_number]);	
	fprintf(audio_manifest,"audio_stream%d_%ld.ts\n", source, next_sequence_number);
    }

    fclose(audio_manifest);
    
    return 0;
}

static int write_ts_master_manifest(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    int i;

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
	fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
    } else {
	fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
	mkdir(core->cd->manifest_directory, 0700);
	fprintf(stderr,"STATUS: Done creating manifest directory\n");
    }

    snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/master.m3u8",core->cd->manifest_directory);

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
	fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
	return -1;
    }
   
    fprintf(master_manifest,"#EXTM3U\n");
    if (strlen(sdata->lang_tag) > 0) {
	fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"allaudio\",LANGUAGE=\"%s\",NAME=\"%s\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio0.m3u8\"\n",
		sdata->lang_tag,
		sdata->lang_tag);
    } else {
	fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"allaudio\",LANGUAGE=\"eng\",NAME=\"eng\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio0.m3u8\"\n");
    }

    for (i = 0; i < core->num_sources; i++) {
	video_stream_struct *vstream = (video_stream_struct*)core->source_stream[i].video_stream;	
	
	fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%ld,CODECS=\"avc1.%2x%02x%2x\",RESOLUTION=%dx%d,AUDIO=\"allaudio\"\n",
		vstream->video_bitrate,
		sdata->h264_profile, //hex
		sdata->midbyte,
		sdata->h264_level,
		sdata->width, sdata->height);	
	fprintf(master_manifest,"video%d.m3u8\n", i);
	sdata++;
    }
    
    fclose(master_manifest);
    
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
    fprintf(video_manifest,"#EXT-X-VERSION:8\n");
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
        fprintf(video_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths[next_sequence_number]);
	fprintf(video_manifest,"video%d/segment%ld.mp4\n", source, next_sequence_number);
    }

    fclose(video_manifest);    
    
    return 0;
}

static int update_mp4_audio_manifest(fillet_app_struct *core, stream_struct *stream, int source, int discontinuity, source_context_struct *sdata)
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
    
    snprintf(stream_name, MAX_STREAM_NAME-1, "%s/audio%dfmp4.m3u8", core->cd->manifest_directory, source);    
    audio_manifest = fopen(stream_name,"w");

    fprintf(audio_manifest,"#EXTM3U\n");
    fprintf(audio_manifest,"#EXT-X-VERSION:8\n");
    fprintf(audio_manifest,"#EXT-X-MEDIA-SEQUENCE:%ld\n", starting_media_sequence_number);
    fprintf(audio_manifest,"#EXT-X-INDEPENDENT-SEGMENTS\n");    
    fprintf(audio_manifest,"#EXT-X-TARGETDURATION:%d\n", core->cd->segment_length);
    fprintf(audio_manifest,"#EXT-X-MAP:URI=\"audio%d/init.mp4\"\n", source);

    for (i = 0; i < core->cd->window_size; i++) {
	int64_t next_sequence_number;	
	next_sequence_number = (starting_file_sequence_number + i) % core->cd->rollover_size;
        if (sdata->discontinuity[next_sequence_number]) {
	    fprintf(audio_manifest,"#EXT-X-DISCONTINUITY\n");
	}	
        fprintf(audio_manifest,"#EXTINF:%.2f,\n", (float)sdata->segment_lengths[next_sequence_number]);
	fprintf(audio_manifest,"audio%d/segment%ld.mp4\n", source, next_sequence_number);	
    }

    fclose(audio_manifest);
    
    return 0;
}

/*
static int write_dash_master_manifest(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    int i;

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
	fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
    } else {
	fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
	mkdir(core->cd->manifest_directory, 0700);
	fprintf(stderr,"STATUS: Done creating manifest directory\n");
    }

    snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/masterdash.mpd",core->cd->manifest_directory);

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
	fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
	return -1;
    }

    fprintf(master_manifest,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");

    fprintf(master_manifest,"<MPD availabilityStartTime=\"1970-01-01T00:00:00Z\" minBufferTime=\"PT2S\" minimumUpdatePeriod=\"PT0S\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" publishTime=\"2018-04-25T22:38:43Z\" timeShiftBufferDepth=\"PT5M\" type=\"dynamic\" ns1:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:ns1=\"http://www.w3.org/2001/XMLSchema-instance\">\n");
    fprintf(master_manifest,"<ProgramInformation>\n");
    fprintf(master_manifest,"<Title>Live Stream</Title>\n");
    fprintf(master_manifest,"</ProgramInformation>\n");
    
    fprintf(master_manifest,"<Period id=\"p0\" start=\"PT0S\">\n");
    fprintf(master_manifest,"<AdaptationSet id=\"1\" group=\"1\" contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\" maxWidth=\"1920\" maxHeight=\"1080\" startWithSAP=\"1\">\n");
    fprintf(master_manifest,"<Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"main\" />\n");
    fprintf(master_manifest,"<ContentComponent id=\"1\" contentType=\"video\" />\n");
    fprintf(master_manifest,"<SegmentTemplate timescale=\"90000\" media=\"$RepresentationID$/segment$Time$.mp4\" initialization=\"$RepresentationID$/init.mp4\">\n");
    fprintf(master_manifest,"<SegmentTimeline>\n");
    
    fprintf(master_manifest,"<S t=\"0\" d=\"450000\" r=\"10\" />\n");

    fprintf(master_manifest,"</SegmentTimeline>\n");
    fprintf(master_manifest,"</SegmentTemplate>\n");

    for (i = 0; i < core->num_sources; i++) {
	video_stream_struct *vstream = (video_stream_struct*)core->source_stream[i].video_stream;

	fprintf(master_manifest,"<Representation id=\"video%d\" codecs=\"avc1.%2x%02x%2x\" width=\"%d\" height=\"%d\" frameRate=\"30000/1001\" bandwidth=\"%d\" />\n",
		i,
		sdata->h264_profile, //hex
		sdata->midbyte,
		sdata->h264_level,
		sdata->width, sdata->height,
		vstream->video_bitrate);	
	sdata++;
    }    

    fprintf(master_manifest,"</AdaptationSet>\n");

    fprintf(master_manifest,"</Period>\n");
    fprintf(master_manifest,"</MPD>\n");    

    fclose(master_manifest);
    return 0;
}
*/

static int write_mp4_master_manifest(fillet_app_struct *core, source_context_struct *sdata)
{
    struct stat sb;
    char master_manifest_filename[MAX_STREAM_NAME];
    FILE *master_manifest;
    int i;

    if (stat(core->cd->manifest_directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
	fprintf(stderr,"STATUS: Manifest directory exists: %s\n", core->cd->manifest_directory);
    } else {
	fprintf(stderr,"STATUS: Manifest directory does not exist: %s (CREATING)\n", core->cd->manifest_directory);
	mkdir(core->cd->manifest_directory, 0700);
	fprintf(stderr,"STATUS: Done creating manifest directory\n");
    }

    snprintf(master_manifest_filename,MAX_STREAM_NAME-1,"%s/masterfmp4.m3u8",core->cd->manifest_directory);

    master_manifest = fopen(master_manifest_filename,"w");
    if (!master_manifest) {
	fprintf(stderr,"ERROR: Unable to create master manifest file - please check system configuration: %s\n", master_manifest_filename);
	return -1;
    }
   
    fprintf(master_manifest,"#EXTM3U\n");
    fprintf(master_manifest,"#EXT-X-VERSION:8\n");
    fprintf(master_manifest,"#EXT-X-INDEPENDENT-SEGMENTS\n");
    if (strlen(sdata->lang_tag) > 0) {
	fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"allaudio\",LANGUAGE=\"%s\",NAME=\"%s\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio%dfmp4.m3u8\"\n",
		sdata->lang_tag,
		sdata->lang_tag,
		0);
    } else {
	fprintf(master_manifest,"#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"allaudio\",LANGUAGE=\"eng\",NAME=\"eng\",AUTOSELECT=YES,DEFAULT=YES,URI=\"audio%dfmp4.m3u8\"\n",
		0);
    }

    for (i = 0; i < core->num_sources; i++) {
	video_stream_struct *vstream = (video_stream_struct*)core->source_stream[i].video_stream;	
	
	fprintf(master_manifest,"#EXT-X-STREAM-INF:BANDWIDTH=%ld,CODECS=\"avc1.%2x%02x%2x\",RESOLUTION=%dx%d,AUDIO=\"allaudio\"\n",
		vstream->video_bitrate,
		sdata->h264_profile, //hex
		sdata->midbyte,
		sdata->h264_level,
		sdata->width, sdata->height);	
	fprintf(master_manifest,"video%dfmp4.m3u8\n", i);
	sdata++;
    }
    
    fclose(master_manifest);
    
    return 0;
}


static int end_ts_fragment(fillet_app_struct *core, stream_struct *stream, int source)
{
    if (stream->output_ts_file) {
	fclose(stream->output_ts_file);
	stream->output_ts_file = NULL;
	stream->fragments_published++;
    }
    
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
    
    for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
	source_data[i].start_time_video = -1;
	source_data[i].start_time_audio = -1;
	source_data[i].total_video_duration = 0;
	source_data[i].total_audio_duration = 0;
	source_data[i].expected_audio_duration = 0;
	source_data[i].expected_video_duration = 0;
	source_data[i].video_fragment_ready = 0;
	source_data[i].source_discontinuity = 0;
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
    }

    for (i = 0; i < num_sources; i++) {
	hlsmux->video[i].muxbuffer = (uint8_t*)malloc(MAX_VIDEO_MUX_BUFFER);
	hlsmux->video[i].pesbuffer = (uint8_t*)malloc(MAX_VIDEO_PES_BUFFER);
	hlsmux->video[i].packettable = (packet_struct*)malloc(sizeof(packet_struct)*(MAX_VIDEO_MUX_BUFFER/188));
	hlsmux->video[i].packet_count = 0;
	hlsmux->video[i].output_ts_file = NULL;
	hlsmux->video[i].output_fmp4_file = NULL;
	hlsmux->video[i].file_sequence_number = 0;
	hlsmux->video[i].media_sequence_number = 0;
	hlsmux->video[i].fragments_published = 0;
	hlsmux->video[i].fmp4 = NULL;
	
	hlsmux->audio[i].muxbuffer = (uint8_t*)malloc(MAX_VIDEO_MUX_BUFFER);
	hlsmux->audio[i].pesbuffer = (uint8_t*)malloc(MAX_VIDEO_PES_BUFFER);
	hlsmux->audio[i].packettable = (packet_struct*)malloc(sizeof(packet_struct)*(MAX_VIDEO_MUX_BUFFER/188));
	hlsmux->audio[i].packet_count = 0;
	hlsmux->audio[i].output_ts_file = NULL;
	hlsmux->audio[i].output_fmp4_file = NULL;	
	hlsmux->audio[i].file_sequence_number = 0;
	hlsmux->audio[i].media_sequence_number = 0;
	hlsmux->audio[i].fragments_published = 0;
	hlsmux->audio[i].fmp4 = NULL;
    }

    while (1) {
	msg = dataqueue_take_back(hlsmux->input_queue);
	if (!msg) {
	    usleep(1000);
	    continue;
	}

	source_discontinuity = msg->source_discontinuity;
	if (source_discontinuity) {
	    int64_t first_video_file_sequence_number;
	    int64_t first_video_media_sequence_number;
	    int available = 0;
	    
	    fprintf(stderr,"SOURCE DISCONTINUITY ENCOUNTERED\n");
	    available = hlsmux_load_state(core, &source_data[0]);		
    
	    for (i = 0; i < MAX_SOURCE_STREAMS; i++) {
		source_data[i].start_time_video = -1;
		source_data[i].start_time_audio = -1;
		source_data[i].total_video_duration = 0;
		source_data[i].total_audio_duration = 0;
		source_data[i].expected_audio_duration = 0;
		source_data[i].expected_video_duration = 0;
		source_data[i].video_fragment_ready = 0;
		source_data[i].h264_sps_decoded = 0;		
		source_data[i].h264_sps_size = 0;
		source_data[i].h264_pps_size = 0;
		source_data[i].hevc_sps_decoded = 0;
		source_data[i].hevc_sps_size = 0;
		source_data[i].hevc_pps_size = 0;
		source_data[i].hevc_vps_size = 0;		
		if (available) {
		    source_data[i].source_discontinuity = 1;
		}
	    }

	    first_video_file_sequence_number = hlsmux->video[0].file_sequence_number;
	    first_video_media_sequence_number = hlsmux->video[0].media_sequence_number;

	    fprintf(stderr,"FIRST FILE SEQ: %ld  FIRST MEDIA SEQ:%ld\n",
		    first_video_file_sequence_number,
		    first_video_media_sequence_number);
	    
	    for (i = 0; i < num_sources; i++) {
		if (core->cd->enable_ts_output) {
		    hlsmux->video[i].packet_count = 0;
		    if (hlsmux->video[i].output_ts_file) {
			fclose(hlsmux->video[i].output_ts_file);
			hlsmux->video[i].output_ts_file = NULL;		    
		    }		
		    hlsmux->audio[i].packet_count = 0;
		    if (hlsmux->audio[i].output_ts_file) {
			fclose(hlsmux->audio[i].output_ts_file);
			hlsmux->audio[i].output_ts_file = NULL;
		    }
		}
		if (core->cd->enable_fmp4_output) {
		    if (hlsmux->video[i].output_fmp4_file) {
			fclose(hlsmux->video[i].output_fmp4_file);
			hlsmux->video[i].output_fmp4_file = NULL;		    
		    }		
		    if (hlsmux->audio[i].output_fmp4_file) {
			fclose(hlsmux->audio[i].output_fmp4_file);
			hlsmux->audio[i].output_fmp4_file = NULL;
		    }
		    if (hlsmux->video[i].fmp4) {
			fmp4_file_finalize(hlsmux->video[i].fmp4);
			hlsmux->video[i].fmp4 = NULL;
		    }
		    if (hlsmux->audio[i].fmp4) {
			fmp4_file_finalize(hlsmux->audio[i].fmp4);
			hlsmux->audio[i].fmp4 = NULL;
		    }						    
		}

		hlsmux->video[i].file_sequence_number = first_video_file_sequence_number;
		hlsmux->video[i].media_sequence_number = first_video_media_sequence_number;
		hlsmux->video[i].fmp4 = NULL;
		hlsmux->audio[i].file_sequence_number = first_video_file_sequence_number;
		hlsmux->audio[i].media_sequence_number = first_video_media_sequence_number;
		hlsmux->audio[i].fmp4 = NULL;
	    }
	    source_discontinuity = 0;
	}

	frame = (sorted_frame_struct*)msg->buffer;
	if (frame->frame_type == FRAME_TYPE_VIDEO) {
	    int source = frame->source;
	    int err;
	    int n;
	    int manifest_ready = 0;

	    if (frame->media_type == MEDIA_TYPE_H264) {
		if (!source_data[source].h264_sps_decoded) {
		    find_and_decode_sps(&source_data[source], frame->buffer, frame->buffer_size);
		}

		if (source_data[source].h264_sps_size == 0) {
		    get_h264_sps(&source_data[source], frame->buffer, frame->buffer_size);
		    fprintf(stderr,"HLSMUX: SAVING SPS: SIZE:%d\n", source_data[source].h264_sps_size);
		}
		if (source_data[source].h264_pps_size == 0) {
		    get_h264_pps(&source_data[source], frame->buffer, frame->buffer_size);
		    fprintf(stderr,"HLSMUX: SAVING PPS: SIZE:%d\n", source_data[source].h264_pps_size);		
		}
		
		for (n = 0; n < num_sources; n++) {
		    if (source_data[n].h264_sps_decoded) {
			manifest_ready++;
		    }
		}
	    } else if (frame->media_type == MEDIA_TYPE_HEVC) {
		//HEVC placeholder for sps, pps, vps parsing
	    } else {
		fprintf(stderr,"HLSMUX: UNSUPPORTED MEDIA TYPE\n");
	    }	    
	    
	    if (manifest_ready == num_sources && manifest_written == 0 && sub_manifest_ready) {
		manifest_written = 1;
		fprintf(stderr,"HLSMUX: WRITING OUT MASTER MANIFEST FILE\n");
		if (core->cd->enable_ts_output) {		
		    err = write_ts_master_manifest(core, &source_data[0]);
		    if (err < 0) {
			//placeholder for error handling
			goto skip_sample;			
			//continue;
		    }
		}

		if (core->cd->enable_fmp4_output) {
		    //err = write_dash_master_manifest(core, &source_data[0]);
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
		
		fprintf(stderr,"HLSMUX: SYNC FRAME FOUND: PTS: %ld DTS:%ld\n",
			frame->pts, frame->dts);
		if (source_data[source].start_time_video == -1) {
		    source_data[source].start_time_video = frame->full_time;
		    
		    if (core->cd->enable_fmp4_output) {
			video_stream_struct *vstream = (video_stream_struct*)core->source_stream[source].video_stream;

			if (frame->media_type == MEDIA_TYPE_H264) {
			    hlsmux->video[source].fmp4 = fmp4_file_create(MEDIA_TYPE_H264,
									  90000, // timescale for H264 video
									  0x15c7, // english language code
									  fragment_length);  // fragment length in seconds

			    // logic needed for multiple sps/pps
			    fmp4_video_set_sps(hlsmux->video[source].fmp4,
					       source_data[source].h264_sps,
					       source_data[source].h264_sps_size);
			    
			    fmp4_video_set_pps(hlsmux->video[source].fmp4,
					       source_data[source].h264_pps,
					       source_data[source].h264_pps_size);			
			    
			    fmp4_video_track_create(hlsmux->video[source].fmp4,
						    source_data[source].width,
						    source_data[source].height,
						    vstream->video_bitrate);
			} else if (frame->media_type == MEDIA_TYPE_HEVC) {
			    //HEVC placeholder
			} else {
			    fprintf(stderr,"HLSMUX: UNSUPPORTED MEDIA TYPE\n");
			}
								
			start_init_mp4_fragment(core, &hlsmux->video[source], source, 1);

			fprintf(stderr,"HLSMUX: CREATED INITIAL MP4 VIDEO FRAGMENT(%d): %p\n",
				source,
				hlsmux->video[source].fmp4->buffer);
			
			fmp4_output_header(hlsmux->video[source].fmp4);
			
			fprintf(stderr,"HLSMUX: WRITING OUT VIDEO INIT FILE - FMP4(%d): %ld\n",
				source,
				hlsmux->video[source].fmp4->buffer_offset);
			
			fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, hlsmux->video[source].output_fmp4_file);
			end_init_mp4_fragment(core, &hlsmux->video[source], source);
			
			fmp4_file_finalize(hlsmux->video[source].fmp4);			
			hlsmux->video[source].fmp4 = NULL;
		    }
		}
		frag_delta = (frame->full_time - source_data[source].start_time_video) / 90000.0;
		if (source_data[source].total_video_duration+frag_delta >= source_data[source].expected_video_duration+fragment_length) {
		    if (core->cd->enable_ts_output) {
			end_ts_fragment(core, &hlsmux->video[source], source);
		    }

		    if (core->cd->enable_fmp4_output) {				    
			if (hlsmux->video[source].fmp4) {
			    fprintf(stderr,"ENDING PREVIOUS fMP4 FRAGMENT(%d): BUFFER:%p SIZE:%d TF:%d\n",
				    source,
				    hlsmux->video[source].fmp4->buffer,
				    hlsmux->video[source].fmp4->buffer_offset,
				    hlsmux->video[source].fmp4->fragment_count);
			}
		    }
		    
		    if (core->cd->enable_fmp4_output) {
			if (hlsmux->video[source].fmp4) {
			    fmp4_fragment_end(hlsmux->video[source].fmp4);
			    fprintf(stderr,"ENDING PREVIOUS fMP4 FRAGMENT(%d): SIZE:%d\n",
				    source,
				    hlsmux->video[source].fmp4->buffer_offset);			    
			    fwrite(hlsmux->video[source].fmp4->buffer, 1, hlsmux->video[source].fmp4->buffer_offset, hlsmux->video[source].output_fmp4_file);
			    end_mp4_fragment(core, &hlsmux->video[source], source);
			}
		    }
		    
		    hlsmux_save_state(core, &source_data[0]);

		    source_data[source].segment_lengths[hlsmux->video[source].file_sequence_number] = frag_delta;
		    source_data[source].discontinuity[hlsmux->video[source].file_sequence_number] = source_data[source].source_discontinuity;
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
			}
			source_data[source].source_discontinuity = 0;
			sub_manifest_ready = 1;
		    }

		    hlsmux->video[source].file_sequence_number = (hlsmux->video[source].file_sequence_number + 1) % core->cd->rollover_size;
		    hlsmux->video[source].media_sequence_number = (hlsmux->video[source].media_sequence_number + 1);
		    
		    source_data[source].start_time_video = frame->full_time;
		    fprintf(stderr,"HLSMUX: STARTING NEW VIDEO FRAGMENT: LENGTH:%.2f  TOTAL:%.2f (NEW START:%ld)\n",
			    frag_delta, source_data[source].total_video_duration, source_data[source].start_time_video);
		    
		    source_data[source].total_video_duration += frag_delta;
		    source_data[source].expected_video_duration += fragment_length;
		    source_data[source].video_fragment_ready = 1;

		    if (core->cd->enable_ts_output) {
			start_ts_fragment(core, &hlsmux->video[source], source, 1);  // start first video fragment
		    }
		    if (core->cd->enable_fmp4_output) {
			video_stream_struct *vstream = (video_stream_struct*)core->source_stream[source].video_stream;
			
			start_mp4_fragment(core, &hlsmux->video[source], source, 1);
			if (hlsmux->video[source].fmp4 == NULL) {
			    if (frame->media_type == MEDIA_TYPE_H264) {			    
				hlsmux->video[source].fmp4 = fmp4_file_create(MEDIA_TYPE_H264,
									      90000, // timescale for H264 video
									      0x157c, // english language code
									      fragment_length);  // fragment length in seconds
				fprintf(stderr,"HLSMUX: CREATING fMP4(%d): %p\n",
					source,
					hlsmux->video[source].fmp4);				
				
				fmp4_video_track_create(hlsmux->video[source].fmp4,
							source_data[source].width,
							source_data[source].height,
							vstream->video_bitrate);
			    } else if (frame->media_type == MEDIA_TYPE_HEVC) {
				//HEVC placeholder
			    } else {
				fprintf(stderr,"HLSMUX: UNSUPPORTED MEDIA TYPE\n");				
			    }
			}
		    }		    				     
		}
	    }
	    if (core->cd->enable_fmp4_output) {	    
		if (hlsmux->video[source].output_fmp4_file != NULL) {
		    if (hlsmux->video[source].fmp4) {
			double fragment_timestamp;
			int fragment_duration;
			int64_t fragment_composition_time;

			fprintf(stderr,"ADDING VIDEO FRAGMENT(%d): %d  BUFFER:%p  OFFSET:%d\n",
				source,
				hlsmux->video[source].fmp4->fragment_count,
				hlsmux->video[source].fmp4->buffer,
				hlsmux->video[source].fmp4->buffer_offset);

			if (frame->dts > 0) {
			    //placeholder: check for overflow issue when one goes back to zero
			    fragment_composition_time = frame->pts - frame->dts;
			    fragment_timestamp = frame->dts;
			} else {
			    fragment_composition_time = 0;
			    fragment_timestamp = frame->pts;
			}

			fragment_duration = frame->duration;

			fprintf(stderr,"FRAGMENT DURATION:%d  CTS:%ld\n", fragment_duration, fragment_composition_time);
			
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

	    if (strlen(frame->lang_tag) == 3) {
		source_data[0].lang_tag[0] = frame->lang_tag[0];
		source_data[0].lang_tag[1] = frame->lang_tag[1];
		source_data[0].lang_tag[2] = frame->lang_tag[2];		
		source_data[0].lang_tag[3] = frame->lang_tag[3];
	    }
	    
	    if (source_data[source].start_time_audio == -1) {
		int64_t start_delta;

		if (source_data[source].start_time_video == -1) {
		    fprintf(stderr,"HLSMUX: VIDEO START TIME NOT SET YET\n");
		    goto skip_sample;
		}
		
		source_data[source].start_time_audio = frame->full_time;		

		if (core->cd->enable_fmp4_output) {
		    audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[0];

		    // decode the initial parameters
		    // 0xfff - 12-bits of sync code
		    uint8_t *srcdata = frame->buffer;		    
		    int audio_object_type = (*(srcdata+2) & 0xC0) >> 6;
		    int sample_freq_index = (*(srcdata+2) & 0x3C) >> 2;
		    int channel_config0 = (*(srcdata+2) & 0x01) << 2;
		    int channel_config1 = (*(srcdata+3) & 0xC0) >> 6;
		    int audio_channels = channel_config0 | channel_config1;
		    int fragment_duration;
		   
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
		    
		    hlsmux->audio[source].fmp4 = fmp4_file_create(MEDIA_TYPE_AAC,
								  astream->audio_samplerate, // timescale for AAC audio (48kHz)
								  0x15c7,                    // english language code
								  fragment_length);          // fragment length in seconds

		    fmp4_audio_track_create(hlsmux->audio[source].fmp4,
					    astream->audio_channels,
					    astream->audio_samplerate,
					    astream->audio_object_type,
					    astream->audio_bitrate);
								
		    start_init_mp4_fragment(core, &hlsmux->audio[source], source, 0);

		    fprintf(stderr,"HLSMUX: CREATED INITIAL MP4 AUDIO FRAGMENT(%d): %p\n",
			    source,
			    hlsmux->audio[source].fmp4->buffer);
		    
		    fmp4_output_header(hlsmux->audio[source].fmp4);
			
		    fprintf(stderr,"HLSMUX: WRITING OUT AUDIO INIT FILE - FMP4(%d): %ld\n",
			    source,
			    hlsmux->audio[source].fmp4->buffer_offset);
			
		    fwrite(hlsmux->audio[source].fmp4->buffer, 1, hlsmux->audio[source].fmp4->buffer_offset, hlsmux->audio[source].output_fmp4_file);
		    end_init_mp4_fragment(core, &hlsmux->audio[source], source);
			
		    fmp4_file_finalize(hlsmux->audio[source].fmp4);			
		    hlsmux->audio[source].fmp4 = NULL;

		    start_delta = source_data[source].start_time_video - source_data[source].start_time_audio;
		    fragment_duration = frame->duration * astream->audio_samplerate / 90000;
		    if (astream->audio_object_type == 5) { // sbr
			fragment_duration = fragment_duration << 1;
		    }
		    fprintf(stderr,"HLSMUX: START DELTA: %ld\n", start_delta);
		    if (start_delta < 0) {			
			// add silence audio to align samples for fmp4
			//aac_quiet_6
			//aac_quiet_2
			astream->audio_samples_to_add = (int)(((abs(start_delta) * astream->audio_samplerate / 90000.0) / fragment_duration)+0.5);
			fprintf(stderr,"HLSMUX: ADDING %d SILENT SAMPLES TO MAINTAIN A/V SYNC\n", astream->audio_samples_to_add);		    			
		    } else {
			astream->audio_samples_to_drop = (int)(((abs(start_delta) * astream->audio_samplerate / 90000.0) / fragment_duration)+0.5);
			fprintf(stderr,"HLSMUX: REMOVING %d SAMPLES TO MAINTAIN A/V SYNC\n", astream->audio_samples_to_drop);
		    }
		}		
	    }
	    frag_delta = (frame->full_time - source_data[source].start_time_audio) / 90000.0;
	    if (source_data[source].total_audio_duration+frag_delta >= source_data[source].total_video_duration && source_data[source].video_fragment_ready) {
		if (core->cd->enable_ts_output) {				    		
		    end_ts_fragment(core, &hlsmux->audio[source], source);
		}

		fprintf(stderr,"fMP4:%p\n", hlsmux->audio[source].fmp4);
		if (hlsmux->audio[source].fmp4) {
		    fprintf(stderr,"ENDING PREVIOUS fMP4 FRAGMENT(%d): BUFFER:%p SIZE:%d TF:%d\n",
			    source,
			    hlsmux->audio[source].fmp4->buffer,
			    hlsmux->audio[source].fmp4->buffer_offset,
			    hlsmux->audio[source].fmp4->fragment_count);
		}
		    
		if (core->cd->enable_fmp4_output) {
		    if (hlsmux->audio[source].fmp4) {
			fmp4_fragment_end(hlsmux->audio[source].fmp4);
			fprintf(stderr,"ENDING PREVIOUS fMP4 FRAGMENT(%d): SIZE:%d\n",
				source,
				hlsmux->audio[source].fmp4->buffer_offset);			    
			fwrite(hlsmux->audio[source].fmp4->buffer, 1, hlsmux->audio[source].fmp4->buffer_offset, hlsmux->audio[source].output_fmp4_file);
			end_mp4_fragment(core, &hlsmux->audio[source], source);
		    }
		}		
		
		source_data[source].segment_lengths[hlsmux->audio[source].file_sequence_number] = frag_delta;
		source_data[source].discontinuity[hlsmux->video[source].file_sequence_number] = source_data[source].source_discontinuity;		
		if (hlsmux->audio[source].fragments_published > core->cd->window_size) {
		    update_ts_audio_manifest(core, &hlsmux->audio[source], source,
					     source_data[source].source_discontinuity,
					     &source_data[source]);
		    if (core->cd->enable_fmp4_output) {
			update_mp4_audio_manifest(core, &hlsmux->audio[source], source,
						 source_data[source].source_discontinuity,
						 &source_data[source]);			
		    }
		    source_data[source].source_discontinuity = 0;
		    sub_manifest_ready = 1;
		}
			
		hlsmux->audio[source].file_sequence_number = (hlsmux->audio[source].file_sequence_number + 1) % core->cd->rollover_size;
		hlsmux->audio[source].media_sequence_number = (hlsmux->audio[source].media_sequence_number + 1);

		source_data[source].video_fragment_ready = 0;
		source_data[source].start_time_audio = frame->full_time;
		fprintf(stderr,"HLSMUX: STARTING NEW AUDIO FRAGMENT: LENGTH:%.2f  TOTAL:%.2f EXPECTED:%.2f (NEW START:%ld)\n",
			frag_delta,
			source_data[source].total_audio_duration,
			source_data[source].expected_audio_duration,
			source_data[source].start_time_audio);
		source_data[source].total_audio_duration += frag_delta;
		source_data[source].expected_audio_duration += fragment_length;

		if (core->cd->enable_ts_output) {				    		
		    start_ts_fragment(core, &hlsmux->audio[source], source, 0);  // start first audio fragment
		}
		if (core->cd->enable_fmp4_output) {
		    audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[0];
			
		    start_mp4_fragment(core, &hlsmux->audio[source], source, 0);
		    if (hlsmux->audio[source].fmp4 == NULL) {			    
			hlsmux->audio[source].fmp4 = fmp4_file_create(MEDIA_TYPE_AAC,
								      astream->audio_samplerate,
								      0x157c, // english language code
								      fragment_length);  // fragment length in seconds
			fprintf(stderr,"HLSMUX: CREATING fMP4(%d): %p\n",
				source,
				hlsmux->audio[source].fmp4);

			fmp4_audio_track_create(hlsmux->audio[source].fmp4,
						astream->audio_channels,
						astream->audio_samplerate,
						astream->audio_object_type,
						astream->audio_bitrate);
			
		    }
		}		    				     		
		
	    }

	    if (core->cd->enable_fmp4_output) {	    
		if (hlsmux->audio[source].output_fmp4_file != NULL) {
		    if (hlsmux->audio[source].fmp4) {
			audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[0];
			double fragment_timestamp = frame->pts;
			int fragment_duration = frame->duration * astream->audio_samplerate / 90000;

			if (astream->audio_object_type == 5) { // sbr
			    fragment_duration = fragment_duration << 1;
			}
			
			fprintf(stderr,"ADDING AUDIO FRAGMENT(%d): %d  BUFFER:%p  OFFSET:%d   DURATION:%d\n",
				source,
				hlsmux->audio[source].fmp4->fragment_count,
				hlsmux->audio[source].fmp4->buffer,
				hlsmux->audio[source].fmp4->buffer_offset,
				fragment_duration);

			if (astream->audio_samples_to_add > 0) {
			    astream->audio_samples_to_add--;

			    if (astream->audio_channels == 2) {
				fmp4_audio_fragment_add(hlsmux->audio[source].fmp4,
							aac_quiet_2,
							sizeof(aac_quiet_2),
							fragment_timestamp,
							fragment_duration);
			    } else if (astream->audio_channels == 6) {
				fmp4_audio_fragment_add(hlsmux->audio[source].fmp4,
							aac_quiet_6,
							sizeof(aac_quiet_6),
							fragment_timestamp,
							fragment_duration);				
			    }
			}
			
			fmp4_audio_fragment_add(hlsmux->audio[source].fmp4,
						frame->buffer,
						frame->buffer_size,
						fragment_timestamp,
						fragment_duration);
		    }
		}
	    }	    

	    if (core->cd->enable_ts_output) {				    	    
		if (hlsmux->audio[source].output_ts_file != NULL) {
		    int s;
		    uint8_t pat[188];
		    uint8_t pmt[188];
		    int64_t pc;
		    
		    muxpatsample(core, &hlsmux->audio[source], &pat[0]);
		    fwrite(pat, 1, 188, hlsmux->audio[source].output_ts_file);
		    muxpmtsample(core, &hlsmux->audio[source], &pmt[0], AUDIO_BASE_PID, CODEC_AAC);
		    fwrite(pmt, 1, 188, hlsmux->audio[source].output_ts_file);
		    
		    pc = muxaudiosample(core, &hlsmux->audio[source], frame, 0);
		    
		    hlsmux->audio[source].packet_count += pc;
		    for (s = 0; s < pc; s++) {
			uint8_t *muxbuffer;
			muxbuffer = hlsmux->audio[source].muxbuffer;
			if (s == 0 && (muxbuffer[5] == 0x10 || muxbuffer[5] == 0x50)) {
			    int64_t base;
			    int64_t ext;
			    int64_t full;
			    int64_t total_pc = hlsmux->audio[source].packet_count;
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
			fwrite(&muxbuffer[s*188], 1, 188, hlsmux->audio[source].output_ts_file);
		    }
		}
	    }
	}
skip_sample:	
	free(frame->buffer);
	frame->buffer = NULL;
	free(frame);
	free(msg);
    }

    for (i = 0; i < num_sources; i++) {
	free(hlsmux->video[i].pesbuffer);
	free(hlsmux->video[i].muxbuffer);
	free(hlsmux->video[i].packettable);

	free(hlsmux->audio[i].pesbuffer);
	free(hlsmux->audio[i].muxbuffer);
	free(hlsmux->audio[i].packettable);
    }
    
    return NULL;
}

	
    





