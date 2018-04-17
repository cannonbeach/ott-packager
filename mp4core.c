
#define TRACK_TYPE_VIDEO    0x00
#define TRACK_TYPE_AUDIO    0x01
#define TRACK_TYPE_CAPTION  0x02
#define MAX_TRACKS          16

typedef struct _fragment_track_struct_
    int         track_type;
} fragment_track_struct;

#define MAX_NAME_SIZE 512

typedef struct _fragment_file_struct_
{
    fragment_track_struct  track_data[MAX_TRACKS];
    char                   fragment_filename[MAX_NAME_SIZE];
    FILE                   *output_file;

    int                    track_count;
    int                    timescale;

    int                    next_track_ID;

    int64_t                sidx_buffer_offset;
    int64_t                duration_buffer_offset;
    
    int64_t                buffer_offset;
    uint8_t                *buffer;    
} fragment_file_struct;   


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

static int output24_raw(uint8_t *data, uint32_t code)
{
    *(data+0) = (code >> 16) & 0xff;
    *(data+1) = (code >> 8) & 0xff;
    *(data+2) = code & 0xff;

    return 3;
}

static int output32_raw(uint8_t *data, uint32_t code)
{
    *(data+0) = (code >> 24) & 0xff;
    *(data+1) = (code >> 16) & 0xff;
    *(data+2) = (code >> 8) & 0xff;
    *(data+3) = code & 0xff;

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

static int output24(fragment_file_struct *fmp4, uint32_t code)
{
    uint8_t *data;
    
    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output24_raw(data, code);

    return 3;
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
    
static uint32_t input32(uint8_t *data)
{
    uint32_t output = 0;
    output = *(data+0) << 24;
    output = output | (*(data+1) << 16);
    output = output | (*(data+2) << 8);
    output = output | (*(data+3) & 0xff);
    return output;
}

static int output_4cc(fragment_file_struct *fmp4, char c0, char c1, char c2, char c3)
{
    uint8_t *data;

    data = fmp4->buffer + fmp4->buffer_offset;
    fmp4->buffer_offset += output8_raw(data, c0);
    fmp4->buffer_offset += output8_raw(data, c1);
    fmp4->buffer_offset += output8_raw(data, c2);
    fmp4->buffer_offset += output8_raw(data, c3);
    
    return 4;
}

    
static int output_fmp4_ftyp(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_4cc(fmp4,'f','t','y','p');
    buffer_offset += output_4cc(fmp4,'i','s','o','m');

    buffer_offset += output32(fmp4, 0);
    buffer_offset += output_4cc(fmp4,'m','p','4','1');
    buffer_offset += output_4cc(fmp4,'d','a','s','h');
    buffer_offset += output_4cc(fmp4,'a','v','c','1');

    output32_raw(data, buffer_offset);
    
    return buffer_offset;
}

static int output_fmp4_styp(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;

    data = fmp4->buffer + fmp4->buffer_offset;

    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_4cc(fmp4,'s','t','y','p');
    buffer_offset += output_4cc(fmp4,'i','s','o','m');			   

    buffer_offset += output32(fmp4, 0);
    buffer_offset += output_4cc(fmp4,'m','p','4','1');
    buffer_offset += output_4cc(fmp4,'d','a','s','h');
    buffer_offset += output_4cc(fmp4,'a','v','c','1');

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
    buffer_offset += output_4cc(fmp4,'m','v','h','d');
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
    //next_track_ID
    buffer_offset += output32(fmp4, fmp4->track_track_ID);
    
    output32_raw(data, buffer_offset);

    return buffer_offset;    
}

static int output_fmp4_trak(fragment_file_struct *fmp4, int track_type, int track_id)
{
    return 0;
}

static int output_fmp4_moov(fragment_file_struct *fmp4)
{
    uint8_t *data;
    int buffer_offset;
    fragment_track_struct *fmp4_tracks;
    int current_track;

    data = fmp4->buffer + fmp4->buffer_offset;
    
    buffer_offset = output32(fmp4, 0);
    buffer_offset += output_4cc(fmp4,'m','o','o','v');
    buffer_offset += output_fmp4_mvhd(fmp4);

    for (current_track = 0; current_track < fmp4->track_count; current_track++) {
	buffer_offset += output_fmp4_trak(fmp4,
					  fmp4->track_data[current_track].track_type,
					  fmp4->track_data[current_track].track_id);
    }
    
    buffer_offset += output_fmp4_mvex(fmp4);

    output32_raw(data, buffer_offset);

    return buffer_offset;
}
    


