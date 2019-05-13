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
#include "transaudio.h"

#if defined(ENABLE_TRANSCODE)

#include "../cbffmpeg/libavcodec/avcodec.h"
#include "../cbffmpeg/libswscale/swscale.h"
#include "../cbffmpeg/libavutil/pixfmt.h"
#include "../cbffmpeg/libavutil/log.h"
#include "../cbffmpeg/libavformat/avformat.h"
#include "../cbffmpeg/libavresample/avresample.h"
#include "../cbffmpeg/libavutil/opt.h"
#include "../cbffmpeg/libavutil/channel_layout.h"
#include "aacenc_lib.h"

typedef struct _startup_buffer_struct_ {
    fillet_app_struct *core;
    int               instance;
} startup_buffer_struct;

static volatile int audio_decode_thread_running = 0;
static volatile int audio_encode_thread_running = 0;
static pthread_t audio_decode_thread_id[MAX_AUDIO_SOURCES];
static pthread_t audio_encode_thread_id[MAX_AUDIO_SOURCES];

int audio_sink_frame_callback(fillet_app_struct *core, uint8_t *new_buffer, int sample_size, int64_t pts, int sub_stream);

void *audio_encode_thread(void *context)
{
    startup_buffer_struct *startup = (startup_buffer_struct*)context;    
    fillet_app_struct *core = (fillet_app_struct*)startup->core;
    int audio_stream = startup->instance;    
    dataqueue_message_struct *msg;
    AACENC_InfoStruct info;
    AACENC_BufDesc source_buf;
    AACENC_BufDesc output_buf;
    AACENC_InArgs source_args;
    AACENC_OutArgs output_args;         
    HANDLE_AACENCODER handle;
    CHANNEL_MODE mode;
    int audio_encoder_ready = 0;
    int channels;
    int output_channels;
    int sample_rate;
    int64_t first_pts = -1;
    int64_t current_duration = 0;
    int requested_length = 0;
    int source_buffer_size = 0;
    uint8_t *source_buffer;
    uint8_t *output_buffer;
    int source_identifier = IN_AUDIO_DATA;
    int output_identifier = OUT_BITSTREAM_DATA;
    int source_elem_size = 2;
    int output_elem_size = 1;
    AACENC_ERROR aac_errcode;
    int output_buffer_size;
    int source_size;
    int64_t encoded_frame_count = 0;
    int output_size;
#define MAX_SOURCE_BUFFER_SIZE 65535
#define MAX_OUTPUT_BUFFER_SIZE 65535

    free(startup);
    startup = NULL;
    
    fprintf(stderr,"status: starting audio encode thread: %d\n", audio_encode_thread_running);

    source_buffer = (uint8_t*)malloc(MAX_SOURCE_BUFFER_SIZE);
    output_buffer = (uint8_t*)malloc(MAX_OUTPUT_BUFFER_SIZE);
    while (audio_encode_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodeaudio[audio_stream]->input_queue);
        while (!msg && audio_encode_thread_running) {
            usleep(1000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodeaudio[audio_stream]->input_queue);
        }

        if (!audio_encode_thread_running) {
            while (msg) {
                if (msg) {
                    memory_return(core->raw_audio_pool, msg->buffer);
                    msg->buffer = NULL;
                    memory_return(core->fillet_msg_pool, msg);
                    msg = NULL;
                }
                msg = (dataqueue_message_struct*)dataqueue_take_back(core->encodeaudio[audio_stream]->input_queue);
            }
            
            goto cleanup_audio_encode_thread;
        }

        channels = msg->channels;
        if (core->cd->enable_stereo) {
            output_channels = 2;
        } else {
            output_channels = channels;
        }
        sample_rate = msg->sample_rate;
        if (first_pts == -1) {
            first_pts = msg->first_pts;
        }

        if (!audio_encoder_ready) {
            int audio_object_type;

            audio_object_type = 2; // let's set it to MPEG4 AAC Low Complexity for now
            // we can support sbr and other advanced mode later
            aacEncOpen(&handle, 0, output_channels);
            aacEncoder_SetParam(handle, AACENC_SAMPLERATE, sample_rate);
            aacEncoder_SetParam(handle, AACENC_AOT, audio_object_type);
            aacEncoder_SetParam(handle, AACENC_CHANNELORDER, 1); // WAV format order- this is what ffmpeg provides            
            if (output_channels == 6) {
                aacEncoder_SetParam(handle, AACENC_CHANNELMODE, MODE_1_2_2_1);
            } else if (output_channels == 2) {
                aacEncoder_SetParam(handle, AACENC_CHANNELMODE, MODE_2);
            } else {
                aacEncoder_SetParam(handle, AACENC_CHANNELMODE, MODE_1);
            }
            aacEncoder_SetParam(handle, AACENC_BITRATE, core->cd->transaudio_info[audio_stream].audio_bitrate * 1000);
            aacEncoder_SetParam(handle, AACENC_TRANSMUX, 2); // adts
            aacEncoder_SetParam(handle, AACENC_AFTERBURNER, 1); // higher quality
            aacEncEncode(handle, NULL, NULL, NULL, NULL);
            aacEncInfo(handle, &info);
            audio_encoder_ready = 1;
        }     

        requested_length = output_channels * 2 * info.frameLength;
        memcpy(source_buffer + source_buffer_size, msg->buffer, msg->buffer_size);
        source_buffer_size += msg->buffer_size;

        while (source_buffer_size >= requested_length) {
            output_buffer_size = MAX_OUTPUT_BUFFER_SIZE;
            source_size = source_buffer_size;
            
            source_buf.numBufs = 1;
            source_buf.bufs = (void**)&source_buffer;
            source_buf.bufferIdentifiers = &source_identifier;
            source_buf.bufSizes = &source_size;
            source_buf.bufElSizes = &source_elem_size;
            source_args.numInSamples = source_buffer_size / 2;
            output_buf.numBufs = 1;
            output_buf.bufs = (void**)&output_buffer;
            output_buf.bufferIdentifiers = &output_identifier;
            output_buf.bufSizes = &output_buffer_size;
            output_buf.bufElSizes = &output_elem_size;
            
            aac_errcode = aacEncEncode(handle,
                                       &source_buf,
                                       &output_buf,
                                       &source_args,
                                       &output_args);
            
            if (aac_errcode != AACENC_OK) {
                fprintf(stderr,"error: unable to encode aac audio!\n");
                // exit?
            }
            source_buffer_size -= requested_length;
            if (source_buffer_size < 0) {
                source_buffer_size = 0;
            } else {
                memmove(source_buffer, source_buffer + requested_length, source_buffer_size);
            }

            output_size = output_args.numOutBytes;
            if (aac_errcode == AACENC_OK && output_size > 0) {
                int sample_duration;
                uint8_t *encoded_output_buffer;

                sample_duration = 1024 * 90000 / sample_rate;
                encoded_frame_count++;
                
                encoded_output_buffer = (uint8_t*)memory_take(core->compressed_audio_pool, output_size);
                if (!encoded_output_buffer) {
                    // handle this
                }
                memcpy(encoded_output_buffer, output_buffer, output_size);

                //fprintf(stderr,"status: encoded audio!  output_size:%d   pts:%ld\n", output_size, first_pts + current_duration);
                audio_sink_frame_callback(core, encoded_output_buffer, output_size, first_pts + current_duration, audio_stream);

                current_duration = (int64_t)encoded_frame_count * (int64_t)sample_duration;
            }
        }
              
        if (msg) {
            memory_return(core->raw_audio_pool, msg->buffer);
            msg->buffer = NULL;
            memory_return(core->fillet_msg_pool, msg);
            msg = NULL;            
        }
    }
cleanup_audio_encode_thread:

    if (audio_encoder_ready) {
        // close it up
        aacEncClose(&handle);
    }
    free(source_buffer);
    free(output_buffer);
    
    return NULL;
}

void *audio_decode_thread(void *context)
{
    startup_buffer_struct *startup = (startup_buffer_struct*)context;    
    fillet_app_struct *core = (fillet_app_struct*)startup->core;
    int audio_stream = startup->instance;        
    AVCodecContext *decode_avctx = NULL;
    AVCodec *decode_codec = NULL;
    AVPacket *decode_pkt = NULL;
    AVFrame *decode_av_frame = NULL;
    int64_t decode_frame_count = 0;
    int audio_decoder_ready = 0;
    dataqueue_message_struct *msg;
    dataqueue_message_struct *encode_msg;
    int64_t expected_audio_data = 0;
    int64_t actual_audio_data = 0;
    int64_t silence_audio_data = 0;
    int64_t first_decoded_pts = -1;
    double previous_delta_time = -1;
    double delta_time;
    double ticks_per_sample;
    AVAudioResampleContext *swr = NULL;
    uint8_t *swr_output_buffer = NULL;
    int output_stride = 0;
    int source_stride = 0;
    int source_samples = 0;
    int output_samples = 0;
    int last_decode_channels = -1;
    int output_channels;
    int64_t last_audio_pts = -1;
    int64_t last_data_amount = 0;
    
    free(startup);
    startup = NULL;
    
    fprintf(stderr,"status: starting audio decode thread: %d\n", audio_decode_thread_running);

    while (audio_decode_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->transaudio[audio_stream]->input_queue);
        while (!msg && audio_decode_thread_running) {
            usleep(1000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->transaudio[audio_stream]->input_queue);
        }

        if (!audio_decode_thread_running) {
            if (msg) {
                sorted_frame_struct *frame = (sorted_frame_struct*)msg->buffer;
                if (frame) {
                    memory_return(core->compressed_audio_pool, frame->buffer);
                    frame->buffer = NULL;
                    memory_return(core->frame_msg_pool, frame);
                    frame = NULL;                    
                }
                memory_return(core->fillet_msg_pool, msg);
                msg = NULL;
            }
            goto cleanup_audio_decoder_thread;
        }

        if (msg) {
            sorted_frame_struct *frame = (sorted_frame_struct*)msg->buffer;
            if (frame) {
                int retcode;

                if (!audio_decoder_ready) {
                    if (frame->media_type == MEDIA_TYPE_AAC) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_AAC);                        
                    } else if (frame->media_type == MEDIA_TYPE_AC3) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_AC3);
                    } else if (frame->media_type == MEDIA_TYPE_EAC3) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_EAC3);
                    } else if (frame->media_type == MEDIA_TYPE_MPEG) {
                        decode_codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
                    } else {
                        //unknown media type- report error and quit!
                        fprintf(stderr,"error: unknown media type - unable to process sample\n");
                        exit(0);
                    }
                    decode_avctx = avcodec_alloc_context3(decode_codec);
                    decode_avctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
                    avcodec_open2(decode_avctx, decode_codec, NULL);
                    decode_av_frame = av_frame_alloc();
                    decode_pkt = av_packet_alloc();
                    audio_decoder_ready = 1;                
                }//!audio_decoder_ready

                int current_sample_out = 0;
                int64_t last_full_time = 0;
                uint8_t *incoming_audio_buffer = frame->buffer;
                int incoming_audio_buffer_size = frame->buffer_size;

                core->decoded_source_info.decoded_audio_media_type[audio_stream] = frame->media_type;

                //sometimes the audio frames are concatenated, especially coming from
                //an mpeg2 transport stream

                decode_pkt->size = incoming_audio_buffer_size;
                decode_pkt->data = incoming_audio_buffer;
                decode_pkt->pts = frame->pts;
                decode_pkt->dts = frame->full_time;

                retcode = avcodec_send_packet(decode_avctx, decode_pkt);
                if (retcode < 0) {
                    //error decoding audio frame-report!
                    fprintf(stderr,"error: unable to decode audio frame - sorry\n");
                }

                while (retcode >= 0) {
                    int audio_frame_size;
                    int64_t full_time;
                    int64_t pts;
                    int updated_output_buffer_size;
                    uint8_t *decoded_audio_buffer = NULL;
                    int64_t output_pts_full_time;
                    int64_t output_pts;
                    int64_t total_audio_time_output = 0;
                    int64_t diff_audio = 0;                                               
                    
                    retcode = avcodec_receive_frame(decode_avctx, decode_av_frame);
                    if (retcode == AVERROR(EAGAIN) || retcode == AVERROR_EOF) {
                        break;
                    }

                    if (core->cd->enable_stereo) {
                        output_channels = 2;
                    } else {
                        output_channels = decode_avctx->channels;
                    }                    
                    
                    audio_frame_size = av_samples_get_buffer_size(NULL,
                                                                  decode_avctx->channels,
                                                                  decode_av_frame->nb_samples,
                                                                  decode_avctx->sample_fmt,
                                                                  1);

                    ticks_per_sample = (double)(((double)decode_avctx->sample_rate / (double)100000.0)) * (double)2.0 * (double)output_channels;
                    /*fprintf(stderr,"decoded audio_frame_size:%d  channels:%d  samplerate:%d  samplefmt:%s\n",
                            audio_frame_size,
                            decode_avctx->channels,
                            decode_avctx->sample_rate,
                            av_get_sample_fmt_name(decode_avctx->sample_fmt));*/
                    
                    // the ffmpeg decoder should be providing the WAV format order for 5.1- FrontLeft, FrontRight, Center, LFE, Side/BackLeft, Side/BackRight                    
                    decode_frame_count++;

                    if (current_sample_out == 0) {
                        full_time = decode_av_frame->pkt_dts; // full time
                    } else {
                        full_time = last_full_time;
                    }
                    last_full_time = full_time;
                    pts = decode_av_frame->pts;
                    if (first_decoded_pts == -1) {
                        first_decoded_pts = pts;
                    }

                    if (swr) {
                        if (decode_avctx->channels != last_decode_channels && last_decode_channels != -1) {
                            avresample_close(swr);
                            avresample_free(&swr);
                            swr = NULL;
                        }
                    }
                    last_decode_channels = decode_avctx->channels;                    

                    core->decoded_source_info.decoded_audio_channels_input[audio_stream] = decode_avctx->channels;
                    core->decoded_source_info.decoded_audio_channels_output[audio_stream] = output_channels;
                    core->decoded_source_info.decoded_audio_sample_rate[audio_stream] = decode_avctx->sample_rate;
                    
                    if (!swr) {
                        swr = avresample_alloc_context();
                        //we can do 5.1 to 2.0 conversion here
                        if (decode_avctx->channels == 6 && output_channels == 6) {
                            av_opt_set_int(swr,"in_channel_layout",AV_CH_LAYOUT_5POINT1,0);
                            av_opt_set_int(swr,"out_channel_layout",AV_CH_LAYOUT_5POINT1,0);
                        } else if (decode_avctx->channels == 6 && output_channels == 2) {
                            av_opt_set_int(swr,"in_channel_layout",AV_CH_LAYOUT_5POINT1,0);
                            av_opt_set_int(swr,"out_channel_layout",AV_CH_LAYOUT_STEREO,0);
                            //"pan=stereo|FL=FC+0.30*FL+0.30*BL|FR=FC+0.30*FR+0.30*BR"                            
                        } else if (decode_avctx->channels == 1 && output_channels == 2) {
                            av_opt_set_int(swr,"in_channel_layout",AV_CH_LAYOUT_MONO,0);
                            av_opt_set_int(swr,"out_channel_layout",AV_CH_LAYOUT_STEREO,0);                            
                        } else if (decode_avctx->channels == 2) {
                            av_opt_set_int(swr,"in_channel_layout",AV_CH_LAYOUT_STEREO,0);
                            av_opt_set_int(swr,"out_channel_layout",AV_CH_LAYOUT_STEREO,0);
                        } else if (decode_avctx->channels == 1) {
                            av_opt_set_int(swr,"in_channel_layout",AV_CH_LAYOUT_MONO,0);
                            av_opt_set_int(swr,"out_channel_layout",AV_CH_LAYOUT_MONO,0);
                        }
                        av_opt_set_int(swr,"internal_sample_fmt",AV_SAMPLE_FMT_NONE,0);
                        av_opt_set_int(swr,"in_sample_rate",decode_avctx->sample_rate,0);
                        // at some point, it might be useful to bring all of the audio to 48khz for the best
                        // compatibility across devices
                        av_opt_set_int(swr,"out_sample_rate",decode_avctx->sample_rate,0);
                        av_opt_set_int(swr,"in_sample_fmt",decode_avctx->sample_fmt,0);
                        av_opt_set_int(swr,"out_sample_fmt",AV_SAMPLE_FMT_S16,0);
                        avresample_open(swr);
                    }
                    // check sample_fmt to calculate the correct number of samples, right now this assumes more than 16-bit
                    source_samples = audio_frame_size / (2 * 2 * decode_avctx->channels);
                    output_samples = avresample_available(swr)+
                        av_rescale_rnd(avresample_get_delay(swr)+
                                       source_samples,decode_avctx->sample_rate,decode_avctx->sample_rate,AV_ROUND_UP);

                    if (!swr_output_buffer) {
                        av_samples_alloc(&swr_output_buffer,
                                         &output_stride,
                                         decode_avctx->channels,
                                         output_samples*2,
                                         AV_SAMPLE_FMT_S16,
                                         0);
                    }

                    output_samples = avresample_convert(swr,
                                                        &swr_output_buffer,
                                                        output_stride,
                                                        source_samples,
                                                        &decode_av_frame->data[0],
                                                        source_stride,
                                                        source_samples);
                    if (core->cd->enable_stereo) {                     
                        updated_output_buffer_size = output_samples * 2 * output_channels;
                    } else {
                        updated_output_buffer_size = output_samples * 2 * decode_avctx->channels;
                    }
                    delta_time = (double)full_time - (double)first_decoded_pts;

                    diff_audio = 0;
                    if (current_sample_out == 0) {
                        if (previous_delta_time != -1 && ticks_per_sample != 0) {
                            expected_audio_data = (int64_t)((double)delta_time / (double)0.9 * (double)ticks_per_sample);
                            diff_audio = expected_audio_data - actual_audio_data;                            
                            fprintf(stderr,"expected audio data:%ld   actual audio data:%ld   full_time:%ld delta:%ld\n",
                                    expected_audio_data, actual_audio_data, full_time, diff_audio);
                            //check how much source audio we have vs. how much we should have
                            //if audio is missing, then there could be some missing data
                            //that would throw off the a/v sync for the mp4 file output mode
                            //so we have to introduce silence pcm to compensate for the "missing" data
                            //and if things are just really bad and unrecoverable for some unknown reason
                            //then we'll just have the application quit out based on some threshold
                            //and rely on the docker container to restart the application in a known healthy state
                            int quit_threshold = 16384*output_channels;
                            if (diff_audio > quit_threshold ||
                                diff_audio < -quit_threshold) {
                                fprintf(stderr,"fatal error: a/v sync is off - too much or too little audio is present - %ld samples\n", diff_audio);
                                syslog(LOG_ERR,"fatal error: a/v sync is off - too much or too little audio is present - %ld samples\n", diff_audio);
                                exit(0);
                            }
                        }
                    }
                    previous_delta_time = delta_time;
                    actual_audio_data += updated_output_buffer_size;

                    // check size of updated_output_buffer_size+1
                    if (updated_output_buffer_size > 65535 || updated_output_buffer_size < 0) {
                        fprintf(stderr,"warning: output buffer size is excessively large or malformed: %d \n", updated_output_buffer_size); 
                        updated_output_buffer_size = 65535;
                    }
                    
                    decoded_audio_buffer = (uint8_t*)memory_take(core->raw_audio_pool, updated_output_buffer_size);
                    if (!decoded_audio_buffer) {
                        fprintf(stderr,"fatal error: unable to obtain decoded_audio_buffer from pool!\n");
                        syslog(LOG_ERR,"fatal error: unable to obtain decoded_audio_buffer from pool!\n");
                        exit(0);
                    }
                    // check size
                    memcpy(decoded_audio_buffer, swr_output_buffer, updated_output_buffer_size);

                    if (current_sample_out == 0) {
                        if (last_audio_pts != -1) {
                            int64_t diff = decode_av_frame->pkt_dts - last_audio_pts;
                            int64_t correct_data;
                            
                            correct_data = (int64_t)((double)diff / (double)0.9 * (double)ticks_per_sample);

                            /*fprintf(stderr,"\n\n\nAUDIO DECODED: SIZE:%d CORRECT:%ld  DIFF:%ld\n\n\n",
                                    last_data_amount,
                                    correct_data,
                                    diff);*/
                        }
                        last_audio_pts = decode_av_frame->pkt_dts;
                        last_data_amount = updated_output_buffer_size;
                    } else {
                        last_data_amount += updated_output_buffer_size;
                    }

                    total_audio_time_output = (int64_t)((double)ticks_per_sample * (double)actual_audio_data);
                    output_pts_full_time = (int64_t)first_decoded_pts + (int64_t)total_audio_time_output;
                    output_pts = output_pts_full_time % (int64_t)8589934592;
                    
                    // 30720
                    // 6144
                    while (diff_audio >= updated_output_buffer_size) {
                        uint8_t *filler_decoded_audio_buffer;
                        filler_decoded_audio_buffer = (uint8_t*)memory_take(core->raw_audio_pool, updated_output_buffer_size);
                        if (!filler_decoded_audio_buffer) {
                            fprintf(stderr,"fatal error: unable to obtain filler_decoded_audio_buffer from pool!\n");
                            syslog(LOG_ERR,"fatal error: unable to obtain filler_decoded_audio_buffer from pool!\n");
                            exit(0);                            
                        }
                        memset(filler_decoded_audio_buffer, 0, updated_output_buffer_size);
                        encode_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                        if (encode_msg) {
                            encode_msg->buffer = filler_decoded_audio_buffer;
                            encode_msg->buffer_size = updated_output_buffer_size;
                            encode_msg->pts = output_pts;//decode_av_frame->pts;
                            encode_msg->dts = output_pts_full_time; //decode_av_frame->pkt_dts; //full time
                            encode_msg->channels = decode_avctx->channels;
                            encode_msg->sample_rate = decode_avctx->sample_rate;
                            encode_msg->first_pts = first_decoded_pts;
                            dataqueue_put_front(core->encodeaudio[audio_stream]->input_queue, encode_msg);                        
                        }
                        current_sample_out++;
                        actual_audio_data += updated_output_buffer_size;
                        diff_audio -= updated_output_buffer_size;

                        syslog(LOG_INFO,"SILENCE INSERTION-MISSING AUDIO!! MAINTAINING A/V SYNC\n");
                        fprintf(stderr,"SILENCE INSERTION-MISSING AUDIO!! MAINTAINING A/V SYNC\n");

                        total_audio_time_output = (int64_t)((double)ticks_per_sample * (double)actual_audio_data);
                        output_pts_full_time = (int64_t)first_decoded_pts + (int64_t)total_audio_time_output;
                        output_pts = output_pts_full_time % (int64_t)8589934592;                        
                    }
                    if (diff_audio <= -updated_output_buffer_size) {
                        syslog(LOG_INFO,"AUDIO DROPPED-GETTING AHEAD-TOO MUCH AUDIO!! MAINTAINING A/V SYNC\n");
                        fprintf(stderr,"AUDIO DROPPED-GETTING AHEAD-TOO MUCH AUDIO!! MAINTAINING A/V SYNC\n");
                        memory_return(core->raw_audio_pool, decoded_audio_buffer);
                        decoded_audio_buffer = NULL;
                        actual_audio_data -= updated_output_buffer_size;
                        diff_audio += updated_output_buffer_size;
                    } else {                                            
                        encode_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                        if (encode_msg) {
                            encode_msg->buffer = decoded_audio_buffer;
                            encode_msg->buffer_size = updated_output_buffer_size;
                            encode_msg->pts = output_pts; //decode_av_frame->pts;
                            encode_msg->dts = output_pts_full_time; //decode_av_frame->pkt_dts; //full time
                            encode_msg->channels = decode_avctx->channels;
                            encode_msg->sample_rate = decode_avctx->sample_rate;
                            encode_msg->first_pts = first_decoded_pts;
                            dataqueue_put_front(core->encodeaudio[audio_stream]->input_queue, encode_msg);                        
                        }
                        current_sample_out++;
                    }
                } // while (retcode >= 0) 
                memory_return(core->compressed_audio_pool, frame->buffer);
                frame->buffer = NULL;                            
                memory_return(core->frame_msg_pool, frame);
                frame = NULL;
            } // if frame
            memory_return(core->fillet_msg_pool, msg);
            msg = NULL;
        } else {
            //report error condition!
        }
    }

cleanup_audio_decoder_thread:
    
    av_frame_free(&decode_av_frame);
    av_packet_free(&decode_pkt);
    avcodec_close(decode_avctx);
    avcodec_free_context(&decode_avctx);
    avresample_close(swr);
    avresample_free(&swr);
    av_freep(&swr_output_buffer);
    // others?
        
    return NULL;
}

int start_audio_transcode_threads(fillet_app_struct *core)
{
    int current_audio;  
    audio_encode_thread_running = 1;

    for (current_audio = 0; current_audio < MAX_AUDIO_SOURCES; current_audio++) {
        startup_buffer_struct *startup;
        startup = (startup_buffer_struct*)malloc(sizeof(startup_buffer_struct));
        startup->instance = current_audio;
        startup->core = core;
        pthread_create(&audio_encode_thread_id[current_audio], NULL, audio_encode_thread, (void*)startup);
    }
    
    audio_decode_thread_running = 1;
    for (current_audio = 0; current_audio < MAX_AUDIO_SOURCES; current_audio++) {
        startup_buffer_struct *startup;
        startup = (startup_buffer_struct*)malloc(sizeof(startup_buffer_struct));
        startup->instance = current_audio;
        startup->core = core;        
        pthread_create(&audio_decode_thread_id[current_audio], NULL, audio_decode_thread, (void*)startup);
    }

    return 0;
}

int stop_audio_transcode_threads(fillet_app_struct *core)
{
    int current_audio;
    
    audio_decode_thread_running = 0;
    for (current_audio = 0; current_audio < MAX_AUDIO_SOURCES; current_audio++) {
        pthread_join(audio_decode_thread_id[current_audio], NULL);
    }

    audio_encode_thread_running = 0;
    for (current_audio = 0; current_audio < MAX_AUDIO_SOURCES; current_audio++) {
        pthread_join(audio_encode_thread_id[current_audio], NULL);
    }

    return 0;
}

#else // ENABLE_TRANSCODE

void *audio_decode_thread(void *context)
{
    fprintf(stderr,"AUDIO DECODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

void *audio_encode_thread(void *context)
{
    fprintf(stderr,"AUDIO ENCODING NOT ENABLED - PLEASE RECOMPILE IF REQUIRED\n");
    return NULL;
}

int start_audio_transcode_threads(fillet_app_struct *core)
{
    return 0;
}

int stop_audio_transcode_threads(fillet_app_struct *core)
{
    return 0;
}

#endif
    



