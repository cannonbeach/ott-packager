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

#include "fillet.h"
#include "mp4core.h"

static int output8_raw(uint8_t *data, uint8_t code)
{
    *(data+0) = code;

    return 1;
}

static int output16_raw(uint8_t *data, uint16_t code)
{
    *(data+0) = (code >> 8) & 0xff;
    *(data+1) = code & 0xff;

    return 2;
}

static int output32_raw(uint8_t *data, uint32_t code)
{
    *(data+0) = ((uint32_t)code >> 24) & 0xff;
    *(data+1) = ((uint32_t)code >> 16) & 0xff;
    *(data+2) = ((uint32_t)code >> 8) & 0xff;
    *(data+3) = (uint32_t)code & 0xff;

    return 4;  // 32-bit unsigned int
}

static int output64_raw(uint8_t *data, uint64_t code)
{
    *(data+0) = (code >> 56) & 0xff;
    *(data+1) = (code >> 48) & 0xff;
    *(data+2) = (code >> 40) & 0xff;
    *(data+3) = (code >> 32) & 0xff;
    *(data+4) = (code >> 24) & 0xff;
    *(data+5) = (code >> 16) & 0xff;
    *(data+6) = (code >> 8) & 0xff;
    *(data+7) = code & 0xff;

    return 8; // 64-bit unsigned int
}

static int output64(fragment_file_struct *fmp4, uint64_t code)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output64_raw(data, code);

    return 8;
}

static int output32(fragment_file_struct *fmp4, uint32_t code)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output32_raw(data, code);

    return 4;
}

static int output16(fragment_file_struct *fmp4, uint32_t code)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output16_raw(data, code);

    return 2;
}

static int output8(fragment_file_struct *fmp4, uint8_t code)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output8_raw(data, code); 

    return 1;
}
    
static int output_data(fragment_file_struct *fmp4, char *tag, int tag_size)
{
    char *data;

    data = (char*)fmp4->buffer + fmp4->buffer_offset;

    memset(data, 0, tag_size);
    *(data+0) = strlen(tag);
    data++;
    memcpy(data, tag, tag_size-2);

    fmp4->buffer_offset += tag_size;

    return tag_size;
}

static int output_raw_data(fragment_file_struct *fmp4, uint8_t *raw_data, int raw_data_size)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    memcpy(data, raw_data, raw_data_size);
    fmp4->buffer_offset += raw_data_size;

    return raw_data_size;
}
 
static int output_fmp4_4cc(fragment_file_struct *fmp4, char *cc)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output8_raw(data, cc[0]);
    data = fmp4->buffer + fmp4->buffer_offset;    
    fmp4->buffer_offset += output8_raw(data, cc[1]);
    data = fmp4->buffer + fmp4->buffer_offset;    
    fmp4->buffer_offset += output8_raw(data, cc[2]);
    data = fmp4->buffer + fmp4->buffer_offset;    
    fmp4->buffer_offset += output8_raw(data, cc[3]);    
    
    return 4;
}
    
static int output_fmp4_ftyp(fragment_file_struct *fmp4, int is_video)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"ftyp");
    buffer_offset += output_fmp4_4cc(fmp4,"isom");

    buffer_offset += output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"iso8");    
    buffer_offset += output_fmp4_4cc(fmp4,"mp41");
    buffer_offset += output_fmp4_4cc(fmp4,"dash");
    if (is_video) {
        buffer_offset += output_fmp4_4cc(fmp4,"avc1");
    }
    buffer_offset += output_fmp4_4cc(fmp4,"cmfc");    

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_styp(fragment_file_struct *fmp4, int fragment_type)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"styp");
    buffer_offset += output_fmp4_4cc(fmp4,"isom");

    buffer_offset += output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"iso8");    
    buffer_offset += output_fmp4_4cc(fmp4,"mp41");
    buffer_offset += output_fmp4_4cc(fmp4,"dash");
    if (fragment_type == VIDEO_FRAGMENT) {
        buffer_offset += output_fmp4_4cc(fmp4,"avc1");
    }
    buffer_offset += output_fmp4_4cc(fmp4,"cmfc");
    
    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_sidx(fragment_file_struct *fmp4, int64_t *sidx_time, int64_t *sidx_duration, double start_time, double frag_length, track_struct *track_data, int current_track_id)
{
    uint8_t *data;
    int buffer_offset;
    uint64_t fragment_duration;
    uint32_t sidx_buffer_offset;
    uint32_t total_duration;
    int frag;
    uint32_t SAP;
    uint32_t fragment_sizes;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"sidx");
    buffer_offset += output8(fmp4, 1);  // use 64-bit values
    buffer_offset += output8(fmp4, 0);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output32(fmp4, current_track_id);
    buffer_offset += output32(fmp4, fmp4->timescale);

    // take time from tfdt+sample_duration
    if (fmp4->enable_youtube) {
        fragment_duration = (uint64_t)fmp4->timescale * (uint64_t)track_data->frag_duration * (uint64_t)track_data->sequence_number;
        if (track_data->track_type == TRACK_TYPE_VIDEO) {
            fragment_duration += track_data->fragments[0].fragment_composition_time;
        }
    } else {
        fragment_duration = (uint64_t)start_time;
    }
    buffer_offset += output64(fmp4, fragment_duration);     // time t

    *sidx_time = fragment_duration;
    buffer_offset += output64(fmp4, 0);  // first_offset
    buffer_offset += output16(fmp4, 0); // reserved = 0
    buffer_offset += output16(fmp4, 1); // reference_count

    sidx_buffer_offset = (uint32_t)track_data->sidx_buffer_offset;
    sidx_buffer_offset = sidx_buffer_offset & 0xefffffff;

    total_duration = 0;
    fragment_sizes = 0;    
    for (frag = 0; frag < track_data->fragment_count; frag++) {
	total_duration += track_data->fragments[frag].fragment_duration;
	fragment_sizes += track_data->fragments[frag].fragment_buffer_size;
    }
    fragment_sizes += sidx_buffer_offset;
    buffer_offset += output32(fmp4, fragment_sizes);

    total_duration = (uint32_t)frag_length;
    buffer_offset += output32(fmp4, total_duration);      // the total duration of the fragment

    *sidx_duration = total_duration;    

    /*if (track_data->track_type == TRACK_TYPE_VIDEO) {    
        SAP = 0x90000000;  // SAP - SAP_type = 1;
    } else {
        SAP = 0x80000000;  // SAP - SAP_type = 0;
    }*/
    SAP = 0x90000000;  // SAP - SAP_type = 1;
    buffer_offset += output32(fmp4, SAP);  
    
    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_mvhd(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    // section 8.2.2.2
    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"mvhd");
    buffer_offset += output32(fmp4, 0);  // version=0, flags=0
    buffer_offset += output32(fmp4, 0);  // creation time
    buffer_offset += output32(fmp4, 0);  // modify time
    buffer_offset += output32(fmp4, fmp4->timescale); // audio and video will be different
    // save the position of duration in the buffer for later overwrite
    fmp4->duration_buffer_offset = fmp4->buffer_offset + buffer_offset;    
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0x00010000);  // rate = typically 1.0
    buffer_offset += output16(fmp4, 0x0100);      // volume = typically, full volume
    buffer_offset += output16(fmp4, 0x0000);      // reservd
    buffer_offset += output32(fmp4, 0x00000000);  // reservd
    buffer_offset += output32(fmp4, 0x00000000);  // reservd
    //unity matrix
    buffer_offset += output32(fmp4, 0x00010000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00010000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x40000000);
    //pre_defined = 0 -- all zero
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    //next_track_id
    buffer_offset += output32(fmp4, fmp4->next_track_id);
    
    output32_raw(data, buffer_offset);

    return buffer_offset;    
}

static int output_fmp4_tkhd(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;
    uint32_t current_time = 2082844800 + time(NULL);    
                           
    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"tkhd");
    buffer_offset += output32(fmp4, 0x7); // flags, track_enabled, etc.
    buffer_offset += output32(fmp4, current_time);
    buffer_offset += output32(fmp4, current_time);
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
        buffer_offset += output32(fmp4, fmp4->video_track_id);        
    } else {
        buffer_offset += output32(fmp4, fmp4->audio_track_id);
    }
    buffer_offset += output32(fmp4, 0);  //reservd
    buffer_offset += output32(fmp4, 0);  //duration - TODO
    buffer_offset += output32(fmp4, 0);  //reservd0
    buffer_offset += output32(fmp4, 0);  //reservd1
    buffer_offset += output16(fmp4, 0);  //layer
    buffer_offset += output16(fmp4, 0);  //altgroup
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
	buffer_offset += output16(fmp4, 0);
    } else {
	buffer_offset += output16(fmp4, 0x0100);
    }
    buffer_offset += output16(fmp4, 0);  //reservd
    // also in mvhd -- why?
    buffer_offset += output32(fmp4, 0x00010000);  // unity matrix
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00010000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x00000000);
    buffer_offset += output32(fmp4, 0x40000000);
    if (track_data->track_type == TRACK_TYPE_AUDIO) {
	buffer_offset += output16(fmp4, 0);
	buffer_offset += output16(fmp4, 0);
	buffer_offset += output16(fmp4, 0);
	buffer_offset += output16(fmp4, 0);
    } else {
	buffer_offset += output16(fmp4, fmp4->video_width);
	buffer_offset += output16(fmp4, 0);
	buffer_offset += output16(fmp4, fmp4->video_height);
	buffer_offset += output16(fmp4, 0);
    }

    output32_raw(data, buffer_offset);    
        
    return buffer_offset;
}

static int output_fmp4_mdhd(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;
    uint32_t current_time = 2082844800 + time(NULL);

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "mdhd");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, current_time);  // creation_time
    buffer_offset += output32(fmp4, current_time);  // modification_time
    buffer_offset += output32(fmp4, fmp4->timescale);
    /*if (track_data->track_type == TRACK_TYPE_AUDIO) {
        buffer_offset += output32(fmp4, fmp4->audio_samplerate);        
    } else {
        buffer_offset += output32(fmp4, fmp4->timescale);
    }*/
    buffer_offset += output32(fmp4, 0); // duration?
    buffer_offset += output16(fmp4, fmp4->lang_code);
    buffer_offset += output16(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_identity(fragment_file_struct *fmp4, char *identity)
{
    char *data;
    int c;
    int buffer_offset = strlen(identity);
    
    data = (char*)fmp4->buffer + fmp4->buffer_offset;
    for (c = 0; c < buffer_offset; c++) {
	*(data+c) = *(identity+c);
    }
    fmp4->buffer_offset += buffer_offset;

    return buffer_offset;
}

static int output_fmp4_hdlr(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;
    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"hdlr");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
	buffer_offset += output_fmp4_4cc(fmp4,"vide");
    } else if (track_data->track_type == TRACK_TYPE_AUDIO) {
	buffer_offset += output_fmp4_4cc(fmp4,"soun");
    } else {
	buffer_offset += output_fmp4_4cc(fmp4,"text");
    }
    buffer_offset += output32(fmp4, 0); // reservd
    buffer_offset += output32(fmp4, 0); // reservd
    buffer_offset += output32(fmp4, 0); // reservd

    if (track_data->track_type == TRACK_TYPE_VIDEO) {
	buffer_offset += output_identity(fmp4,"FILLET:VIDEO");
    } else if (track_data->track_type == TRACK_TYPE_AUDIO) {
	buffer_offset += output_identity(fmp4,"FILLET:AUDIO");
    }
    buffer_offset += output8(fmp4, 0);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_smhd(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "smhd");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output16(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_vmhd(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "vmhd");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output16(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_url(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"url ");
    buffer_offset += output32(fmp4, 1);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_dref(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"dref");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 1); // entry_count
    buffer_offset += output_fmp4_url(fmp4);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_dinf(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"dinf");
    buffer_offset += output_fmp4_dref(fmp4);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_avcc(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"avcC");
    buffer_offset += output8(fmp4, 0x01);
    buffer_offset += output8(fmp4, fmp4->video_sps[1]);
    buffer_offset += output8(fmp4, fmp4->video_sps[2]);
    buffer_offset += output8(fmp4, fmp4->video_sps[3]);

    // nal unit length - 1
    buffer_offset += output8(fmp4, 0xff);

    // todo - support for more than one SPS+PPS
    // how many sps?
    buffer_offset += output8(fmp4, 0xe1);  // the one is at the end    
    buffer_offset += output16(fmp4, fmp4->video_sps_size);
    fprintf(stderr,"FMP4: WRITING OUT SPS-SIZE:%d\n", fmp4->video_sps_size);
    buffer_offset += output_raw_data(fmp4, fmp4->video_sps, fmp4->video_sps_size);
    buffer_offset += output8(fmp4, 0x01);  // the one is open
    buffer_offset += output16(fmp4, fmp4->video_pps_size);
    fprintf(stderr,"FMP4: WRITING OUT PPS-SIZE:%d\n", fmp4->video_pps_size);
    buffer_offset += output_raw_data(fmp4, fmp4->video_pps, fmp4->video_pps_size);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_vse(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    if (fmp4->video_media_type == MEDIA_TYPE_H264) {
	buffer_offset += output_fmp4_4cc(fmp4,"avc1");
    } else if (fmp4->video_media_type == MEDIA_TYPE_HEVC) {
	buffer_offset += output_fmp4_4cc(fmp4,"hvc1");
    }

    buffer_offset += output32(fmp4, 0);  // reservd
    buffer_offset += output16(fmp4, 0);  // reservd

    buffer_offset += output16(fmp4, 1);  // data_ref_index
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output16(fmp4, 0);

    buffer_offset += output32(fmp4, 0);  // pre_defined
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);

    buffer_offset += output16(fmp4, fmp4->video_width);
    buffer_offset += output16(fmp4, fmp4->video_height);

    buffer_offset += output32(fmp4, 0x00480000);
    buffer_offset += output32(fmp4, 0x00480000);

    buffer_offset += output32(fmp4, 0);
    buffer_offset += output16(fmp4, 1);

    buffer_offset += output_data(fmp4, "FILLET", 32);
    buffer_offset += output16(fmp4, 0x0018);
    buffer_offset += output16(fmp4, 0xffff);

    if (fmp4->video_media_type == MEDIA_TYPE_H264) {
	buffer_offset += output_fmp4_avcc(fmp4);
    } else {
	// TODO
	//buffer_offset += output_fmp4_hvcc(fmp4);
    }

    output32_raw(data, buffer_offset);
    
    return buffer_offset;	
}

static int output_fmp4_esds(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"esds");
    buffer_offset += output32(fmp4, 0);
    // ES descriptor
    buffer_offset += output8(fmp4, 0x03);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x1c);  // 28 bytes

    buffer_offset += output16(fmp4, fmp4->audio_track_id);
    buffer_offset += output8(fmp4, 0x00); //version, flags

    // decoderconfig
    buffer_offset += output8(fmp4, 0x04);  //streamtype
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x14);  // 20 bytes

    buffer_offset += output8(fmp4, 0x40); //fmp4->audio_object_type);
    buffer_offset += output8(fmp4, 0x15); // stream_type
    buffer_offset += output8(fmp4, 0x00); // buffer size
    buffer_offset += output16(fmp4, 0x00);
    buffer_offset += output32(fmp4, fmp4->audio_bitrate); // max bitrate
    buffer_offset += output32(fmp4, fmp4->audio_bitrate); // avg bitrate

    // decoder specific config
    buffer_offset += output8(fmp4, 0x05);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, fmp4->audio_config_size);
    if (fmp4->audio_config_size == 2) {
	buffer_offset += output16(fmp4, fmp4->audio_config);
    } else {
	buffer_offset += output32(fmp4, fmp4->audio_config);
    }

    // SL descriptor
    buffer_offset += output8(fmp4, 0x06);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x80);
    buffer_offset += output8(fmp4, 0x01);
    buffer_offset += output8(fmp4, 0x02);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_ase(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"mp4a");
    buffer_offset += output32(fmp4, 0);  //reserved
    buffer_offset += output16(fmp4, 0);  //reserved

    buffer_offset += output16(fmp4, 1);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output16(fmp4, 0);
    buffer_offset += output32(fmp4, 0);  // reservd
    buffer_offset += output16(fmp4, fmp4->audio_channels);
    buffer_offset += output16(fmp4, 16); // 16-bit samples
    buffer_offset += output16(fmp4, 0); // pre_defined
    buffer_offset += output16(fmp4, 0); // reservd
    buffer_offset += output16(fmp4, fmp4->audio_samplerate);
    buffer_offset += output16(fmp4, 0); // reservd

    buffer_offset += output_fmp4_esds(fmp4);

    output32_raw(data, buffer_offset);

    return buffer_offset;    
}

static int output_fmp4_stsd(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"stsd");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 1);  // entry_count
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
	buffer_offset += output_fmp4_vse(fmp4);
    } else if (track_data->track_type == TRACK_TYPE_AUDIO) {
	buffer_offset += output_fmp4_ase(fmp4);
    } else {
	// TODO
    }

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_stts(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "stts");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_stsc(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "stsc");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_stsz(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "stsz");
    buffer_offset += output32(fmp4, 0);    
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_stco(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4, "stco");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_stbl(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"stbl");
    buffer_offset += output_fmp4_stsd(fmp4, track_data);
    buffer_offset += output_fmp4_stts(fmp4);
    buffer_offset += output_fmp4_stsc(fmp4);
    buffer_offset += output_fmp4_stsz(fmp4);
    buffer_offset += output_fmp4_stco(fmp4);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_trex(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"trex");
    buffer_offset += output32(fmp4, 0);
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
        buffer_offset += output32(fmp4, fmp4->video_track_id);
    } else {
        buffer_offset += output32(fmp4, fmp4->audio_track_id);        
    }
    buffer_offset += output32(fmp4, 1);
    buffer_offset += output32(fmp4, 0); //default sample duration
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, 0);

    output32_raw(data, buffer_offset);

    return buffer_offset;    
}

static int output_fmp4_mvex(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;
    int current_track;
    
    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"mvex");
    for (current_track = 0; current_track < fmp4->track_count; current_track++) {
        buffer_offset += output_fmp4_trex(fmp4, (track_struct*)&fmp4->track_data[current_track]);
    }

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_minf(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;
    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"minf");
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
	buffer_offset += output_fmp4_vmhd(fmp4);
    } else if (track_data->track_type == TRACK_TYPE_AUDIO) {
	buffer_offset += output_fmp4_smhd(fmp4);
    } else {
	buffer_offset += 0;
    }
    buffer_offset += output_fmp4_dinf(fmp4);
    buffer_offset += output_fmp4_stbl(fmp4, track_data);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_mdia(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"mdia");
    buffer_offset += output_fmp4_mdhd(fmp4, track_data);
    buffer_offset += output_fmp4_hdlr(fmp4, track_data);
    buffer_offset += output_fmp4_minf(fmp4, track_data);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_trak(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"trak");
    buffer_offset += output_fmp4_tkhd(fmp4, track_data);
    buffer_offset += output_fmp4_mdia(fmp4, track_data);

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_moov(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"moov");
    buffer_offset += output_fmp4_mvhd(fmp4);
    if (fmp4->enable_youtube) {
        int current_track;
        for (current_track = 0; current_track < fmp4->track_count; current_track++) {
            buffer_offset += output_fmp4_trak(fmp4, (track_struct*)&fmp4->track_data[current_track]);
        }
    } else {
        buffer_offset += output_fmp4_trak(fmp4, (track_struct*)&fmp4->track_data[0]);
    }
    buffer_offset += output_fmp4_mvex(fmp4);       

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_mfhd(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"mfhd");
    buffer_offset += output32(fmp4, 0);
    buffer_offset += output32(fmp4, track_data->sequence_number);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_tfhd(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"tfhd");

    if (track_data->track_type == TRACK_TYPE_VIDEO) {
        buffer_offset += output32(fmp4, 0x20000);        
        buffer_offset += output32(fmp4, fmp4->video_track_id);
    } else {
        buffer_offset += output32(fmp4, 0x2002a);        
        buffer_offset += output32(fmp4, fmp4->audio_track_id);
        buffer_offset += output32(fmp4, 1);  // default sample description index- ?
        int64_t duration90k = track_data->fragments[0].fragment_duration;
        duration90k = duration90k * 90000 / 48000;        
        buffer_offset += output32(fmp4, duration90k); // default sample duration-- fixed for now
        buffer_offset += output32(fmp4, 0); // default sample flags
    }    
    /*
    buffer_offset += output32(fmp4, 0x20000); //0x020002);
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
        buffer_offset += output32(fmp4, fmp4->video_track_id);
    } else {
        buffer_offset += output32(fmp4, fmp4->audio_track_id);
    }*/

    output32_raw(data, buffer_offset);
	
    return buffer_offset;
}

static int output_fmp4_tfdt(fragment_file_struct *fmp4, double start_time, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;
    uint64_t fragment_duration;

    data = fmp4->buffer + fmp4->buffer_offset;
    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"tfdt");
    buffer_offset += output32(fmp4, 0x01000000);
    if (!fmp4->enable_youtube) {
        if (track_data->track_type == TRACK_TYPE_VIDEO) {
            fragment_duration = (uint64_t)((int64_t)start_time - (int64_t)track_data->fragments[0].fragment_composition_time);
        } else {
            fragment_duration = (uint64_t)start_time;            
        }
    } else {
        fragment_duration = (uint64_t)fmp4->timescale * (uint64_t)track_data->frag_duration * (uint64_t)track_data->sequence_number;
    }
    buffer_offset += output64(fmp4, fragment_duration);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_trun(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    uint8_t *data_start;    
    int buffer_offset;
    int frag;
    int total_duration;

    data = fmp4->buffer + fmp4->buffer_offset;
    
    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"trun");
    if (track_data->track_type == TRACK_TYPE_VIDEO) {
	buffer_offset += output32(fmp4, 0x000f01);
    } else {
        buffer_offset += output32(fmp4, 0x000201);
	/*buffer_offset += output32(fmp4, 0x000701);*/
    }
    buffer_offset += output32(fmp4, track_data->fragment_count);
    
    data_start = data + buffer_offset;
    buffer_offset += output32(fmp4, 0);

    total_duration = 0;
    for (frag = 0; frag < track_data->fragment_count; frag++) {
        if (track_data->track_type == TRACK_TYPE_VIDEO) {
            buffer_offset += output32(fmp4, track_data->fragments[frag].fragment_duration);
            total_duration += track_data->fragments[frag].fragment_duration;
            buffer_offset += output32(fmp4, track_data->fragments[frag].fragment_buffer_size);
            if (frag == 0) {
                buffer_offset += output32(fmp4, 0x2000000);  // sync sample
            } else {
                buffer_offset += output32(fmp4, 0x1000000);
            }
            buffer_offset += output32(fmp4, track_data->fragments[frag].fragment_composition_time);
        } else {
            buffer_offset += output32(fmp4, track_data->fragments[frag].fragment_buffer_size);
        }
    }
    track_data->total_duration = total_duration;

    output32_raw(data, buffer_offset);
    track_data->sidx_buffer_offset = fmp4->buffer_offset + 8;
    output32_raw(data_start, fmp4->buffer_offset + 8 - fmp4->initial_offset);

    return buffer_offset;
}

static int output_fmp4_traf(fragment_file_struct *fmp4, double start_time, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"traf");
    buffer_offset += output_fmp4_tfhd(fmp4, track_data);
    buffer_offset += output_fmp4_tfdt(fmp4, start_time, track_data);
    buffer_offset += output_fmp4_trun(fmp4, track_data);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

static int output_fmp4_moof(fragment_file_struct *fmp4, double start_time, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"moof");
    buffer_offset += output_fmp4_mfhd(fmp4, track_data);
    buffer_offset += output_fmp4_traf(fmp4, start_time, track_data);

    output32_raw(data, buffer_offset);   
    
    return buffer_offset;
}

static int output_fmp4_mdat(fragment_file_struct *fmp4, track_struct *track_data)
{
    uint8_t *data;
    int buffer_offset;
    int frag;
    int64_t total_fragsize = 0;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_fmp4_4cc(fmp4,"mdat");

    for (frag = 0; frag < track_data->fragment_count; frag++) {
	//uint32_t *fragsize = (uint32_t*)fmp4->fragments[frag].fragment_buffer;
	//fprintf(stderr,"writing frag size: %u\n", ntohl(*fragsize));
        //total_fragsize += ntohl(*fragsize);	
	buffer_offset += output_raw_data(fmp4, track_data->fragments[frag].fragment_buffer, track_data->fragments[frag].fragment_buffer_size);
        total_fragsize += track_data->fragments[frag].fragment_buffer_size;
	free(track_data->fragments[frag].fragment_buffer);
    }
    fprintf(stderr,"writing fmp4 data: %ld\n", total_fragsize);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}

fragment_file_struct *fmp4_file_create(int media_type, int timescale, int lang_code, int frag_duration)
{
    fragment_file_struct *fmp4;
    fmp4 = (fragment_file_struct*)malloc(sizeof(fragment_file_struct));    
    if (!fmp4) {
	return NULL;
    }
    track_struct *track_data = (track_struct*)&fmp4->track_data[0];
    
    memset(fmp4, 0, sizeof(fragment_file_struct));    

    fmp4->audio_media_type = media_type;
    fmp4->video_media_type = media_type;
    fmp4->buffer = (uint8_t*)malloc(MAX_MP4_SIZE);   
    if (!fmp4->buffer) {
	free(fmp4);
	return NULL;
    }
    memset(fmp4->buffer, 0, MAX_MP4_SIZE);

    fmp4->buffer_offset = 0;
    fmp4->next_track_id = 2;
    fmp4->video_track_id = 1;
    fmp4->audio_track_id = 1;
    fmp4->track_count = 0;
    fmp4->timescale = timescale;
    fmp4->lang_code = lang_code;
    fmp4->enable_youtube = 0;

    track_data->sequence_number = 0;
    track_data->frag_duration = frag_duration;
    track_data->total_duration = 0;   

    return fmp4;      
}

fragment_file_struct *fmp4_file_create_youtube(int video_media_type, int audio_media_type, int timescale, int lang_code, int frag_duration)
{
    fragment_file_struct *fmp4;

    fmp4 = (fragment_file_struct*)malloc(sizeof(fragment_file_struct));
    if (!fmp4) {
	return NULL;
    }
    memset(fmp4, 0, sizeof(fragment_file_struct));    

    fmp4->video_media_type = video_media_type;
    fmp4->audio_media_type = audio_media_type;
    fmp4->buffer = (uint8_t*)malloc(MAX_MP4_SIZE);   
    if (!fmp4->buffer) {
	free(fmp4);
	return NULL;
    }
    memset(fmp4->buffer, 0, MAX_MP4_SIZE);

    fmp4->buffer_offset = 0;
    fmp4->next_track_id = 3;
    fmp4->video_track_id = 1;
    fmp4->audio_track_id = 2;
    fmp4->track_count = 0;
    fmp4->timescale = timescale;
    fmp4->lang_code = lang_code;
    fmp4->enable_youtube = 1;

    return fmp4;      
}
    
int fmp4_file_finalize(fragment_file_struct *fmp4)
{
    if (!fmp4) {
	return -1;
    }

    free(fmp4->buffer);
    fmp4->buffer = NULL;
    free(fmp4);

    return 0;
}
    
int fmp4_fragment_start(fragment_file_struct *fmp4)
{
    if (fmp4->enable_youtube) {
        
    }
    return 0;
}

int fmp4_fragment_end(fragment_file_struct *fmp4, int64_t *sidx_time, int64_t *sidx_duration, double start_time, double frag_length, uint32_t sequence_number, int fragment_type)
{
    fmp4->buffer_offset = 0;
    fmp4->initial_offset = output_fmp4_styp(fmp4, fragment_type);
    if (fmp4->enable_youtube) {
        if (fragment_type == VIDEO_FRAGMENT) {
            int vtid = fmp4->video_track_id-1;
            fmp4->track_data[vtid].sequence_number = sequence_number;            
            fmp4->initial_offset += output_fmp4_sidx(fmp4, sidx_time, sidx_duration, start_time, frag_length, (track_struct*)&fmp4->track_data[vtid], fmp4->video_track_id);    
            output_fmp4_moof(fmp4, start_time, (track_struct*)&fmp4->track_data[vtid]);
            output_fmp4_mdat(fmp4, (track_struct*)&fmp4->track_data[vtid]);      
            fmp4->track_data[vtid].sequence_number++;
            fmp4->track_data[vtid].fragment_count = 0;
        } else {
            int atid = fmp4->audio_track_id-1;
            fmp4->track_data[atid].sequence_number = sequence_number;                        
            fmp4->initial_offset += output_fmp4_sidx(fmp4, sidx_time, sidx_duration, start_time, frag_length, (track_struct*)&fmp4->track_data[atid], fmp4->audio_track_id);    
            output_fmp4_moof(fmp4, start_time, (track_struct*)&fmp4->track_data[atid]);
            output_fmp4_mdat(fmp4, (track_struct*)&fmp4->track_data[atid]);      
            fmp4->track_data[atid].sequence_number++;
            fmp4->track_data[atid].fragment_count = 0;
        }
    } else {
        fmp4->track_data[0].sequence_number = sequence_number;
        // video and audio track id are the same        
        fmp4->initial_offset += output_fmp4_sidx(fmp4, sidx_time, sidx_duration, start_time, frag_length, (track_struct*)&fmp4->track_data[0], fmp4->video_track_id);
        output_fmp4_moof(fmp4, start_time, (track_struct*)&fmp4->track_data[0]);
        output_fmp4_mdat(fmp4, (track_struct*)&fmp4->track_data[0]);  
        fmp4->track_data[0].sequence_number++;
        fmp4->track_data[0].fragment_count = 0;
    }
    
    return 0;
}

int fmp4_video_set_pps(fragment_file_struct *fmp4, uint8_t *pps, int pps_size)
{
    if (!fmp4) {
	return -1;
    }
    if (!pps) {
	return -1;
    }

    if (pps_size > MAX_PRIVATE_DATA_SIZE) {
	return -1;
    }
    memcpy(fmp4->video_pps, pps, pps_size);
    fmp4->video_pps_size = pps_size;
    
    return 0;
}

int fmp4_video_set_sps(fragment_file_struct *fmp4, uint8_t *sps, int sps_size)
{
    if (!fmp4) {
	return -1;
    }
    if (!sps) {
	return -1;
    }

    if (sps_size > MAX_PRIVATE_DATA_SIZE) {
	return -1;
    }
    memcpy(fmp4->video_sps, sps, sps_size);
    fmp4->video_sps_size = sps_size;
    
    return 0;
}

int fmp4_video_set_vps(fragment_file_struct *fmp4, uint8_t *vps, int vps_size)
{
    if (!fmp4) {
	return -1;
    }
    if (!vps) {
	return -1;
    }

    if (vps_size > MAX_PRIVATE_DATA_SIZE) {
	return -1;
    }
    memcpy(fmp4->video_vps, vps, vps_size);
    fmp4->video_vps_size = vps_size;
    
    return 0;
}

int fmp4_output_header(fragment_file_struct *fmp4, int is_video)
{
    if (!fmp4) {
	return -1;
    }

    output_fmp4_ftyp(fmp4, is_video);
    output_fmp4_moov(fmp4);
    
    return 0;
}    

int fmp4_video_track_create(fragment_file_struct *fmp4, int video_width, int video_height, int video_bitrate)
{
    if (!fmp4) {
	return -1;
    }
    int vtid = fmp4->video_track_id-1;

    fmp4->track_count++;    

    fmp4->track_data[vtid].fragment_count = 0;
    memset(fmp4->track_data[vtid].fragments, 0, sizeof(fmp4->track_data[vtid].fragments));

    fmp4->video_width = video_width;
    fmp4->video_height = video_height;
    fmp4->video_bitrate = video_bitrate;
    fmp4->track_data[vtid].track_type = TRACK_TYPE_VIDEO;        

    return 0;
}

uint8_t *fmp4_get_fragment(fragment_file_struct *fmp4, int *fragment_size)
{
    if (!fragment_size) {
	return NULL;
    }

    *fragment_size = fmp4->buffer_offset;
    return fmp4->buffer;
}    

int fmp4_audio_track_create(fragment_file_struct *fmp4, int audio_channels, int audio_samplerate, int audio_object_type, int audio_bitrate)
{
    if (!fmp4) {
	return -1;
    }
    int atid = fmp4->audio_track_id-1;

    fmp4->track_count++;   

    fmp4->track_data[atid].fragment_count = 0;
    memset(fmp4->track_data[atid].fragments, 0, sizeof(fmp4->track_data[atid].fragments));

    fmp4->audio_channels = audio_channels;
    fmp4->audio_samplerate = audio_samplerate;
    fmp4->audio_bitrate = audio_bitrate;
    fmp4->audio_object_type = audio_object_type;
    fmp4->subtitle_bitrate = 0;
    fmp4->track_data[atid].track_type = TRACK_TYPE_AUDIO;
   
    if (audio_channels == 2 && audio_samplerate == 48000) {
	fmp4->audio_config = 0x1190;
	fmp4->audio_config_size = 2;
    } else if (audio_channels == 6 && audio_samplerate == 48000) {
	fmp4->audio_config = 0x11b0;
	fmp4->audio_config_size = 2;	
    } else {	
	fmp4->audio_config = 0x1190;
	fmp4->audio_config_size = 2;
    }
    
    return 0;
}

static int replace_startcode_with_size(uint8_t *input_buffer, int input_buffer_size, uint8_t *output_buffer, int max_output_buffer_size)
{
    int i;
    int parsing_sample = 0;
    uint8_t *sample_buffer = output_buffer;
    int saved_position = 0;
    int sample_size;
    int read_pos = 0;
    int write_pos = 0;

    for (i = 0; i < input_buffer_size; i++) {
	if (input_buffer[read_pos+0] == 0x00 &&
	    input_buffer[read_pos+1] == 0x00 &&
	    input_buffer[read_pos+2] == 0x01) {	    
	    int nal_type = input_buffer[read_pos+3] & 0x1f;
	    if (parsing_sample) {
		sample_size = write_pos - saved_position - 4;
		/*fprintf(stderr,"DONE PARSING SAMPLE: %d  SAVED_POS:%d\n",
			sample_size,
			saved_position);*/
		*(sample_buffer+saved_position+0) = (sample_size >> 24) & 0xff;
		*(sample_buffer+saved_position+1) = (sample_size >> 16) & 0xff;
		*(sample_buffer+saved_position+2) = (sample_size >> 8) & 0xff;
		*(sample_buffer+saved_position+3) = sample_size & 0xff;
	    }
	    if (nal_type == 6) {
		int sei_type = input_buffer[read_pos+4];
		if (sei_type != 4) {
		    parsing_sample = 0;
		    read_pos += 3;
		    continue;
		}		    
	    }
	    if (nal_type == 9 || nal_type == 12) {
		parsing_sample = 0;
		read_pos += 3;	     
		continue;
	    }
            
            // enable this code to skip including sps/pps inline with the content- it doesn't hurt to have it there            
            /*if (nal_type == 7 || nal_type == 8) {  // skip out on the sps/pps
                parsing_sample = 0;
                read_pos += 3;
                continue;
            }*/
            
            if (nal_type == 7 || nal_type == 8 || nal_type == 5) {
                fprintf(stderr,"STARTING NAL TYPE: 0x%x  SAVING POS:%d\n", nal_type, write_pos);
            }
	    saved_position = write_pos;
	    *(sample_buffer+write_pos+0) = 0xf0;
	    *(sample_buffer+write_pos+1) = 0x0d;
	    *(sample_buffer+write_pos+2) = 0xf0;
	    *(sample_buffer+write_pos+3) = 0x0d;	    
	    parsing_sample = 1;
	    read_pos += 3;
	    write_pos += 4;
	} else {
	    if (parsing_sample) {
		sample_buffer[write_pos] = input_buffer[read_pos];
		write_pos++;
	    }
	    read_pos++;	    
	}
	if (read_pos == i) {
	    break;
	}
    }
    if (parsing_sample) {
	sample_size = write_pos - saved_position - 4;
	//fprintf(stderr,"DONE PARSING LAST SAMPLE: %d  SAVED_POS:%d\n", sample_size, sample_size);
	*(sample_buffer+saved_position+0) = ((uint32_t)sample_size >> 24) & 0xff;
	*(sample_buffer+saved_position+1) = ((uint32_t)sample_size >> 16) & 0xff;
	*(sample_buffer+saved_position+2) = ((uint32_t)sample_size >> 8) & 0xff;
	*(sample_buffer+saved_position+3) = (uint32_t)sample_size & 0xff;
    }    
    return write_pos;
}

int fmp4_video_fragment_add(fragment_file_struct *fmp4,
			    uint8_t *fragment_buffer,
			    int fragment_buffer_size,
			    double fragment_timestamp,
			    int fragment_duration,
			    int64_t fragment_composition_time)
{
    int vtid = fmp4->video_track_id-1;
    track_struct *track_data = (track_struct*)&fmp4->track_data[vtid];
    int frag = track_data->fragment_count;
    uint8_t *new_frag;
    int updated_fragment_buffer_size;

    if (frag >= MAX_FRAGMENTS) {
	fprintf(stderr,"MP4CORE: ERROR - EXCEEDED NUMBER OF VIDEO FRAGMENTS: %d!  VTID:%d\n", frag, vtid);
	return -1;
    }    
    
    if (frag == 0) {
	track_data->fragment_start_timestamp = fragment_timestamp * fmp4->timescale;
    }

    new_frag = (uint8_t*)malloc(fragment_buffer_size*2);

    updated_fragment_buffer_size = replace_startcode_with_size(fragment_buffer, fragment_buffer_size, new_frag, fragment_buffer_size*2);

    //uint32_t *fragsize = (uint32_t*)new_frag;
    /*fprintf(stderr,"new frag size: %u   0x%x 0x%x 0x%x 0x%x\n", ntohl(*fragsize),
      new_frag[0], new_frag[1], new_frag[2], new_frag[3]);*/
    
    track_data->fragments[frag].fragment_buffer = new_frag;
    track_data->fragments[frag].fragment_buffer_size = updated_fragment_buffer_size;
    track_data->fragments[frag].fragment_duration = fragment_duration;
    track_data->fragments[frag].fragment_timestamp = fragment_timestamp * fmp4->timescale;
    track_data->fragments[frag].fragment_composition_time = fragment_composition_time;

    track_data->fragment_count++;

    return 0;
}

int fmp4_audio_fragment_add(fragment_file_struct *fmp4,
			    uint8_t *fragment_buffer,
			    int fragment_buffer_size,
			    double fragment_timestamp,
			    int fragment_duration)
{
    uint8_t *new_frag;
    int header_size;
#define ADTS_HEADER_SIZE 7

    int atid = fmp4->audio_track_id-1;
    track_struct *track_data = (track_struct*)&fmp4->track_data[atid];
    int frag = track_data->fragment_count; 
    
    if (frag >= MAX_FRAGMENTS) {
	fprintf(stderr,"MP4CORE: ERROR - EXCEEDED NUMBER OF AUDIO FRAGMENTS: %d!  ATID:%d\n", frag, atid);
	return -1;
    }    
    
    if (frag == 0) {
	track_data->fragment_start_timestamp = fragment_timestamp * fmp4->timescale; // should this be sampling rate instead?
    }

    new_frag = (uint8_t*)malloc(fragment_buffer_size*2);

    header_size = ADTS_HEADER_SIZE + ((fragment_buffer[1] & 0x01) ? 0 : 2);  // check for crc

    fragment_buffer_size -= header_size;
    memcpy(new_frag, fragment_buffer + header_size, fragment_buffer_size);

    track_data->fragments[frag].fragment_buffer = new_frag;
    track_data->fragments[frag].fragment_buffer_size = fragment_buffer_size;
    track_data->fragments[frag].fragment_duration = fragment_duration;
    track_data->fragments[frag].fragment_timestamp = fragment_timestamp * fmp4->timescale;  // should this be sampling rate?
    track_data->fragments[frag].fragment_composition_time = 0;

    track_data->fragment_count++;

    return 0;
}

