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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <math.h>
#include <syslog.h>
#include <error.h>
#include "fillet.h"
#include "dataqueue.h"
#include "transvideo.h"

#if defined(ENABLE_TRANSCODE)

#include "../cbffmpeg/libavcodec/avcodec.h"
#include "../cbffmpeg/libswscale/swscale.h"
#include "../cbffmpeg/libavutil/pixfmt.h"
#include "../cbffmpeg/libavutil/log.h"
#include "../cbffmpeg/libavutil/imgutils.h"
#include "../cbffmpeg/libavformat/avformat.h"
#include "../cbffmpeg/libavfilter/buffersink.h"
#include "../cbffmpeg/libavfilter/buffersrc.h"
#include "../cbx264/x264.h"
#include "../x265_3.0/source/x265.h"

static volatile int video_decode_thread_running = 0;
static volatile int video_prepare_thread_running = 0;
static volatile int video_encode_thread_running = 0;
static volatile int video_thumbnail_thread_running = 0;
static pthread_t video_decode_thread_id;
static pthread_t video_prepare_thread_id;
static pthread_t video_encode_thread_id;
static pthread_t video_thumbnail_thread_id;

typedef struct _x264_encoder_struct_ {
    x264_t           *h;
    x264_nal_t       *nal;    
    x264_param_t     param;
    x264_picture_t   pic;
    x264_picture_t   pic_out;
    int              i_nal;    
    int              y_size;
    int              uv_size;
    int              width;
    int              height;
    int64_t          frame_count_pts;
    int64_t          frame_count_dts;
} x264_encoder_struct;

typedef struct _x265_encoder_struct_ {
    x265_stats       stats;
    x265_picture     pic_orig;
    x265_picture     pic_out;
    x265_param       *param;   
    x265_picture     *pic_in;
    x265_picture     *pic_recon;
    x265_api         *api;
    x265_encoder     *encoder;
    x265_nal         *p_nal;
    x265_nal         *p_current_nal;
    int64_t          frame_count_pts;
    int64_t          frame_count_dts;
} x265_encoder_struct;

typedef struct _scale_struct_
{
    uint8_t *output_data[4];
    int     output_stride[4];
} scale_struct;

#define THUMBNAIL_WIDTH   176
#define THUMBNAIL_HEIGHT  144

int video_sink_frame_callback(fillet_app_struct *core, uint8_t *new_buffer, int sample_size, int64_t pts, int64_t dts, int source, int splice_point, int64_t splice_duration, int64_t splice_duration_remaining);

int save_frame_as_jpeg(fillet_app_struct *core, AVFrame *pFrame)
{
    AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext *jpegContext = avcodec_alloc_context3(jpegCodec);
    FILE *JPEG = NULL;
#define MAX_FILENAME_SIZE 256
    char filename[MAX_FILENAME_SIZE];
    AVPacket packet = {.data = NULL, .size = 0};
    int encodedFrame = 0;

    jpegContext->bit_rate = 250000;
    jpegContext->width = THUMBNAIL_WIDTH;
    jpegContext->height = THUMBNAIL_HEIGHT;
    jpegContext->time_base= (AVRational){1,30};
    jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
    
    avcodec_open2(jpegContext, jpegCodec, NULL);
    av_init_packet(&packet);
    avcodec_encode_video2(jpegContext, &packet, pFrame, &encodedFrame);
    
    snprintf(filename, MAX_FILENAME_SIZE-1, "/var/www/html/thumbnail%d.jpg", core->cd->identity);
    JPEG = fopen(filename, "wb");
    if (JPEG) {
        fwrite(packet.data, 1, packet.size, JPEG);
        fclose(JPEG);
    }
    av_free_packet(&packet);
    avcodec_close(jpegContext);
    return 0;
}

void *video_thumbnail_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;

    av_register_all();   
    
    while (video_thumbnail_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->thumbnail_queue);
        while (!msg && video_thumbnail_thread_running) {
            usleep(10000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->thumbnail_queue);
        }

        scale_struct *thumbnail_output = (scale_struct*)msg->buffer;

        if (thumbnail_output) {
            AVFrame *jpeg_frame;

            jpeg_frame = av_frame_alloc();

            jpeg_frame->data[0] = thumbnail_output->output_data[0];
            jpeg_frame->data[1] = thumbnail_output->output_data[1];
            jpeg_frame->data[2] = thumbnail_output->output_data[2];
            jpeg_frame->data[3] = thumbnail_output->output_data[3];
            jpeg_frame->linesize[0] = thumbnail_output->output_stride[0];
            jpeg_frame->linesize[1] = thumbnail_output->output_stride[1];
            jpeg_frame->linesize[2] = thumbnail_output->output_stride[2];
            jpeg_frame->linesize[3] = thumbnail_output->output_stride[3];
            jpeg_frame->pts = AV_NOPTS_VALUE;
            jpeg_frame->pkt_dts = AV_NOPTS_VALUE;
            jpeg_frame->pkt_pts = AV_NOPTS_VALUE;
            jpeg_frame->pkt_duration = 0;
            jpeg_frame->pkt_pos = -1;
            jpeg_frame->pkt_size = -1;
            jpeg_frame->key_frame = -1;
            jpeg_frame->sample_aspect_ratio = (AVRational){1,1};
            jpeg_frame->format = 0;
            jpeg_frame->extended_data = NULL;
            jpeg_frame->color_primaries = AVCOL_PRI_BT709;
            jpeg_frame->color_trc = AVCOL_TRC_BT709;
            jpeg_frame->colorspace = AVCOL_SPC_BT709;
            jpeg_frame->color_range = AVCOL_RANGE_JPEG;
            jpeg_frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
            jpeg_frame->flags = 0;
            jpeg_frame->channels = 0;
            jpeg_frame->channel_layout = 0;
            jpeg_frame->width = THUMBNAIL_WIDTH;
            jpeg_frame->height = THUMBNAIL_HEIGHT;
            jpeg_frame->interlaced_frame = 0;
            jpeg_frame->top_field_first = 0;

            save_frame_as_jpeg(core, jpeg_frame);
            
            av_frame_free(&jpeg_frame);            
            av_freep(&thumbnail_output->output_data[0]);
            free(thumbnail_output);
            thumbnail_output = NULL;
        }
        memory_return(core->fillet_msg_pool, msg);
    }
    return NULL;
}

void *video_encode_thread_x265(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;
    int current_queue;
    int current_encoder;
    int num_outputs = core->cd->num_outputs;
    x265_encoder_struct x265_data[MAX_TRANS_OUTPUTS];

    for (current_encoder = 0; current_encoder < num_outputs; current_encoder++) {
        x265_data[current_encoder].api = NULL;
        x265_data[current_encoder].encoder = NULL;
        x265_data[current_encoder].param = NULL;
        x265_data[current_encoder].pic_in = (x265_picture*)&x265_data[current_encoder].pic_orig;
        x265_data[current_encoder].pic_recon = (x265_picture*)&x265_data[current_encoder].pic_out;
        x265_data[current_encoder].p_nal = NULL;
        x265_data[current_encoder].p_current_nal = NULL;
        x265_data[current_encoder].frame_count_pts = 1;
        x265_data[current_encoder].frame_count_dts = 0;
    }

    while (video_encode_thread_running) {
        // loop across the input queues and feed the encoders
        for (current_queue = 0; current_queue < num_outputs; current_queue++) {
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
            while (!msg && video_encode_thread_running) {
                usleep(100);
                msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
            }

            //Managing all of the encoding from a single thread to keep the output synchronized prior to the sorted queue
            //If performance becomes an issue, I can separate these into individual threads which will run better especially
            //with extensive use of yuv scaling and memcpy.
            int current_encoder = current_queue;
            int output_width = core->cd->transvideo_info[current_encoder].width;
            int output_height = core->cd->transvideo_info[current_encoder].height;
            uint32_t sar_width;
            uint32_t sar_height;            
            
            if (!x265_data[current_encoder].encoder) {
                x265_data[current_encoder].api = x265_api_get(8); // only 8-bit for now
                if (!x265_data[current_encoder].api) {
                    x265_data[current_encoder].api = x265_api_get(0);
                }
                x265_data[current_encoder].param = x265_data[current_encoder].api->param_alloc();

                memset(x265_data[current_encoder].param, 0, sizeof(x265_param));
                x265_param_default(x265_data[current_encoder].param);

                x265_data[current_encoder].param->bEmitInfoSEI = 0;
                x265_data[current_encoder].param->internalCsp = X265_CSP_I420; //8-bit/420
                x265_data[current_encoder].param->internalBitDepth = 8;
                x265_data[current_encoder].param->bHighTier = 0;
                x265_data[current_encoder].param->bRepeatHeaders = 1;
                x265_data[current_encoder].param->bAnnexB = 1;                
                x265_data[current_encoder].param->sourceWidth = output_width;
                x265_data[current_encoder].param->sourceHeight = output_height;
                x265_data[current_encoder].param->limitTU = 0;
                x265_data[current_encoder].param->logLevel = X265_LOG_DEBUG;
                x265_data[current_encoder].param->bEnableWavefront = 1;
                x265_data[current_encoder].param->bOpenGOP = 0;                
                x265_data[current_encoder].param->fpsNum = msg->fps_num;
                x265_data[current_encoder].param->fpsDenom = msg->fps_den;                
                x265_data[current_encoder].param->bDistributeModeAnalysis = 0;
                x265_data[current_encoder].param->bDistributeMotionEstimation = 0;
                x265_data[current_encoder].param->scenecutThreshold = 0;
                x265_data[current_encoder].param->bframes = 2;  // set configuration
                x265_data[current_encoder].param->bBPyramid = 0; // set configuration
                x265_data[current_encoder].param->bFrameAdaptive = 0;                
                x265_data[current_encoder].param->rc.rateControlMode = X265_RC_ABR;
                x265_data[current_encoder].param->rc.bitrate = core->cd->transvideo_info[current_encoder].video_bitrate;
                x265_data[current_encoder].param->rc.vbvBufferSize = core->cd->transvideo_info[current_encoder].video_bitrate;
                x265_data[current_encoder].param->bEnableAccessUnitDelimiters = 1;                
                x265_data[current_encoder].param->frameNumThreads = 0;
                x265_data[current_encoder].param->interlaceMode = 0; // nope!
                x265_data[current_encoder].param->levelIdc = 0;
                x265_data[current_encoder].param->bEnableRectInter = 0;
                x265_data[current_encoder].param->bEnableAMP = 0;
                x265_data[current_encoder].param->bEnablePsnr = 0;
                x265_data[current_encoder].param->bEnableSsim = 0;
                x265_data[current_encoder].param->bEnableStrongIntraSmoothing = 1;
                x265_data[current_encoder].param->bEnableWeightedPred = 0; // simple
                x265_data[current_encoder].param->bEnableTemporalMvp = 0;
                x265_data[current_encoder].param->bEnableLoopFilter = 1;
                x265_data[current_encoder].param->bEnableSAO = 1;
                x265_data[current_encoder].param->bEnableFastIntra = 1;                
                x265_data[current_encoder].param->decodedPictureHashSEI = 0;
                x265_data[current_encoder].param->bLogCuStats = 0;
                x265_data[current_encoder].param->lookaheadDepth = 15;
                x265_data[current_encoder].param->lookaheadSlices = 5;
                
                if (output_width < 960) {  // sd/hd?
                    x265_data[current_encoder].param->maxCUSize = 16;
                    x265_data[current_encoder].param->minCUSize = 8;               
                } else {
                    x265_data[current_encoder].param->maxCUSize = 32;
                    x265_data[current_encoder].param->minCUSize = 16;
                }

                x265_data[current_encoder].param->rdLevel = 1; // very simple... will make quality profiles
                x265_data[current_encoder].param->bEnableEarlySkip = 1;
                x265_data[current_encoder].param->searchMethod = X265_DIA_SEARCH; // simple
                x265_data[current_encoder].param->subpelRefine = 1; // simple
                x265_data[current_encoder].param->maxNumMergeCand = 2;
                x265_data[current_encoder].param->maxNumReferences = 1;

                // these really need to be tuned
                if (core->cd->transvideo_info[current_encoder].encoder_quality == ENCODER_QUALITY_LOW) {
                    // low is default
                } else if (core->cd->transvideo_info[current_encoder].encoder_quality == ENCODER_QUALITY_MEDIUM) {
                    x265_data[current_encoder].param->rdLevel = 3;
                    x265_data[current_encoder].param->bEnableEarlySkip = 1;
                    x265_data[current_encoder].param->searchMethod = X265_DIA_SEARCH; 
                    x265_data[current_encoder].param->subpelRefine = 3;
                    x265_data[current_encoder].param->maxNumMergeCand = 2;
                    x265_data[current_encoder].param->maxNumReferences = 2;                    
                } else if (core->cd->transvideo_info[current_encoder].encoder_quality == ENCODER_QUALITY_HIGH) {
                    x265_data[current_encoder].param->rdLevel = 5;
                    x265_data[current_encoder].param->bEnableEarlySkip = 1;
                    x265_data[current_encoder].param->searchMethod = X265_DIA_SEARCH; 
                    x265_data[current_encoder].param->subpelRefine = 5;
                    x265_data[current_encoder].param->maxNumMergeCand = 2;
                    x265_data[current_encoder].param->maxNumReferences = 3;
                    x265_data[current_encoder].param->bframes = 3;  // set configuration
                } else {  // ENCODER_QUALITY_CRAZY
                    x265_data[current_encoder].param->rdLevel = 5;
                    x265_data[current_encoder].param->bEnableEarlySkip = 1;
                    x265_data[current_encoder].param->searchMethod = X265_DIA_SEARCH; 
                    x265_data[current_encoder].param->subpelRefine = 5;
                    x265_data[current_encoder].param->maxNumMergeCand = 2;
                    x265_data[current_encoder].param->maxNumReferences = 3;
                    x265_data[current_encoder].param->bframes = 4;
                }                

                // vui factors
                x265_data[current_encoder].param->vui.aspectRatioIdc = X265_EXTENDED_SAR;
                x265_data[current_encoder].param->vui.videoFormat = 5;

                if (core->cd->transvideo_info[0].aspect_num > 0 &&
                    core->cd->transvideo_info[0].aspect_den > 0) {
                    sar_width = output_height*core->cd->transvideo_info[0].aspect_num;
                    sar_height = output_width*core->cd->transvideo_info[0].aspect_den;      
                } else {
                    sar_width = output_height*msg->aspect_num;
                    sar_height = output_width*msg->aspect_den;
                }

                x265_data[current_encoder].param->vui.sarWidth = sar_width;
                x265_data[current_encoder].param->vui.sarHeight = sar_height;
                x265_data[current_encoder].param->vui.transferCharacteristics = 2;
                x265_data[current_encoder].param->vui.matrixCoeffs = 2;                
                x265_data[current_encoder].param->vui.bEnableOverscanAppropriateFlag = 0;
                x265_data[current_encoder].param->vui.bEnableVideoSignalTypePresentFlag = 0;
                x265_data[current_encoder].param->vui.bEnableVideoFullRangeFlag = 1;
                x265_data[current_encoder].param->vui.bEnableColorDescriptionPresentFlag = 1;
                x265_data[current_encoder].param->vui.bEnableDefaultDisplayWindowFlag = 0;
                x265_data[current_encoder].param->vui.bEnableChromaLocInfoPresentFlag = 0;
                x265_data[current_encoder].param->vui.chromaSampleLocTypeTopField = 0;
                x265_data[current_encoder].param->vui.chromaSampleLocTypeBottomField = 0;
                x265_data[current_encoder].param->vui.defDispWinLeftOffset = 0;
                x265_data[current_encoder].param->vui.defDispWinTopOffset = 0;                
                x265_data[current_encoder].param->vui.defDispWinRightOffset = output_width;
                x265_data[current_encoder].param->vui.defDispWinBottomOffset = output_height;
               
                // rate control factors
                x265_data[current_encoder].param->rc.qgSize = 16;                
                x265_data[current_encoder].param->rc.vbvBufferInit = 0.9;
                x265_data[current_encoder].param->rc.rfConstant = 26;
                x265_data[current_encoder].param->rc.qpStep = 4;
                x265_data[current_encoder].param->rc.qp = 28;
                x265_data[current_encoder].param->rc.cuTree = 0;
                x265_data[current_encoder].param->rc.complexityBlur = 25;
                x265_data[current_encoder].param->rc.qblur = 0.6;                
                x265_data[current_encoder].param->rc.qCompress = 0.65;
                x265_data[current_encoder].param->rc.pbFactor = 1.2f;                
                x265_data[current_encoder].param->rc.ipFactor = 1.4f;                
                x265_data[current_encoder].param->rc.bStrictCbr = 1;
                
                // let's put the idr frames at second boundaries - make this configurable later
                double fps = 30.0;
                if (msg->fps_den > 0) {
                    fps = (double)((double)msg->fps_num / (double)msg->fps_den);
                }
                x265_data[current_encoder].param->keyframeMax = (int)((double)fps + 0.5);
                x265_data[current_encoder].param->keyframeMin = 1;                

                // start it up
                x265_data[current_encoder].encoder = x265_data[current_encoder].api->encoder_open(x265_data[current_encoder].param);
                x265_data[current_encoder].api->encoder_parameters(x265_data[current_encoder].encoder,
                                                                   x265_data[current_encoder].param);

                x265_data[current_encoder].api->picture_init(x265_data[current_encoder].param,
                                                             x265_data[current_encoder].pic_in);

                fprintf(stderr,"status: hevc encoder initialized\n");
            }

            if (!video_encode_thread_running) {
                while (msg) {
                    if (msg) {
                        memory_return(core->raw_video_pool, msg->buffer);
                        msg->buffer = NULL;
                        memory_return(core->fillet_msg_pool, msg);
                        msg = NULL;
                    }
                    msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
                }
                goto cleanup_video_encode_thread;
            }

            uint8_t *video;
            int output_size;
            int64_t pts;
            int64_t dts;
            int splice_point = 0;
            int64_t splice_duration = 0;
            int64_t splice_duration_remaining = 0;
            int owhalf = output_width / 2;
            int ohhalf = output_height / 2;
            int frames;
            uint32_t nal_count;

            video = msg->buffer;
            splice_point = msg->splice_point;
            splice_duration = msg->splice_duration;
            splice_duration_remaining = msg->splice_duration_remaining;

            // if splice point- force intra frame
            // when duration is done- also force intra frame
            // keep track of splice duration remaining
            fprintf(stderr,"VIDEO ENCODER: SPLICE POINT:%d  SPLICE DURATION: %ld  SPLICE DURATION REMAINING: %ld\n",
                    splice_point,
                    splice_duration,
                    splice_duration_remaining);
            

            x265_data[current_encoder].pic_in->colorSpace = X265_CSP_I420;
            x265_data[current_encoder].pic_in->bitDepth = 8;
            x265_data[current_encoder].pic_in->stride[0] = output_width;
            x265_data[current_encoder].pic_in->stride[1] = owhalf;
            x265_data[current_encoder].pic_in->stride[2] = owhalf;
            x265_data[current_encoder].pic_in->planes[0] = video;
            x265_data[current_encoder].pic_in->planes[1] = video + (output_width * output_height);
            x265_data[current_encoder].pic_in->planes[2] = x265_data[current_encoder].pic_in->planes[1] + (owhalf*ohhalf);
            x265_data[current_encoder].pic_in->pts = x265_data[current_queue].frame_count_pts;

            nal_count = 0;
            frames = x265_data[current_encoder].api->encoder_encode(x265_data[current_encoder].encoder,
                                                                    &x265_data[current_encoder].p_nal,
                                                                    &nal_count,
                                                                    x265_data[current_encoder].pic_in,
                                                                    x265_data[current_encoder].pic_recon);

            x265_data[current_queue].frame_count_pts++;

            if (frames > 0) {
                uint8_t *nal_buffer;
                double output_fps = 30000.0/1001.0;
                double ticks_per_frame_double = (double)90000.0/(double)output_fps;
                video_stream_struct *vstream = (video_stream_struct*)core->source_stream[0].video_stream;  // only one source stream
                int nal_idx;
                x265_nal *nalout;
                int pos = 0;
                int nalsize = 0;

                nalout = x265_data[current_encoder].p_nal;
                for (nal_idx = 0; nal_idx < nal_count; nal_idx++) {
                    nalsize += nalout->sizeBytes;
                    nalout++;
                }

                nalout = x265_data[current_encoder].p_nal;
                nal_buffer = (uint8_t*)memory_take(core->compressed_video_pool, nalsize);
                if (!nal_buffer) {
                    fprintf(stderr,"FATAL ERROR: unable to obtain nal_buffer!!\n");
                    exit(0);                    
                }
                for (nal_idx = 0; nal_idx < nal_count; nal_idx++) {
                    int nocopy = 0;

                    if (nalout->type == 35) {  // AUD-not needed right now
                        nocopy = 1;
                    }

#if defined(DEBUG_NALTYPE)                    
                    if (nalout->type == 32) {
                        syslog(LOG_INFO,"HEVC VIDEO FRAME: VPS\n");
                    } else if (nalout->type == 33) {
                        syslog(LOG_INFO,"HEVC VIDEO FRAME: SPS\n");
                    } else if (nalout->type == 34) {
                        syslog(LOG_INFO,"HEVC VIDEO FRAME: PPS\n");
                    } else if (nalout->type == 35) {
                        syslog(LOG_INFO,"HEVC VIDEO FRAME: AUD\n");
                        nocopy = 1;
                    } else if (nalout->type == 19 || nalout->type == 20) {
                        syslog(LOG_INFO,"HEVC VIDEO FRAME: IDR\n");
                    } else {
                        syslog(LOG_INFO,"HEVC VIDEO FRAME: 0x%x (SIZE:%d)\n", nalout->type, nalout->sizeBytes);
                    }
#endif // DEBUG_NALTYPE                    

                    if (!nocopy) {
                        memcpy(nal_buffer + pos, nalout->payload, nalout->sizeBytes);
                        pos += nalout->sizeBytes;
                    } else {
                        // skip aud
                    }
                    
                    nalout++;
                }
                nalsize = pos;
                //nalsize is the size of the nal unit
                
                if (x265_data[current_encoder].param->fpsDenom > 0) {
                    output_fps = (double)x265_data[current_encoder].param->fpsNum / (double)x265_data[current_encoder].param->fpsDenom;
                    if (output_fps > 0) {
                        ticks_per_frame_double = (double)90000.0/(double)output_fps;
                    }
                }
                
                /*encoder_opaque_struct *opaque_output = (encoder_opaque_struct*)x264_data[current_queue].pic_out.opaque;
                int64_t opaque_int64 = 0;
                if (opaque_output) {
                    splice_point = opaque_output->splice_point;
                    splice_duration = opaque_output->splice_duration;
                    splice_duration_remaining = opaque_output->splice_duration_remaining;
                    opaque_int64 = opaque_output->frame_count_pts;            
                    //int64_t opaque_int64 = (int64_t)x264_data[current_queue].pic_out.opaque;                    
                }
                double opaque_double = (double)opaque_int64;
                */

                double opaque_double = (double)x265_data[current_queue].pic_recon->pts;
                pts = (int64_t)((double)opaque_double * (double)ticks_per_frame_double) + (int64_t)vstream->first_timestamp;
                dts = (int64_t)((double)x265_data[current_queue].frame_count_dts * (double)ticks_per_frame_double) + (int64_t)vstream->first_timestamp;

                x265_data[current_queue].frame_count_dts++;
                //need to pass this data through
                splice_point = 0;
                splice_duration = 0;
                splice_duration_remaining = 0;
                output_size = nalsize;

#if defined(DEBUG_NALTYPE)                
                syslog(LOG_INFO,"DELIVERING HEVC ENCODED VIDEO FRAME: %d   PTS:%ld  DTS:%ld\n",
                       output_size,
                       pts, dts);
#endif                
                
                video_sink_frame_callback(core, nal_buffer, output_size, pts, dts, current_queue, splice_point, splice_duration, splice_duration_remaining);
            }
                   
            if (msg) {
                memory_return(core->raw_video_pool, msg->buffer);
                msg->buffer = NULL;
                memory_return(core->fillet_msg_pool, msg);
                msg = NULL;                
            }                        
        }
    }

cleanup_video_encode_thread:

    for (current_encoder = 0; current_encoder < num_outputs; current_encoder++) {
        //x265_data[current_encoder].api->encoder_close();        
    }
    
    return NULL;
}

typedef struct _encoder_opaque_struct_ {
    int         splice_point;
    int64_t     splice_duration;
    int64_t     splice_duration_remaining;
    int64_t     frame_count_pts;
} encoder_opaque_struct;

void *video_encode_thread_x264(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;
    int current_queue;
    int current_encoder;
    int num_outputs = core->cd->num_outputs;
    x264_encoder_struct x264_data[MAX_TRANS_OUTPUTS];
#define MAX_SEI_PAYLOAD_SIZE 512    

    for (current_encoder = 0; current_encoder < num_outputs; current_encoder++) {
        x264_data[current_encoder].h = NULL;
        x264_data[current_encoder].frame_count_pts = 1;
        x264_data[current_encoder].frame_count_dts = 0;
    }
    
    while (video_encode_thread_running) {
        // loop across the input queues and feed the encoders
        for (current_queue = 0; current_queue < num_outputs; current_queue++) {
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
            while (!msg && video_encode_thread_running) {
                usleep(100);
                msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
            }

            //Managing all of the encoding from a single thread to keep the output synchronized prior to the sorted queue
            //If performance becomes an issue, I can separate these into individual threads which will run better especially
            //with extensive use of yuv scaling and memcpy.
            int current_encoder = current_queue;
            
            if (!x264_data[current_encoder].h) {
                int output_width = core->cd->transvideo_info[current_encoder].width;
                int output_height = core->cd->transvideo_info[current_encoder].height;
                uint32_t sar_width;
                uint32_t sar_height;
                
                x264_data[current_encoder].width = output_width;
                x264_data[current_encoder].height = output_height;
                x264_param_default_preset(&x264_data[current_encoder].param,
                                          "medium",
                                          NULL);
                
                x264_data[current_encoder].y_size = output_width * output_height;
                x264_data[current_encoder].uv_size = (output_width * output_height) / 4;
                
                x264_data[current_encoder].param.i_csp = X264_CSP_I420;
                x264_data[current_encoder].param.i_width = output_width;
                x264_data[current_encoder].param.i_height = output_height;
                x264_picture_alloc(&x264_data[current_encoder].pic,
                                   x264_data[current_encoder].param.i_csp,
                                   output_width,
                                   output_height);
                
                x264_data[current_encoder].param.b_interlaced = 0;
                x264_data[current_encoder].param.b_fake_interlaced = 0;
                x264_data[current_encoder].param.b_deterministic = 1;
                x264_data[current_encoder].param.b_vfr_input = 0;
                x264_data[current_encoder].param.b_repeat_headers = 1;
                x264_data[current_encoder].param.b_annexb = 1;
                x264_data[current_encoder].param.b_aud = 1;
                x264_data[current_encoder].param.b_open_gop = 0;
                x264_data[current_encoder].param.b_sliced_threads = 0;
                
                x264_data[current_encoder].param.rc.i_lookahead = 15;

                // some people may want the filler bits, but for HLS/DASH, it's just extra baggage to carry around
                x264_data[current_encoder].param.rc.b_filler = 0; // no filler- less bits
                x264_data[current_encoder].param.rc.i_aq_mode = X264_AQ_VARIANCE;
                x264_data[current_encoder].param.rc.f_aq_strength = 1.0;
                x264_data[current_encoder].param.rc.b_mb_tree = 1;
                x264_data[current_encoder].param.rc.i_rc_method = X264_RC_ABR;
                x264_data[current_encoder].param.rc.i_bitrate = core->cd->transvideo_info[current_encoder].video_bitrate;
                x264_data[current_encoder].param.rc.i_vbv_buffer_size = core->cd->transvideo_info[current_encoder].video_bitrate;
                x264_data[current_encoder].param.rc.i_vbv_max_bitrate = core->cd->transvideo_info[current_encoder].video_bitrate;
                
                x264_data[current_encoder].param.analyse.i_me_method = X264_ME_HEX;
                x264_data[current_encoder].param.analyse.i_subpel_refine = 3;
                x264_data[current_encoder].param.analyse.b_dct_decimate = 1;
                
                x264_data[current_encoder].param.i_nal_hrd = X264_NAL_HRD_CBR;
                x264_data[current_encoder].param.i_threads = 0;
                x264_data[current_encoder].param.i_lookahead_threads = 0;                
                x264_data[current_encoder].param.i_slice_count = 0;
                x264_data[current_encoder].param.i_frame_reference = 1;

                if (core->cd->transvideo_info[0].aspect_num > 0 &&
                    core->cd->transvideo_info[0].aspect_den > 0) {
                    sar_width = output_height*core->cd->transvideo_info[0].aspect_num;
                    sar_height = output_width*core->cd->transvideo_info[0].aspect_den;      
                } else {
                    sar_width = output_height*msg->aspect_num;
                    sar_height = output_width*msg->aspect_den;
                }
                if (sar_width > 0 && sar_height > 0) {
                    x264_data[current_encoder].param.vui.i_sar_width = sar_width;
                    x264_data[current_encoder].param.vui.i_sar_height = sar_height;
                }                
                
                // let's put the idr frames at second boundaries - make this configurable later
                double fps = 30.0;
                if (msg->fps_den > 0) {
                    fps = (double)((double)msg->fps_num / (double)msg->fps_den);
                }
                x264_data[current_encoder].param.i_keyint_max = (int)((double)fps + 0.5);
                x264_data[current_encoder].param.i_keyint_min = 1;
                x264_data[current_encoder].param.i_fps_num = msg->fps_num;
                x264_data[current_encoder].param.i_fps_den = msg->fps_den;

                // i was not able to get alignment with scene change detection enabled
                // because at different resolutions it seems that the scene change
                // detector gets triggered and other resolutions it doesn't
                // so we end up with different idr sync points
                // to solve the scenecut issue, we can put the scene detector
                // outside of the encoder and then tell all of the encoders
                // at once to put in an idr sync frame
                // anyone want to write some scene change detection code?
                x264_data[current_encoder].param.i_scenecut_threshold = 0;  // need to keep abr alignment
                x264_data[current_encoder].param.i_bframe_adaptive = 0;                
                x264_data[current_encoder].param.i_bframe_pyramid = 0;
                
                // we set a base quality above and then modify things here
                if (core->cd->transvideo_info[current_encoder].encoder_quality == ENCODER_QUALITY_LOW) {
                    x264_data[current_encoder].param.analyse.i_me_method = X264_ME_DIA;
                    x264_data[current_encoder].param.analyse.i_subpel_refine = 3;                    
                    x264_data[current_encoder].param.i_frame_reference = 1;
                    x264_data[current_encoder].param.rc.i_lookahead = (int)(fps+0.5)/2;
                    x264_data[current_encoder].param.i_bframe = 0;
                } else if (core->cd->transvideo_info[current_encoder].encoder_quality == ENCODER_QUALITY_MEDIUM) {
                    x264_data[current_encoder].param.analyse.i_me_method = X264_ME_HEX;
                    x264_data[current_encoder].param.analyse.i_subpel_refine = 5;                    
                    x264_data[current_encoder].param.i_frame_reference = 3;
                    x264_data[current_encoder].param.rc.i_lookahead = (int)(fps+0.5);
                    x264_data[current_encoder].param.i_bframe = 2;                    
                } else if (core->cd->transvideo_info[current_encoder].encoder_quality == ENCODER_QUALITY_HIGH) {
                    x264_data[current_encoder].param.analyse.i_me_method = X264_ME_HEX;
                    x264_data[current_encoder].param.analyse.i_subpel_refine = 7;
                    x264_data[current_encoder].param.i_frame_reference = 3;
                    x264_data[current_encoder].param.rc.i_lookahead = (int)(fps+0.5);
                    x264_data[current_encoder].param.i_bframe = 2;
                } else {  // ENCODER_QUALITY_CRAZY
                    x264_data[current_encoder].param.analyse.i_me_method = X264_ME_UMH;
                    x264_data[current_encoder].param.analyse.i_subpel_refine = 8;
                    x264_data[current_encoder].param.i_frame_reference = 5;
                    x264_data[current_encoder].param.rc.i_lookahead = (int)(fps+0.5)*2;
                    x264_data[current_encoder].param.i_bframe = 3;
                }

//#define ENABLE_SLICED_THREADS
#if defined(ENABLE_SLICED_THREADS)
                x264_data[current_encoder].param.b_sliced_threads = 0;

                // these values were derived from testing across several different cloud instances- seem 
                if (output_width > 960 && output_height > 540) {                    
                    x264_data[current_encoder].param.i_threads = 12;
                    x264_data[current_encoder].param.i_slice_count = 8;
                } else {
                    x264_data[current_encoder].param.i_threads = 6;
                    x264_data[current_encoder].param.i_slice_count = 4;
                }
#endif

                if (core->cd->transvideo_info[current_encoder].encoder_profile == ENCODER_PROFILE_BASE) {                    
                    x264_param_apply_profile(&x264_data[current_encoder].param,"baseline");
                    x264_data[current_encoder].param.b_cabac = 0;
                    x264_data[current_encoder].param.i_cqm_preset = X264_CQM_FLAT;
                    x264_data[current_encoder].param.i_bframe = 0;
                    x264_data[current_encoder].param.analyse.i_weighted_pred = 0;
                    x264_data[current_encoder].param.analyse.b_weighted_bipred = 0;                    
                    x264_data[current_encoder].param.analyse.b_transform_8x8 = 0;                    
                } else if (core->cd->transvideo_info[current_encoder].encoder_profile == ENCODER_PROFILE_MAIN) {
                    x264_param_apply_profile(&x264_data[current_encoder].param,"main");
                    x264_data[current_encoder].param.i_cqm_preset = X264_CQM_FLAT;                    
                    x264_data[current_encoder].param.analyse.b_transform_8x8 = 0;                    
                } else {
                    x264_param_apply_profile(&x264_data[current_encoder].param,"high");
                    x264_data[current_encoder].param.analyse.b_transform_8x8 = 1;
                }
                
                x264_data[current_encoder].h = x264_encoder_open(&x264_data[current_encoder].param);
            }
                        
            if (!video_encode_thread_running) {
                while (msg) {
                    if (msg) {
                        memory_return(core->raw_video_pool, msg->buffer);
                        msg->buffer = NULL;
                        memory_return(core->fillet_msg_pool, msg);
                        msg = NULL;                                        
                    }
                    msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
                }
                goto cleanup_video_encode_thread;
            }

            uint8_t *video;
            int output_size;
            int64_t pts;
            int64_t dts;
            int splice_point = 0;
            int64_t splice_duration = 0;
            int64_t splice_duration_remaining = 0;

            video = msg->buffer;
            splice_point = msg->splice_point;
            splice_duration = msg->splice_duration;
            splice_duration_remaining = msg->splice_duration_remaining;

            // if splice point- force intra frame
            // when duration is done- also force intra frame
            // keep track of splice duration remaining
            fprintf(stderr,"VIDEO ENCODER: SPLICE POINT:%d  SPLICE DURATION: %ld  SPLICE DURATION REMAINING: %ld\n",
                    splice_point,
                    splice_duration,
                    splice_duration_remaining);

            x264_data[current_queue].pic.i_pts = msg->pts;
            x264_data[current_queue].pic.i_dts = msg->dts;

            encoder_opaque_struct *opaque_data = (encoder_opaque_struct*)malloc(sizeof(encoder_opaque_struct));
            if (opaque_data) {
                opaque_data->splice_point = splice_point;
                opaque_data->splice_duration = splice_duration;
                opaque_data->splice_duration_remaining = splice_duration_remaining;
                opaque_data->frame_count_pts = x264_data[current_queue].frame_count_pts;
            }
            
            x264_data[current_queue].pic.opaque = (void*)opaque_data;
            
            memcpy(x264_data[current_queue].pic.img.plane[0],
                   video,
                   x264_data[current_queue].y_size);
            memcpy(x264_data[current_queue].pic.img.plane[1],
                   video + x264_data[current_queue].y_size,
                   x264_data[current_queue].uv_size);
            memcpy(x264_data[current_queue].pic.img.plane[2],
                   video + x264_data[current_queue].y_size + x264_data[current_queue].uv_size,
                   x264_data[current_queue].uv_size);

            if (msg->caption_buffer) {
//#define DISABLE_CAPTIONS
#if !defined(DISABLE_CAPTIONS)                
                uint8_t *caption_buffer;
                caption_buffer = (uint8_t*)malloc(MAX_SEI_PAYLOAD_SIZE);
                if (caption_buffer) {
                    memset(caption_buffer, 0, MAX_SEI_PAYLOAD_SIZE);
                    x264_data[current_queue].pic.extra_sei.payloads = (x264_sei_payload_t*)malloc(sizeof(x264_sei_payload_t));
                    //check
                    if (x264_data[current_queue].pic.extra_sei.payloads) {                    
                        memset(x264_data[current_queue].pic.extra_sei.payloads, 0, sizeof(x264_sei_payload_t));
                        caption_buffer[0] = 0xb5;
                        caption_buffer[1] = 0x00;
                        caption_buffer[2] = 0x31;
                        if (msg->caption_size > MAX_SEI_PAYLOAD_SIZE) {
                            msg->caption_size = MAX_SEI_PAYLOAD_SIZE;
                            // log truncation
                        }
                        //fprintf(stderr,"status: encoding caption buffer\n");
                        memcpy(caption_buffer+3, msg->caption_buffer, msg->caption_size);
                        x264_data[current_queue].pic.extra_sei.payloads[0].payload = caption_buffer;
                        x264_data[current_queue].pic.extra_sei.payloads[0].payload_size = msg->caption_size+3;
                        x264_data[current_queue].pic.extra_sei.num_payloads = 1;
                        x264_data[current_queue].pic.extra_sei.payloads[0].payload_type = 4;
                        x264_data[current_queue].pic.extra_sei.sei_free = free;
                    } else {
                        free(caption_buffer);
                        // fail
                        fprintf(stderr,"error: unable to allocate memory for sei payload (for captions)\n");
                    }
                } else {
                    // fail
                    fprintf(stderr,"error: unable to allocate memory for caption_buffer\n");
                }
                caption_buffer = NULL;
#endif // DISABLE_CAPTIONS                
                free(msg->caption_buffer);
                msg->caption_buffer = NULL;
                msg->caption_size = 0;
            } else {
                x264_data[current_queue].pic.extra_sei.num_payloads = 0;
                x264_data[current_queue].pic.extra_sei.payloads = NULL;
            }

            if (splice_point == 1 || splice_point == 2) {
                syslog(LOG_INFO,"SCTE35(%d)- INSERTING IDR FRAME DURING SPLICE POINT: %d\n",
                       current_queue, splice_point);
                x264_data[current_queue].pic.i_type = X264_TYPE_IDR;
            } else {
                x264_data[current_queue].pic.i_type = X264_TYPE_AUTO;
            }
            
            output_size = x264_encoder_encode(x264_data[current_queue].h,
                                              &x264_data[current_queue].nal,
                                              &x264_data[current_queue].i_nal,
                                              &x264_data[current_queue].pic,
                                              &x264_data[current_queue].pic_out);

            x264_data[current_queue].frame_count_pts++;

            if (x264_data[current_queue].i_nal > 0) {
                uint8_t *nal_buffer;
                double output_fps = 30000.0/1001.0;
                double ticks_per_frame_double = (double)90000.0/(double)output_fps;
                video_stream_struct *vstream = (video_stream_struct*)core->source_stream[0].video_stream;  // only one source stream                

                nal_buffer = (uint8_t*)memory_take(core->compressed_video_pool, output_size);
                if (!nal_buffer) {
                    fprintf(stderr,"FATAL ERROR: unable to obtain nal_buffer!!\n");
                    exit(0);                                                            
                }
                memcpy(nal_buffer, x264_data[current_queue].nal->p_payload, output_size);
                
                if (x264_data[current_encoder].param.i_fps_den > 0) {
                    output_fps = (double)x264_data[current_encoder].param.i_fps_num / (double)x264_data[current_encoder].param.i_fps_den;
                    if (output_fps > 0) {
                        ticks_per_frame_double = (double)90000.0/(double)output_fps;
                    }
                }
                
                /*
                  fprintf(stderr,"RECEIVED ENCODED FRAME OUTPUT:%d FRAME COUNT DISPLAY ORDER:%ld  CURRENT TS:%ld\n",
                  output_size,
                  (int64_t)x264_data[current_queue].pic_out.opaque,
                  (int64_t)x264_data[current_queue].pic_out.opaque * (int64_t)ticks_per_frame_double + (int64_t)vstream->first_timestamp);
                */
                
                pts = x264_data[current_queue].pic_out.i_pts;
                dts = x264_data[current_queue].pic_out.i_dts;

                encoder_opaque_struct *opaque_output = (encoder_opaque_struct*)x264_data[current_queue].pic_out.opaque;
                int64_t opaque_int64 = 0;
                if (opaque_output) {
                    splice_point = opaque_output->splice_point;
                    splice_duration = opaque_output->splice_duration;
                    splice_duration_remaining = opaque_output->splice_duration_remaining;
                    opaque_int64 = opaque_output->frame_count_pts;            
                    //int64_t opaque_int64 = (int64_t)x264_data[current_queue].pic_out.opaque;                    
                }
                double opaque_double = (double)opaque_int64;
                
                pts = (int64_t)((double)opaque_double * (double)ticks_per_frame_double) + (int64_t)vstream->first_timestamp;
                dts = (int64_t)((double)x264_data[current_queue].frame_count_dts * (double)ticks_per_frame_double) + (int64_t)vstream->first_timestamp;

                x264_data[current_queue].frame_count_dts++;
                video_sink_frame_callback(core, nal_buffer, output_size, pts, dts, current_queue, splice_point, splice_duration, splice_duration_remaining);
            }            
                   
            if (msg) {
                //check for orphaned caption buffer
                memory_return(core->raw_video_pool, msg->buffer);
                msg->buffer = NULL;
                memory_return(core->fillet_msg_pool, msg);
                msg = NULL;                                                        
            }            
        }
    }
cleanup_video_encode_thread:
    // since we're using malloc and not buffer pools yet
    // it's possible for some of the caption data to get
    // orphaned if we don't flush out the encoder
    // planning to switch over to pools so this will not become an issue       
    for (current_encoder = 0; current_encoder < num_outputs; current_encoder++) {
        if (x264_data[current_encoder].h) {
            x264_encoder_close(x264_data[current_encoder].h);
        }
    }
    
    return NULL;
}

void copy_image_data_to_ffmpeg(uint8_t *source, int source_width, int source_height, AVFrame *source_frame)
{    
    int row;
    uint8_t *ysrc = source;
    uint8_t *usrc = ysrc + (source_width * source_height);
    uint8_t *vsrc = usrc + ((source_width/2) * (source_height/2));
    uint8_t *ydst = (uint8_t*)source_frame->data[0];
    uint8_t *udst = (uint8_t*)source_frame->data[1];
    uint8_t *vdst = (uint8_t*)source_frame->data[2];
    int shhalf = source_height / 2;
    int swhalf = source_width / 2;
    for (row = 0; row < source_height; row++) {
        memcpy(ydst, ysrc, source_frame->linesize[0]);
        ysrc += source_width;
        ydst += source_frame->linesize[0];
    }
    for (row = 0; row < shhalf; row++) {
        memcpy(udst, usrc, source_frame->linesize[1]);
        usrc += swhalf;
        udst += source_frame->linesize[1];
    }
    for (row = 0; row < shhalf; row++) {
        memcpy(vdst, vsrc, source_frame->linesize[2]);
        vsrc += swhalf;
        vdst += source_frame->linesize[2];
    }
}

typedef struct _opaque_struct_ {
    uint8_t        *caption_buffer;
    int            caption_size;
    int64_t        splice_duration;
    int            splice_point;
    int64_t        splice_duration_remaining;
} opaque_struct;

void *video_prepare_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;
    dataqueue_message_struct *encode_msg;
    int deinterlacer_ready = 0;
    AVFilterContext *deinterlacer_source = NULL;
    AVFilterContext *deinterlacer_output = NULL;
    AVFilterGraph *deinterlacer = NULL;

    AVFilter *filter_source = (AVFilter*)avfilter_get_by_name("buffer");
    AVFilter *filter_output = (AVFilter*)avfilter_get_by_name("buffersink");
    AVFilterInOut *filter_inputs = avfilter_inout_alloc();
    AVFilterInOut *filter_outputs = avfilter_inout_alloc();
    AVBufferSinkParams *params = av_buffersink_params_alloc();
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P };
    AVFrame *source_frame = NULL;
    AVFrame *deinterlaced_frame = NULL;
    uint8_t *output_data[4];
    int output_stride[4];
    int row;
    int i;
    uint8_t *deinterlaced_buffer = NULL;
    int num_outputs = core->cd->num_outputs;
    int current_output;
    struct SwsContext *output_scaler[MAX_TRANS_OUTPUTS];
    scale_struct scaled_output[MAX_TRANS_OUTPUTS];
    struct SwsContext *thumbnail_scaler = NULL;
    scale_struct *thumbnail_output = NULL;
    int64_t deinterlaced_frame_count[MAX_TRANS_OUTPUTS];
    int64_t sync_frame_count = 0;
    double fps = 30.0;
    opaque_struct *opaque_data = NULL;
    int thumbnail_count = 0;

    for (i = 0; i < MAX_TRANS_OUTPUTS; i++) {
        output_scaler[i] = NULL;
        deinterlaced_frame_count[i] = 0;
        scaled_output[i].output_data[0] = NULL;
    }
    
    params->pixel_fmts = pix_fmts;

    source_frame = av_frame_alloc();
    deinterlaced_frame = av_frame_alloc();    
    
#define MAX_SETTINGS_SIZE 256    
    char settings[MAX_SETTINGS_SIZE];
    //const char *filter = "yadif=0:-1:0";
    //const char *filter = "w3fdif=1:1";
    const char *filter = "bwdif=0:-1:0";
    
    while (video_prepare_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);
        while (!msg && video_prepare_thread_running) {
            usleep(1000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);            
        }

        if (!video_prepare_thread_running) {
            while (msg) {
                if (msg) {
                    memory_return(core->raw_video_pool, msg->buffer);
                    msg->buffer = NULL;
                    memory_return(core->fillet_msg_pool, msg);
                    msg = NULL;
                }
                msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);                            
            }
            goto cleanup_video_prepare_thread;
        }

        if (msg) {
            int width = msg->width;
            int height = msg->height;
            int ready = 0;
            
            // deinterlace the frame and scale it to the number of outputs
            // required and then feed the encoder input queues
            // for now I'm doing a blanket deinterlace
            // this is based on ffmpeg examples found on various sites online
            // there is not a lot of documentation on using the filter graphs
            if (!deinterlacer_ready) {
                char scaler_params[32];
                char *scaler_flags;
                
                snprintf(settings, MAX_SETTINGS_SIZE-1,"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                         width,
                         height,
                         AV_PIX_FMT_YUV420P,
                         msg->fps_den,   
                         msg->fps_num,
                         msg->aspect_num,
                         msg->aspect_den);
                deinterlacer = avfilter_graph_alloc();
                deinterlacer->nb_threads = 4;

                avfilter_graph_create_filter(&deinterlacer_source,
                                             filter_source,
                                             "in",
                                             settings,
                                             NULL,
                                             deinterlacer);
                avfilter_graph_create_filter(&deinterlacer_output,
                                             filter_output,
                                             "out",
                                             NULL,
                                             params,
                                             deinterlacer);
                av_free(params);

                filter_inputs->name = av_strdup("out");
                filter_inputs->filter_ctx = deinterlacer_output;
                filter_inputs->pad_idx = 0;
                filter_inputs->next = NULL;

                filter_outputs->name = av_strdup("in");
                filter_outputs->filter_ctx = deinterlacer_source;
                filter_outputs->pad_idx = 0;
                filter_outputs->next = NULL;

                memset(scaler_params,0,sizeof(scaler_params));
                sprintf(scaler_params,"flags=%d",SWS_BICUBIC); // good performance!
                scaler_flags = av_strdup(scaler_params);

                deinterlacer->scale_sws_opts = av_malloc(strlen(scaler_flags)+1);
                strcpy(deinterlacer->scale_sws_opts, scaler_flags);
                free(scaler_flags);

                avfilter_graph_parse_ptr(deinterlacer,
                                         filter,
                                         &filter_inputs,
                                         &filter_outputs,
                                         NULL);
                avfilter_graph_config(deinterlacer,
                                      NULL);

                avfilter_inout_free(&filter_inputs);
                avfilter_inout_free(&filter_outputs);

                av_image_alloc(output_data,
                               output_stride,
                               width, height,
                               AV_PIX_FMT_YUV420P,
                               1);                

                source_frame->pts = AV_NOPTS_VALUE;
                source_frame->pkt_dts = AV_NOPTS_VALUE;
                source_frame->pkt_pts = AV_NOPTS_VALUE;
                source_frame->pkt_duration = 0;
                source_frame->pkt_pos = -1;
                source_frame->pkt_size = -1;
                source_frame->key_frame = -1;
                source_frame->sample_aspect_ratio = (AVRational){1,1};
                source_frame->format = 0;
                source_frame->extended_data = NULL;
                source_frame->color_primaries = AVCOL_PRI_BT709;
                source_frame->color_trc = AVCOL_TRC_BT709;
                source_frame->colorspace = AVCOL_SPC_BT709;
                source_frame->color_range = AVCOL_RANGE_JPEG;
                source_frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
                source_frame->flags = 0;
                source_frame->data[0] = output_data[0];
                source_frame->data[1] = output_data[1];
                source_frame->data[2] = output_data[2];
                source_frame->data[3] = output_data[3];
                source_frame->linesize[0] = output_stride[0];
                source_frame->linesize[1] = output_stride[1];
                source_frame->linesize[2] = output_stride[2];
                source_frame->linesize[3] = output_stride[3];                               
                source_frame->channels = 0;
                source_frame->channel_layout = 0;
                
                deinterlacer_ready = 1;
            }
            source_frame->pts = msg->pts;
            source_frame->pkt_dts = msg->dts;
            source_frame->pkt_pts = msg->pts;
            source_frame->width = width;
            source_frame->height = height;
            source_frame->interlaced_frame = msg->interlaced;
            source_frame->top_field_first = msg->tff;

            if (!opaque_data && (msg->caption_buffer || msg->splice_point)) {
                opaque_data = (opaque_struct*)malloc(sizeof(opaque_struct));
                if (msg->caption_buffer) {
                    opaque_data->caption_buffer = msg->caption_buffer;
                    opaque_data->caption_size = msg->caption_size;
                } else {
                    opaque_data->caption_buffer = NULL;
                    opaque_data->caption_size = 0;
                }
                if (msg->splice_point) {
                    opaque_data->splice_point = msg->splice_point;
                    opaque_data->splice_duration = msg->splice_duration;
                    opaque_data->splice_duration_remaining = msg->splice_duration_remaining;
                } else {
                    opaque_data->splice_point = 0;
                    opaque_data->splice_duration = msg->splice_duration;
                    opaque_data->splice_duration_remaining = msg->splice_duration_remaining;
                }
                source_frame->opaque = (void*)opaque_data;
                opaque_data = NULL;
            } else {               
                source_frame->opaque = NULL;
                opaque_data = NULL;                
            }

            copy_image_data_to_ffmpeg(msg->buffer, width, height, source_frame);

            ready = av_buffersrc_add_frame_flags(deinterlacer_source,
                                                 source_frame,
                                                 AV_BUFFERSRC_FLAG_KEEP_REF);
            
            while (video_prepare_thread_running) {
                int retcode;
                int output_ready = 1;
                double av_sync_offset = 0;

                retcode = av_buffersink_get_frame(deinterlacer_output, deinterlaced_frame);
                if (retcode == AVERROR(EAGAIN) ||
                    retcode == AVERROR(AVERROR_EOF)) {
                    output_ready = 0;
                    break;
                }

                if (output_ready) {                    
                    // get deinterlaced frame
                    // deinterlaced_frame is where the video is located now
                    opaque_data = (opaque_struct*)deinterlaced_frame->opaque;
                    deinterlaced_frame->opaque = NULL;

                    if (thumbnail_scaler == NULL) {
                        thumbnail_scaler = sws_getContext(width, height, AV_PIX_FMT_YUV420P,
                                                          THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, AV_PIX_FMT_YUV420P,
                                                          SWS_BICUBIC, NULL, NULL, NULL);
                    }

                    thumbnail_count++;
                    if (thumbnail_count == 150) {  // maybe use a timer instead?
                        dataqueue_message_struct *thumbnail_msg;
                        
                        thumbnail_output = (scale_struct*)malloc(sizeof(scale_struct));
                        av_image_alloc(thumbnail_output->output_data,
                                       thumbnail_output->output_stride,
                                       THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, AV_PIX_FMT_YUV420P, 1);                        
                        sws_scale(thumbnail_scaler,
                                  (const uint8_t * const*)deinterlaced_frame->data,
                                  deinterlaced_frame->linesize,
                                  0, height,
                                  thumbnail_output->output_data,
                                  thumbnail_output->output_stride);

                        thumbnail_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                        if (!thumbnail_msg) {
                            fprintf(stderr,"FATAL ERROR: unable to obtain thumbnail_msg!!\n");
                            exit(0);                                                                                            
                        }
                        thumbnail_msg->buffer = thumbnail_output;                        
                        dataqueue_put_front(core->encodevideo->thumbnail_queue, thumbnail_msg);
                        thumbnail_output = NULL;
                        thumbnail_count = 0;
                    }
                    
                    for (current_output = 0; current_output < num_outputs; current_output++) {
                        int output_width = core->cd->transvideo_info[current_output].width;
                        int output_height = core->cd->transvideo_info[current_output].height;
                        int video_frame_size = 3 * output_height * output_width / 2;
                        
                        if (output_scaler[current_output] == NULL) {
                            output_scaler[current_output] = sws_getContext(width, height, AV_PIX_FMT_YUV420P,
                                                                           output_width, output_height, AV_PIX_FMT_YUV420P,
                                                                           SWS_LANCZOS, NULL, NULL, NULL); 
                            av_image_alloc(scaled_output[current_output].output_data,
                                           scaled_output[current_output].output_stride,
                                           output_width, output_height, AV_PIX_FMT_YUV420P, 1);
                        }
                        
                        fprintf(stderr,"[%d] SCALING OUTPUT FRAME TO: %d x %d\n",
                                current_output,
                                output_width,
                                output_height);
                        
                        sws_scale(output_scaler[current_output],
                                  (const uint8_t * const*)deinterlaced_frame->data,
                                  deinterlaced_frame->linesize,
                                  0, height,
                                  scaled_output[current_output].output_data,
                                  scaled_output[current_output].output_stride);

                        uint8_t *outputy;
                        uint8_t *outputu;
                        uint8_t *outputv;
                        uint8_t *sourcey;
                        uint8_t *sourceu;
                        uint8_t *sourcev;
                        int stridey;
                        int strideu;
                        int stridev;
                        int whalf = output_width/2;
                        int hhalf = output_height/2;
                        
                        deinterlaced_buffer = (uint8_t*)memory_take(core->raw_video_pool, video_frame_size);
                        if (!deinterlaced_buffer) {
                            fprintf(stderr,"FATAL ERROR: unable to obtain deinterlaced_buffer!!\n");
                            exit(0);                                                
                        }
                        sourcey = (uint8_t*)scaled_output[current_output].output_data[0];
                        sourceu = (uint8_t*)scaled_output[current_output].output_data[1];
                        sourcev = (uint8_t*)scaled_output[current_output].output_data[2];
                        stridey = scaled_output[current_output].output_stride[0];
                        strideu = scaled_output[current_output].output_stride[1];
                        stridev = scaled_output[current_output].output_stride[2];
                        outputy = (uint8_t*)deinterlaced_buffer;
                        outputu = (uint8_t*)outputy + (output_width*output_height);
                        outputv = (uint8_t*)outputu + (whalf*hhalf);
                        for (row = 0; row < output_height; row++) {
                            memcpy(outputy, sourcey, output_width);
                            outputy += output_width;
                            sourcey += stridey;
                        }
                        for (row = 0; row < hhalf; row++) {
                            memcpy(outputu, sourceu, whalf);
                            outputu += whalf;
                            sourceu += strideu;
                        }
                        for (row = 0; row < hhalf; row++) {
                            memcpy(outputv, sourcev, whalf);
                            outputv += whalf;
                            sourcev += stridev;
                        }

                        if (msg->fps_den > 0) {
                            fps = (double)((double)msg->fps_num / (double)msg->fps_den);
                        }

                        //fprintf(stderr,"[%d] FPS:%f\n", current_output, fps);

                        video_stream_struct *vstream = (video_stream_struct*)core->source_stream[0].video_stream;  // only one source stream
                        int64_t sync_diff;

                        deinterlaced_frame_count[current_output]++;  // frames since the video start time
                        sync_frame_count = (int64_t)(((((double)deinterlaced_frame->pkt_pts-(double)vstream->first_timestamp) / (double)90000.0))*(double)fps);
                        av_sync_offset = (((double)deinterlaced_frame_count[current_output] - (double)sync_frame_count)/(double)fps)*(double)1000.0;
                        sync_diff = (int64_t)deinterlaced_frame_count[current_output] - (int64_t)sync_frame_count;

                        fprintf(stderr,"DEINTERLACED_FRAME_COUNT:%ld  SYNC_FRAME_COUNT:%ld   SYNC OFFSET:%ld (%.2fms)  PKT_DTS:%ld  FT:%ld  FPS:%.2f\n",
                                deinterlaced_frame_count[current_output],
                                sync_frame_count,
                                deinterlaced_frame_count[current_output] - sync_frame_count,
                                av_sync_offset,
                                deinterlaced_frame->pkt_pts,
                                vstream->first_timestamp,
                                fps);

                        if (sync_diff > fps ||
                            sync_diff < -fps) {
                            syslog(LOG_ERR,"FATAL ERROR: a/v sync is compromised!!\n");
                            fprintf(stderr,"FATAL ERROR: a/v sync is compromised!!\n");
                            exit(0);                                                                                                                        
                        }

                        if (sync_diff < -1) {
                            uint8_t *repeated_buffer = (uint8_t*)memory_take(core->raw_video_pool, video_frame_size);
                            if (repeated_buffer) {
                                memcpy(repeated_buffer, deinterlaced_buffer, video_frame_size);

                                encode_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                                if (!encode_msg) {
                                    fprintf(stderr,"FATAL ERROR: unable to obtain encode_msg!!\n");
                                    exit(0);                                                                                            
                                }
                                encode_msg->buffer = repeated_buffer;
                                encode_msg->buffer_size = video_frame_size;
                                encode_msg->pts = 0;
                                encode_msg->dts = 0;
                                encode_msg->tff = 1;
                                encode_msg->interlaced = 0;
                                encode_msg->fps_num = msg->fps_num;
                                encode_msg->fps_den = msg->fps_den;
                                encode_msg->aspect_num = msg->aspect_num;
                                encode_msg->aspect_den = msg->aspect_den;
                                encode_msg->width = output_width;
                                encode_msg->height = output_height;
                                encode_msg->stream_index = current_output;
                                
                                encode_msg->caption_buffer = NULL;
                                encode_msg->caption_size = 0;
                                                
                                /*fprintf(stderr,"[%d] SENDING VIDEO FRAME TO ENCODER PTS:%ld DTS:%ld  ASPECT:%d:%d\n",
                                        current_output,
                                        encode_msg->pts,
                                        encode_msg->dts,
                                        encode_msg->aspect_num,
                                        encode_msg->aspect_den);*/
                                
                                deinterlaced_frame_count[current_output]++;  // frames since the video start time
                                dataqueue_put_front(core->encodevideo->input_queue[current_output], encode_msg);                                
                            } else {
                                fprintf(stderr,"FATAL ERROR: unable to obtain repeated_buffer!!\n");
                                exit(0);
                            }
                        }

                        encode_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                        if (!encode_msg) {
                            fprintf(stderr,"FATAL ERROR: unable to obtain encode_msg!!\n");
                            exit(0);                            
                        }
                        encode_msg->buffer = deinterlaced_buffer;
                        encode_msg->buffer_size = video_frame_size;
                        encode_msg->pts = deinterlaced_frame->pkt_pts;  // or pkt_pts?
                        encode_msg->dts = deinterlaced_frame->pkt_dts;                        
                        encode_msg->interlaced = 0;
                        encode_msg->tff = 1;
                        encode_msg->fps_num = msg->fps_num;
                        encode_msg->fps_den = msg->fps_den;
                        encode_msg->aspect_num = msg->aspect_num;
                        encode_msg->aspect_den = msg->aspect_den;
                        encode_msg->width = output_width;
                        encode_msg->height = output_height;
                        encode_msg->stream_index = current_output;

                        if (opaque_data) {
                            uint8_t *caption_buffer;
                            int caption_size;

                            caption_size = opaque_data->caption_size;
                            if (caption_size > 0) {
                                caption_buffer = (uint8_t*)malloc(caption_size+1);
                                memcpy(caption_buffer, opaque_data->caption_buffer, caption_size);
                                encode_msg->caption_buffer = caption_buffer;
                                encode_msg->caption_size = caption_size;
                            } else {
                                encode_msg->caption_buffer = NULL;
                                encode_msg->caption_size = 0;
                            }
                            encode_msg->splice_point = opaque_data->splice_point;
                            encode_msg->splice_duration = opaque_data->splice_duration;
                            encode_msg->splice_duration_remaining = opaque_data->splice_duration_remaining;
                        } else {
                            encode_msg->caption_buffer = NULL;
                            encode_msg->caption_size = 0;
                            encode_msg->splice_point = 0;
                            encode_msg->splice_duration = 0;
                        }

                        if (sync_diff > 1) {
                            if (encode_msg->splice_point == 0 || encode_msg->splice_duration_remaining == 0) {
                                if (encode_msg->caption_buffer) {
                                    // THIS NEEDS TO BE SAVED AND TACKED ONTO ANOTHER FRAME- FOR NOW WE WILL JUST DROP IT
                                    free(encode_msg->caption_buffer);
                                    encode_msg->caption_buffer = NULL;
                                }
                                memory_return(core->raw_video_pool, deinterlaced_buffer);
                                deinterlaced_buffer = NULL;
                                deinterlaced_frame_count[current_output]--;  // frames since the video start time- go back one
                                memory_return(core->fillet_msg_pool, encode_msg);
                                encode_msg = NULL;
                                // dropped!
                            }
                        }
                    
                        /*fprintf(stderr,"[%d] SENDING VIDEO FRAME TO ENCODER PTS:%ld DTS:%ld  ASPECT:%d:%d\n",
                          current_output,
                          encode_msg->pts,
                          encode_msg->dts,
                          encode_msg->aspect_num,
                          encode_msg->aspect_den);*/

                        if (encode_msg) {
                            dataqueue_put_front(core->encodevideo->input_queue[current_output], encode_msg);
                        }
                    }
                    if (opaque_data) {
                        if (opaque_data->caption_buffer) {
                            free(opaque_data->caption_buffer);
                        }
                        opaque_data->caption_buffer = NULL;
                        opaque_data->caption_size = 0;
                        free(opaque_data);
                        opaque_data = NULL;
                    }
                } else {
                    break;
                }
                av_frame_unref(deinterlaced_frame);
            }

            memory_return(core->raw_video_pool, msg->buffer);
            msg->buffer = NULL;
            memory_return(core->fillet_msg_pool, msg);
            msg = NULL;                                                                            
        }
    }
    
cleanup_video_prepare_thread:
    if (deinterlacer_ready) {
        av_free(deinterlacer->scale_sws_opts);
        deinterlacer->scale_sws_opts = NULL;
        avfilter_graph_free(&deinterlacer);
        deinterlacer_ready = 0;
        av_freep(&output_data[0]);
        av_frame_free(&deinterlaced_frame);
        av_frame_free(&source_frame);

        for (current_output = 0; current_output < num_outputs; current_output++) {
            if (scaled_output[current_output].output_data[0]) {
                av_freep(&scaled_output[current_output].output_data[0]);                
            }
            sws_freeContext(output_scaler[current_output]);                   
        }
        sws_freeContext(thumbnail_scaler);        
    }
    return NULL;
}

typedef struct _signal_struct_
{
    int64_t pts;
    int     scte35_ready;
    int64_t scte35_duration;
    int64_t scte35_duration_remaining;
} signal_struct;

void *video_decode_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    int video_decoder_ready = 0;
    dataqueue_message_struct *msg;
    AVCodecContext *decode_avctx = NULL;
    AVCodec *decode_codec = NULL;
    AVPacket *decode_pkt = NULL;
    AVFrame *decode_av_frame = NULL;
    int64_t decoder_frame_count = 0;
    enum AVPixelFormat source_format = AV_PIX_FMT_YUV420P;
    enum AVPixelFormat output_format = AV_PIX_FMT_YUV420P;
    struct SwsContext *decode_converter = NULL;
    uint8_t *source_data[4];
    uint8_t *output_data[4];
    int source_stride[4];
    int output_stride[4];
    int last_video_frame_size = -1;

#define MAX_SIGNAL_WINDOW 30    
    signal_struct signal_data[MAX_SIGNAL_WINDOW];
    int signal_write_index = 0;

    memset(signal_data,0,sizeof(signal_data));

    fprintf(stderr,"status: starting video decode thread: %d\n", video_decode_thread_running);
    
    while (video_decode_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->transvideo->input_queue);
        while (!msg && video_decode_thread_running) {
            usleep(1000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->transvideo->input_queue);            
        }
        
        if (!video_decode_thread_running) {
            if (msg) {
                sorted_frame_struct *frame = (sorted_frame_struct*)msg->buffer;
                if (frame) {
                    memory_return(core->compressed_video_pool, frame->buffer);
                    frame->buffer = NULL;
                    memory_return(core->frame_msg_pool, frame);
                    frame = NULL;
                }
                memory_return(core->fillet_msg_pool, msg);
                msg = NULL;               
            }
            goto cleanup_video_decoder_thread;
        }

        if (msg) {
            sorted_frame_struct *frame = (sorted_frame_struct*)msg->buffer;
            if (frame) {
                int retcode;

                if (!video_decoder_ready) {
                    if (frame->media_type == MEDIA_TYPE_MPEG2) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
                    } else if (frame->media_type == MEDIA_TYPE_H264) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
                    } else if (frame->media_type == MEDIA_TYPE_HEVC) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
                    } else {
                        fprintf(stderr,"error: unknown source video codec type- failing!\n");
                        //unknown media type- report error and quit!
                        exit(0);
                    }
                    decode_avctx = avcodec_alloc_context3(decode_codec);
                    avcodec_open2(decode_avctx, decode_codec, NULL);
                    decode_av_frame = av_frame_alloc();
                    decode_pkt = av_packet_alloc();
                    video_decoder_ready = 1;
                }//!video_decoder_ready

                uint8_t *incoming_video_buffer = frame->buffer;
                int incoming_video_buffer_size = frame->buffer_size;

                decode_pkt->size = incoming_video_buffer_size;
                decode_pkt->data = incoming_video_buffer;
                decode_pkt->pts = frame->pts;
                decode_pkt->dts = frame->full_time;  // this is the one that matters

                signal_data[signal_write_index].pts = frame->full_time;
                signal_data[signal_write_index].scte35_ready = frame->splice_point;
                signal_data[signal_write_index].scte35_duration = frame->splice_duration;
                signal_data[signal_write_index].scte35_duration_remaining = frame->splice_duration_remaining;
                signal_write_index = (signal_write_index + 1) % MAX_SIGNAL_WINDOW;
                
                retcode = avcodec_send_packet(decode_avctx, decode_pkt);
                if (retcode < 0) {
                    //error decoding video frame-report!
                    fprintf(stderr,"error: unable to decode video frame - sorry\n");
                }

                while (retcode >= 0) {
                    AVFrameSideData *caption_data = NULL;
                    uint8_t *caption_buffer = NULL;
                    int caption_size = 0;
                    int is_frame_interlaced;
                    int is_frame_tff;
                    dataqueue_message_struct *prepare_msg;
                    uint8_t *output_video_frame;
                    int video_frame_size;
                    int frame_height;
                    int frame_height2;
                    int frame_width;
                    int frame_width2;
                    int row;
                    uint8_t *y_output_video_frame;
                    uint8_t *u_output_video_frame;
                    uint8_t *v_output_video_frame;
                    uint8_t *y_source_video_frame;
                    uint8_t *u_source_video_frame;
                    uint8_t *v_source_video_frame;
                    int y_source_stride;
                    int uv_source_stride;
                    
                    retcode = avcodec_receive_frame(decode_avctx, decode_av_frame);
                    if (retcode == AVERROR(EAGAIN) || retcode == AVERROR_EOF) {
                        break;
                    }
                    if (retcode < 0) {
                        break;
                    }
                    decoder_frame_count++;

                    is_frame_interlaced = decode_av_frame->interlaced_frame;
                    is_frame_tff = decode_av_frame->top_field_first;
                    source_format = decode_av_frame->format;
                    frame_height = decode_avctx->height;
                    frame_width = decode_avctx->width;                 

                    fprintf(stderr,"STATUS: %ld:DECODED VIDEO FRAME: %dx%d (%d:%d) INTERLACED:%d (TFF:%d)\n",
                            decoder_frame_count,
                            frame_width, frame_height,
                            decode_avctx->sample_aspect_ratio.num,
                            decode_avctx->sample_aspect_ratio.den,
                            is_frame_interlaced,
                            is_frame_tff);

                    if (frame_width > 3840 &&
                        frame_height > 2160) {
                        // error decoding video frame
                        break;
                    }

                    frame_height2 = frame_height / 2;
                    frame_width2 = frame_width / 2;

                    video_frame_size = 3 * frame_height * frame_width / 2;
                    if (last_video_frame_size != -1) {
                        if (last_video_frame_size != video_frame_size) {
                            // report video frame size change
                            fprintf(stderr,"warning: video frame size changed - new size: %d x %d\n", frame_width, frame_height);
                        }
                    }
                    last_video_frame_size = video_frame_size;

                    output_video_frame = (uint8_t*)memory_take(core->raw_video_pool, video_frame_size);
                    if (!output_video_frame) {
                        fprintf(stderr,"FATAL ERROR: unable to obtain output_video_frame!!\n");
                        exit(0);
                    }

                    //422 to 420 conversion if needed
                    //10 to 8 bit conversion if needed
                    source_data[0] = decode_av_frame->data[0];
                    source_data[1] = decode_av_frame->data[1];
                    source_data[2] = decode_av_frame->data[2];
                    source_data[3] = decode_av_frame->data[3];
                    source_stride[0] = decode_av_frame->linesize[0];
                    source_stride[1] = decode_av_frame->linesize[1];
                    source_stride[2] = decode_av_frame->linesize[2];
                    source_stride[3] = decode_av_frame->linesize[3];
                    
                    if (source_format != output_format) {
                        if (!decode_converter) {
                            decode_converter = sws_getContext(frame_width, frame_height, source_format,
                                                              frame_width, frame_height, output_format,
                                                              SWS_BICUBIC,
                                                              NULL, NULL, NULL); // no dither specified
                            av_image_alloc(output_data, output_stride, frame_width, frame_height, output_format, 1);
                        }

                        sws_scale(decode_converter,
                                  (const uint8_t * const*)source_data, source_stride, 0,
                                  frame_height, output_data, output_stride);

                        y_source_video_frame = output_data[0];
                        u_source_video_frame = output_data[1];
                        v_source_video_frame = output_data[2];
                        y_source_stride = output_stride[0];
                        uv_source_stride = output_stride[1];
                    } else {
                        y_source_video_frame = source_data[0];
                        u_source_video_frame = source_data[1];
                        v_source_video_frame = source_data[2];
                        y_source_stride = source_stride[0];
                        uv_source_stride = source_stride[1];
                    }

                    y_output_video_frame = output_video_frame;
                    u_output_video_frame = y_output_video_frame + (frame_width * frame_height);
                    v_output_video_frame = u_output_video_frame + (frame_width2 * frame_height2);
                    for (row = 0; row < frame_height; row++) {
                        memcpy(y_output_video_frame, y_source_video_frame, frame_width);
                        y_output_video_frame += frame_width;
                        y_source_video_frame += y_source_stride;                        
                    }
                    for (row = 0; row < frame_height2; row++) {
                        memcpy(u_output_video_frame, u_source_video_frame, frame_width2);
                        u_output_video_frame += frame_width2;
                        u_source_video_frame += uv_source_stride;                        
                    }
                    for (row = 0; row < frame_height2; row++) {
                        memcpy(v_output_video_frame, v_source_video_frame, frame_width2);
                        v_output_video_frame += frame_width2;
                        v_source_video_frame += uv_source_stride;
                    }

                    caption_data = (AVFrameSideData*)av_frame_get_side_data(decode_av_frame, AV_FRAME_DATA_A53_CC);
                    if (caption_data) {
                        // caption data is present
                        uint8_t *buffer;
                        int buffer_size;
                        int caption_elements;

                        buffer = (uint8_t*)caption_data->data;
                        if (buffer) {
                            buffer_size = caption_data->size;
                            caption_elements = buffer_size / 3;
                            
                            // ffmpeg doesn't return the caption buffer with the header information
#define CAPTION_HEADER_SIZE 7
                            caption_buffer = (uint8_t*)malloc(buffer_size+CAPTION_HEADER_SIZE);
                            caption_buffer[0] = 0x47;  // this is the GA94 tag
                            caption_buffer[1] = 0x41;
                            caption_buffer[2] = 0x39;
                            caption_buffer[3] = 0x34;
                            caption_buffer[4] = 0x03;
                            caption_buffer[5] = caption_elements | 0xc0;
                            caption_buffer[6] = 0xff;
                            memcpy(caption_buffer+CAPTION_HEADER_SIZE,
                                   buffer,
                                   buffer_size);
                            //fprintf(stderr,"status: decoder-- saving caption_buffer\n");
                            caption_size = buffer_size+CAPTION_HEADER_SIZE;
                        } else {
                            caption_buffer = NULL;
                            caption_size = 0;
                        }                        
                    }
                    
                    prepare_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct)); 
                    if (prepare_msg) {
                        prepare_msg->buffer = output_video_frame;
                        prepare_msg->buffer_size = video_frame_size;
                        prepare_msg->pts = decode_av_frame->pts;
                        prepare_msg->dts = decode_av_frame->pkt_dts;  // full time
                        /*fprintf(stderr,"DECODED VIDEO FRAME: PTS:%ld PKT_DTS:%ld PKT_PTS:%ld\n",
                                decode_av_frame->pts,
                                decode_av_frame->pkt_dts,
                                decode_av_frame->pkt_pts);*/
                        if (!is_frame_interlaced) {
                            prepare_msg->tff = 1;
                        } else {
                            prepare_msg->tff = is_frame_tff;
                        }                        
                        prepare_msg->interlaced = is_frame_interlaced;
                        prepare_msg->caption_buffer = caption_buffer;
                        prepare_msg->caption_size = caption_size;
                        caption_buffer = NULL;
                        caption_size = 0;

                        int lookup;
                        int splice_point = 0;
                        int64_t splice_duration = 0;
                        int64_t splice_duration_remaining = 0;
                        for (lookup = 0; lookup < MAX_SIGNAL_WINDOW; lookup++) {
                            if (signal_data[lookup].pts == decode_av_frame->pkt_dts) {
                                splice_point = signal_data[lookup].scte35_ready;
                                splice_duration = signal_data[lookup].scte35_duration;
                                splice_duration_remaining = signal_data[lookup].scte35_duration_remaining;
                                break;
                            }
                        }

                        //ffmpeg does this inverse because of the 1/X
                        prepare_msg->fps_num = decode_avctx->time_base.den / decode_avctx->ticks_per_frame;  // for 29.97fps- should be 30000 and
                        prepare_msg->fps_den = decode_avctx->time_base.num;                                  // this should be 1001
                        //fprintf(stderr,"FPS: %d / %d\n", prepare_msg->fps_num, prepare_msg->fps_den);
                        prepare_msg->aspect_num = decode_avctx->sample_aspect_ratio.num;
                        prepare_msg->aspect_den = decode_avctx->sample_aspect_ratio.den;
                        prepare_msg->width = frame_width;
                        prepare_msg->height = frame_height;
                        prepare_msg->splice_point = splice_point;
                        prepare_msg->splice_duration = splice_duration;
                        prepare_msg->splice_duration_remaining = splice_duration_remaining;

                        core->decoded_source_info.decoded_width = frame_width;
                        core->decoded_source_info.decoded_height = frame_height;
                        core->decoded_source_info.decoded_fps_num = prepare_msg->fps_num;
                        core->decoded_source_info.decoded_fps_den = prepare_msg->fps_den;
                        core->decoded_source_info.decoded_aspect_num = prepare_msg->aspect_num;
                        core->decoded_source_info.decoded_aspect_den = prepare_msg->aspect_den;
                        core->decoded_source_info.decoded_video_media_type = frame->media_type;

                        dataqueue_put_front(core->preparevideo->input_queue, prepare_msg);                        
                    } else {
                        fprintf(stderr,"FATAL ERROR: unable to obtain prepare_msg!!\n");
                        exit(0);                                               
                    }
                }
                memory_return(core->compressed_video_pool, frame->buffer);
                frame->buffer = NULL;
            } else {
                //report error condition
            }

            memory_return(core->frame_msg_pool, frame);
            frame = NULL;
            memory_return(core->fillet_msg_pool, msg);
            msg = NULL;
        } else {
            //report error condition
        }
    }
cleanup_video_decoder_thread:

    av_frame_free(&decode_av_frame);
    av_packet_free(&decode_pkt);
    avcodec_close(decode_avctx);
    avcodec_free_context(&decode_avctx);
    sws_freeContext(decode_converter);
    av_freep(&output_data[0]);    
    
    return NULL;
}

int start_video_transcode_threads(fillet_app_struct *core)
{
    video_encode_thread_running = 1;

    // for now we don't allow mixing the video codec across the streams
    // mixing will come later once i get some other things figured out
    if (core->cd->transvideo_info[0].video_codec == STREAM_TYPE_HEVC) {
        pthread_create(&video_encode_thread_id, NULL, video_encode_thread_x265, (void*)core);        
    } else if (core->cd->transvideo_info[0].video_codec == STREAM_TYPE_H264) {    
        pthread_create(&video_encode_thread_id, NULL, video_encode_thread_x264, (void*)core);
    } else {
        fprintf(stderr,"error: unknown video encoder selected! fail!\n");
        exit(0);
    }

    video_thumbnail_thread_running = 1;
    pthread_create(&video_thumbnail_thread_id, NULL, video_thumbnail_thread, (void*)core);

    video_prepare_thread_running = 1;
    pthread_create(&video_prepare_thread_id, NULL, video_prepare_thread, (void*)core);

    video_decode_thread_running = 1;
    pthread_create(&video_decode_thread_id, NULL, video_decode_thread, (void*)core);
    
    return 0;
}

int stop_video_transcode_threads(fillet_app_struct *core)
{
    video_decode_thread_running = 0;
    pthread_join(video_decode_thread_id, NULL);
    
    video_prepare_thread_running = 0;
    pthread_join(video_prepare_thread_id, NULL);

    video_thumbnail_thread_running = 0;
    pthread_join(video_thumbnail_thread_id, NULL);
    
    video_encode_thread_running = 0;
    pthread_join(video_encode_thread_id, NULL);
    
    return 0;
}

#else // ENABLE_TRANSCODE

void *video_prepare_thread(void *context)
{
    fprintf(stderr,"VIDEO PREPARE NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

void *video_decode_thread(void *context)
{
    fprintf(stderr,"VIDEO DECODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");            
    return NULL;
}

void *video_encode_thread_x264(void *context)
{
    fprintf(stderr,"VIDEO ENCODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

void *video_encode_thread_x265(void *context)
{
    fprintf(stderr,"VIDEO ENCODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

int start_video_transcode_threads(fillet_app_struct *core)
{
    return 0;
}

int stop_video_transcode_threads(fillet_app_struct *core)
{
    return 0;
}

#endif
    



