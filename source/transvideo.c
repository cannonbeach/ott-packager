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
#include "transvideo.h"

#if defined(ENABLE_TRANSCODE)

#include "../cbffmpeg/libavcodec/avcodec.h"
#include "../cbffmpeg/libswscale/swscale.h"

static int video_decode_thread_running = 0;
static int video_prepare_thread_running = 0;

void *video_prepare_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;

    while (video_prepare_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->preparevideo->input_queue);
        while (!msg && video_prepare_thread_running) {
            usleep(1000);
        }

        if (!video_prepare_thread_running) {
            if (msg) {
                sorted_frame_struct *frame = (sorted_frame_struct*)msg->buffer;
                if (frame) {
                    free(frame->buffer);
                    frame->buffer = NULL;
                }
                free(msg);
                msg = NULL;               
            }
            goto cleanup_video_prepare_thread;
        }

        if (msg) {
            // deinterlace the frame and scale it to the number of outputs required and then feed the encoders
            free(msg->buffer);
            free(msg);
        }   
    }
    
cleanup_video_prepare_thread:    
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
    
    while (video_decode_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->transvideo->input_queue);
        while (!msg && video_decode_thread_running) {
            usleep(1000);
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
                decode_pkt->dts = frame->full_time;

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
                    if (source_format != output_format) {
                        if (!decode_converter) {
                            decode_converter = sws_getContext(frame_width, frame_height, source_format,
                                                              frame_width, frame_height, output_format,
                                                              SWS_BICUBIC,
                                                              NULL, NULL, NULL); // no dither specified
                            av_image_alloc(output_data, output_stride, frame_width, frame_height, output_format, 1);
                        }

                        source_data[0] = decode_av_frame->data[0];
                        source_data[1] = decode_av_frame->data[1];
                        source_data[2] = decode_av_frame->data[2];
                        source_data[3] = decode_av_frame->data[3];
                        source_stride[0] = decode_av_frame->linesize[0];
                        source_stride[1] = decode_av_frame->linesize[1];
                        source_stride[2] = decode_av_frame->linesize[2];
                        source_stride[3] = decode_av_frame->linesize[3];

                        sws_scale(decode_converter,
                                  (const uint8_t * const*)source_data, source_stride, 0,
                                  frame_height, output_data, output_stride);

                        y_source_video_frame = output_data[0];
                        y_source_stride = output_stride[0];
                        uv_source_stride = output_stride[1];
                    } else {
                        y_source_video_frame = source_data[0];
                        y_source_stride = source_stride[0];
                        uv_source_stride = source_stride[1];
                    }

                    u_source_video_frame = y_source_video_frame + (frame_width * frame_height);
                    v_source_video_frame = u_source_video_frame + (frame_width2 * frame_height2);                    
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
                        prepare_msg->tff = is_frame_tff;
                        prepare_msg->interlaced = is_frame_interlaced;
                        prepare_msg->fps_num = 0;
                        prepare_msg->fps_den = 0;
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
    // decode_converter free
    
    return NULL;
}

void *video_encode_thread(void *context)
{
    return NULL;
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

#endif
    



