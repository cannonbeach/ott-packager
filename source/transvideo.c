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
#include "../cbffmpeg/libavformat/avformat.h"
#include "../cbffmpeg/libavfilter/buffersink.h"
#include "../cbffmpeg/libavfilter/buffersrc.h"
#include "../cbx264/x264.h"

static volatile int video_decode_thread_running = 0;
static volatile int video_prepare_thread_running = 0;
static volatile int video_encode_thread_running = 0;
static pthread_t video_decode_thread_id;
static pthread_t video_prepare_thread_id;
static pthread_t video_encode_thread_id;

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
} x264_encoder_struct;

int video_sink_frame_callback(fillet_app_struct *core, uint8_t *new_buffer, int sample_size, int64_t pts, int64_t dts, int source);    

void *video_encode_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;
    int current_queue;
    int current_encoder;
    int num_outputs = core->cd->num_outputs;
    x264_encoder_struct x264_data[MAX_TRANS_OUTPUTS];

    for (current_encoder = 0; current_encoder < num_outputs; current_encoder++) {
        x264_data[current_encoder].h = NULL;
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
                
                sar_width = output_height*msg->aspect_num;
                sar_height = output_width*msg->aspect_den;
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
                x264_data[current_encoder].param.i_scenecut_threshold = 0;  // need to keep abr alignment
                
                x264_param_apply_profile(&x264_data[current_encoder].param,"main");
                
                x264_data[current_encoder].h = x264_encoder_open(&x264_data[current_encoder].param);
            }
                        
            if (!video_encode_thread_running) {
                while (msg) {
                    if (msg) {
                        free(msg->buffer);
                        free(msg);
                    }
                    msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodevideo->input_queue[current_queue]);
                }
                goto cleanup_video_encode_thread;
            }

            uint8_t *video;
            int output_size;
            int64_t pts;
            int64_t dts;

            video = msg->buffer;

            x264_data[current_queue].pic.i_pts = msg->pts;
            x264_data[current_queue].pic.i_dts = msg->dts;
            
            memcpy(x264_data[current_queue].pic.img.plane[0],
                   video,
                   x264_data[current_queue].y_size);
            memcpy(x264_data[current_queue].pic.img.plane[1],
                   video + x264_data[current_queue].y_size,
                   x264_data[current_queue].uv_size);
            memcpy(x264_data[current_queue].pic.img.plane[2],
                   video + x264_data[current_queue].y_size + x264_data[current_queue].uv_size,
                   x264_data[current_queue].uv_size);

            fprintf(stderr,"[%d] SENDING IN FRAME ENCODER: %p\n",
                    current_queue,
                    x264_data[current_queue].h);
            
            output_size = x264_encoder_encode(x264_data[current_queue].h,
                                              &x264_data[current_queue].nal,
                                              &x264_data[current_queue].i_nal,
                                              &x264_data[current_queue].pic,
                                              &x264_data[current_queue].pic_out);

            if (x264_data[current_queue].i_nal > 0) {
                uint8_t *nal_buffer;

                nal_buffer = (uint8_t*)malloc(output_size+1);
                memcpy(nal_buffer, x264_data[current_queue].nal->p_payload, output_size);
                
                /*
                static FILE *dbfile = NULL;
                if (!dbfile) {
                    dbfile = fopen("debug.264","wb");
                }
                if (dbfile) {
                    fwrite(x264_data[current_queue].nal->p_payload,
                           output_size,
                           1,
                           dbfile);
                    fflush(dbfile);
                }*/
                
                fprintf(stderr,"RECEIVED ENCODED FRAME OUTPUT:%d\n", output_size);
                pts = x264_data[current_queue].pic_out.i_pts;
                dts = x264_data[current_queue].pic_out.i_dts;                
                video_sink_frame_callback(core, nal_buffer, output_size, pts, dts, current_queue);
            }            
                   
            if (msg) {
                free(msg->buffer);
                free(msg);
            }            
        }
    }
cleanup_video_encode_thread:

    for (current_encoder = 0; current_encoder < num_outputs; current_encoder++) {
        if (x264_data[current_encoder].h) {
            x264_encoder_close(x264_data[current_encoder].h);
        }
    }
    
    return NULL;
}

typedef struct _scale_struct_
{
    uint8_t *output_data[4];
    int output_stride[4];
} scale_struct;

void copy_image_from_ffmpeg(uint8_t *source, int source_width, int source_height, AVFrame *source_frame)
{
    //
}

void copy_image_to_ffmpeg(uint8_t *source, int source_width, int source_height, AVFrame *source_frame)
{    
    int row;
    uint8_t *ysrc = source;
    uint8_t *usrc = ysrc + (source_width * source_height);
    uint8_t *vsrc = usrc + ((source_width/2) * (source_height/2));
    uint8_t *ydst = (uint8_t*)source_frame->data[0];
    uint8_t *udst = (uint8_t*)source_frame->data[1];
    uint8_t *vdst = (uint8_t*)source_frame->data[2];
    for (row = 0; row < source_height; row++) {
        memcpy(ydst, ysrc, source_frame->linesize[0]);
        ysrc += source_width;
        ydst += source_frame->linesize[0];
    }
    for (row = 0; row < source_height / 2; row++) {
        memcpy(udst, usrc, source_frame->linesize[1]);
        usrc += source_width / 2;
        udst += source_frame->linesize[1];
    }
    for (row = 0; row < source_height / 2; row++) {
        memcpy(vdst, vsrc, source_frame->linesize[2]);
        vsrc += source_width / 2;
        vdst += source_frame->linesize[2];
    }
}

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
    int64_t deinterlaced_frame_count[MAX_TRANS_OUTPUTS];
    int64_t sync_frame_count = 0;
    double fps = 30.0;

    for (i = 0; i < MAX_TRANS_OUTPUTS; i++) {
        output_scaler[i] = NULL;
        deinterlaced_frame_count[i] = 0;
    }
    
    params->pixel_fmts = pix_fmts;

    source_frame = av_frame_alloc();
    deinterlaced_frame = av_frame_alloc();    
    
#define MAX_SETTINGS_SIZE 256    
    char settings[MAX_SETTINGS_SIZE];
    const char *filter = "yadif=0:-1:0";
    
    while (video_prepare_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);
        while (!msg && video_prepare_thread_running) {
            usleep(1000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);            
        }

        if (!video_prepare_thread_running) {
            while (msg) {
                if (msg) {
                    free(msg->buffer);
                    free(msg);
                }
                msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);                            
            }
            goto cleanup_video_prepare_thread;
        }

        if (msg) {
            int width = msg->width;
            int height = msg->height;
            
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

            copy_image_to_ffmpeg(msg->buffer, width, height, source_frame);

            av_buffersrc_add_frame_flags(deinterlacer_source,
                                         source_frame,
                                         AV_BUFFERSRC_FLAG_KEEP_REF);

            while (video_prepare_thread_running) {
                int retcode;
                int output_ready = 1;

                fprintf(stderr,"SENDING VIDEO FRAME INTO DEINTERLACER\n");
                retcode = av_buffersink_get_frame(deinterlacer_output, deinterlaced_frame);
                if (retcode == AVERROR(EAGAIN) ||
                    retcode == AVERROR(AVERROR_EOF)) {
                    output_ready = 0;
                    break;
                }

                fprintf(stderr,"OUTPUT READY: %d   PTS:%ld DTS:%ld\n", output_ready, msg->pts, msg->dts);
                if (output_ready) {                    
                    // get deinterlaced frame
                    // deinterlaced_frame is where the video is located now
                    for (current_output = 0; current_output < num_outputs; current_output++) {
                        int output_width = core->cd->transvideo_info[current_output].width;
                        int output_height = core->cd->transvideo_info[current_output].height;
                        int video_frame_size = 3 * output_height * output_width / 2;
                        
                        if (output_scaler[current_output] == NULL) {
                            output_scaler[current_output] = sws_getContext(width, height, AV_PIX_FMT_YUV420P,
                                                                           output_width, output_height, AV_PIX_FMT_YUV420P,
                                                                           SWS_BICUBIC, NULL, NULL, NULL); 
                            av_image_alloc(scaled_output[current_output].output_data,
                                           scaled_output[current_output].output_stride,
                                           output_width, output_height, AV_PIX_FMT_YUV420P, 1);
                        }
                        
                        fprintf(stderr,"[%d] SCALING OUTPUT FRAME TO: %d x %d\n",
                                current_output,
                                output_width,
                                output_height);
                        
                        sws_scale(output_scaler[current_output],
                                  deinterlaced_frame->data,
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
                        
                        deinterlaced_buffer = (uint8_t*)malloc(video_frame_size);
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

                        fprintf(stderr,"[%d] FPS:%f\n", current_output, fps);

                        video_stream_struct *vstream = (video_stream_struct*)core->source_stream[0].video_stream;  // only one source stream

                        deinterlaced_frame_count[current_output]++;  // frames since the video start time
                        sync_frame_count = (int64_t)(((((double)deinterlaced_frame->pkt_dts-(double)vstream->first_timestamp) / (double)90000.0))*(double)fps);

                        fprintf(stderr,"\n\nDEINTERLACED_FRAME_COUNT:%ld  SYNC_FRAME_COUNT:%ld   SYNC OFFSET:%.2fms  PKT_DTS:%ld  FT:%ld  FPS:%.2f\n\n",
                                deinterlaced_frame_count[current_output],
                                sync_frame_count,
                                deinterlaced_frame_count[current_output] - sync_frame_count,
                                (((double)deinterlaced_frame_count[current_output] - (double)sync_frame_count)/(double)fps)*(double)1000.0,
                                deinterlaced_frame->pkt_dts,
                                vstream->first_timestamp,
                                fps);
                        
                        encode_msg = (dataqueue_message_struct*)malloc(sizeof(dataqueue_message_struct));
                        encode_msg->buffer = deinterlaced_buffer;
                        encode_msg->buffer_size = video_frame_size;
                        encode_msg->pts = deinterlaced_frame->pkt_pts;  // or pkt_pts?
                        encode_msg->dts = deinterlaced_frame->pkt_dts;
                        encode_msg->tff = 1;
                        encode_msg->interlaced = 0;
                        encode_msg->fps_num = msg->fps_num;
                        encode_msg->fps_den = msg->fps_den;
                        encode_msg->aspect_num = msg->aspect_num;
                        encode_msg->aspect_den = msg->aspect_den;
                        encode_msg->width = output_width;
                        encode_msg->height = output_height;
                        encode_msg->stream_index = current_output;

                        fprintf(stderr,"[%d] SENDING VIDEO FRAME TO ENCODER PTS:%ld DTS:%ld  ASPECT:%d:%d\n",
                                current_output,
                                encode_msg->pts,
                                encode_msg->dts,
                                encode_msg->aspect_num,
                                encode_msg->aspect_den);

                        dataqueue_put_front(core->encodevideo->input_queue[current_output], encode_msg);
                    }                    
                } else {
                    break;
                }
                av_frame_unref(deinterlaced_frame);
            }

            free(msg->buffer);
            msg->buffer = NULL;
            free(msg);
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
        // free output_scaler
        // free scaled_output       
    }
    return NULL;
}

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
                    free(frame->buffer);
                    frame->buffer = NULL;
                }
                free(msg);
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

                retcode = avcodec_send_packet(decode_avctx, decode_pkt);
                if (retcode < 0) {
                    //error decoding video frame-report!
                }

                while (retcode >= 0) {
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
                        }
                    }
                    last_video_frame_size = video_frame_size;

                    output_video_frame = (uint8_t*)malloc(video_frame_size);

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

                    prepare_msg = (dataqueue_message_struct*)malloc(sizeof(dataqueue_message_struct));
                    if (prepare_msg) {
                        prepare_msg->buffer = output_video_frame;
                        prepare_msg->buffer_size = video_frame_size;
                        prepare_msg->pts = decode_av_frame->pts;
                        prepare_msg->dts = decode_av_frame->pkt_dts;  // full time
                        fprintf(stderr,"DECODED VIDEO FRAME: PTS:%ld PKT_DTS:%ld PKT_PTS:%ld\n",
                                decode_av_frame->pts,
                                decode_av_frame->pkt_dts,
                                decode_av_frame->pkt_pts);
                        prepare_msg->tff = is_frame_tff;
                        prepare_msg->interlaced = is_frame_interlaced;

                        //ffmpeg does this inverse because of the 1/X
                        prepare_msg->fps_num = decode_avctx->time_base.den / decode_avctx->ticks_per_frame;  // for 29.97fps- should be 30000 and
                        prepare_msg->fps_den = decode_avctx->time_base.num;                                  // this should be 1001
                        fprintf(stderr,"FPS: %d / %d\n", prepare_msg->fps_num, prepare_msg->fps_den);
                        prepare_msg->aspect_num = decode_avctx->sample_aspect_ratio.num;
                        prepare_msg->aspect_den = decode_avctx->sample_aspect_ratio.den;
                        prepare_msg->width = frame_width;
                        prepare_msg->height = frame_height;

                        dataqueue_put_front(core->preparevideo->input_queue, prepare_msg);
                        //
                    } else {
                        // serious error!
                    }
                }
                free(frame->buffer);
                frame->buffer = NULL;
                free(msg->buffer);
                msg->buffer = NULL;
                free(msg);
                msg = NULL;
            } else {
                //report error condition
            }
        } else {
            //report error condition
        }
    }
cleanup_video_decoder_thread:

    av_frame_free(&decode_av_frame);
    av_packet_free(&decode_pkt);
    avcodec_close(decode_avctx);
    avcodec_free_context(&decode_avctx);
    // finish-- decode_converter free
    // finish-- output_data free
    
    return NULL;
}

int start_video_transcode_threads(fillet_app_struct *core)
{
    video_encode_thread_running = 1;
    pthread_create(&video_encode_thread_id, NULL, video_encode_thread, (void*)core);

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

void *video_encode_thread(void *context)
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
    



