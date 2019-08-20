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
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <math.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <execinfo.h>
#include "fillet.h"
#include "fgetopt.h"
#include "dataqueue.h"
#include "mempool.h"
#include "udpsource.h"
#include "tsreceive.h"
#include "tsdecode.h"
#include "hlsmux.h"
#include "mp4core.h"
#include "background.h"
#include "webdav.h"
#if defined(ENABLE_TRANSCODE)
#include "transvideo.h"
#include "transaudio.h"
#endif // ENABLE_TRANSCODE

#define SOURCE_NONE      -1
#define SOURCE_IP        1
#define SOURCE_FILE      2

static int calculated_mux_rate = 0;
static error_struct error_data[MAX_ERROR_SIZE];
static int64_t error_count = 0;
static int video_synchronizer_entries = 0;
static int audio_synchronizer_entries = 0;
static pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
static int quit_sync_thread = 0;
static int sync_thread_running = 0;
static pthread_t frame_sync_thread_id;
static int enable_verbose = 0;
static int enable_transcode = 0;
static int enable_scte35 = 0;
static int enable_stereo = 0;
static int enable_webvtt = 0;
static int enable_fmp4 = 0;
static int enable_youtube = 0;
static int enable_ts = 0;
static int audio_streams = -1;

static config_options_struct config_data;

static struct option long_options[] = {
     {"sources", required_argument, 0, 'S'},
     {"ip", required_argument, 0, 'i'},
     {"verbose", no_argument, &enable_verbose, 1},
     {"interface", required_argument, 0, 'f'},
     {"window", required_argument, 0, 'w'},
     {"segment", required_argument, 0, 's'},
     {"manifest", required_argument, 0, 'm'},
     {"rollover", required_argument, 0, 'r'},
     {"identity", required_argument, 0, 'u'},
     {"dash", no_argument, &enable_fmp4, 'd'},
     {"hls", no_argument, &enable_ts, 'h'},
     {"google", no_argument, &enable_youtube, 'y'},
     {"manifest-dash", required_argument, 0, 'M'},
     {"manifest-hls", required_argument, 0, 'H'},
     {"manifest-fmp4", required_argument, 0, 'F'},
     {"webvtt", no_argument, &enable_webvtt, 'W'},     
     {"astreams", required_argument, 0, 'T'},
     {"cdnusername", required_argument, 0, '7'},
     {"cdnpassword", required_argument, 0, '8'},
     {"cdnserver", required_argument, 0, '9'},
#if defined(ENABLE_TRANSCODE)
     {"transcode", no_argument, &enable_transcode, 'z'},
     {"outputs", required_argument, 0, 'o'},              // number of output profiles
     {"vcodec", required_argument, 0, 'c'},               // video output codec used    --vcodec hevc or --vcodec h264
     {"resolutions", required_argument, 0, 'e'},          // the resolutions            --resolutions 320x240,640x360,960x540,1280x720
     {"vrate", required_argument, 0, 'v'},                // the video bitrates (kbps)  --vrate 800,1250,2500,5000
     {"acodec", required_argument, 0, 'a'},               // audio output codec used    --acodec aac or --acodec ac3
     {"arate", required_argument, 0, 't'},                // the audio bitrates (kbps)  --arate 128,96
     {"aspect", required_argument, 0, 'A'},               // force the aspect ratio (16:9,4:3, or other)
     {"scte35", no_argument, &enable_scte35, '3'},        // enable scte35 passthrough
     {"stereo", no_argument, &enable_stereo, '2'},
     {"quality", required_argument, 0, 'q'},              // set the quality level
     {"profile", required_argument, 0, 'p'},              // set the encoder profile level
#endif // ENABLE_TRANSCODE
     {"youtube", required_argument, 0, 'C'},              // the youtube cid
     {0, 0, 0, 0}
};

void crash_handler(int sig)
{
#define MAX_TRACE 10    
    void *array[MAX_TRACE];
    size_t size;

    size = backtrace(array, MAX_TRACE);

    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    exit(1);
}

#if defined(ENABLE_TRANSCODE)
static int create_transvideo_core(fillet_app_struct *core)
{
    int i;
    
    core->transvideo = (transvideo_internal_struct*)malloc(sizeof(transvideo_internal_struct));
    core->preparevideo = (preparevideo_internal_struct*)malloc(sizeof(preparevideo_internal_struct));
    core->encodevideo = (encodevideo_internal_struct*)malloc(sizeof(encodevideo_internal_struct));
    
    core->transvideo->input_queue = (void*)dataqueue_create();
    core->preparevideo->input_queue = (void*)dataqueue_create();

    for (i = 0; i < MAX_AUDIO_SOURCES; i++) {
        core->transaudio[i] = (transaudio_internal_struct*)malloc(sizeof(transaudio_internal_struct));
        core->transaudio[i]->input_queue = (void*)dataqueue_create();
        core->encodeaudio[i] = (encodeaudio_internal_struct*)malloc(sizeof(encodeaudio_internal_struct));
        core->encodeaudio[i]->input_queue = (void*)dataqueue_create();
    }
        
    for (i = 0; i < MAX_TRANS_OUTPUTS; i++) {
        core->encodevideo->input_queue[i] = (void*)dataqueue_create();
    }
    core->encodevideo->thumbnail_queue = (void*)dataqueue_create();

    start_video_transcode_threads(core);
    start_audio_transcode_threads(core);
    
    return 0;
}

static int destroy_transvideo_core(fillet_app_struct *core)
{
    int i;

    stop_video_transcode_threads(core);
    stop_audio_transcode_threads(core);
    
    for (i = 0; i < MAX_TRANS_OUTPUTS; i++) {
        dataqueue_destroy(core->encodevideo->input_queue[i]);
        core->encodevideo->input_queue[i] = NULL;
    }
    for (i = 0; i < MAX_AUDIO_SOURCES; i++) {
        dataqueue_destroy(core->transaudio[i]->input_queue);
        core->transaudio[i]->input_queue = NULL;
        free(core->transaudio[i]);
        core->transaudio[i] = NULL;
        dataqueue_destroy(core->encodeaudio[i]->input_queue);
        core->encodeaudio[i]->input_queue = NULL;
        free(core->encodeaudio[i]);
        core->encodeaudio[i] = NULL;
    }
    dataqueue_destroy(core->encodevideo->thumbnail_queue);
    core->encodevideo->thumbnail_queue = NULL;
    dataqueue_destroy(core->preparevideo->input_queue);
    core->preparevideo->input_queue = NULL;
    dataqueue_destroy(core->transvideo->input_queue);
    core->transvideo->input_queue = NULL;
    free(core->preparevideo);
    core->preparevideo = NULL;
    free(core->transvideo);
    core->transvideo = NULL;
    free(core->encodevideo);
    core->encodevideo = NULL;
    
    return 0;
}
#endif // ENABLE_TRANSCODE    

static int destroy_fillet_core(fillet_app_struct *core)
{
    int num_sources;
    int current_source;
    
    if (!core) {
	return -1;
    }

#if defined(ENABLE_TRANSCODE)
    destroy_transvideo_core(core);
#endif

#if defined(ENABLE_TRANSCODE)
    num_sources = MAX_TRANS_OUTPUTS;
#else    
    num_sources = core->num_sources;
#endif // ENABLE_TRANSCODE    
    for (current_source = 0; current_source < num_sources; current_source++) {
	video_stream_struct *vstream;
	audio_stream_struct *astream;
	int audio_source;

	for (audio_source = 0; audio_source < MAX_AUDIO_SOURCES; audio_source++) {
	    astream = (audio_stream_struct*)core->source_stream[current_source].audio_stream[audio_source];
            if (astream->audio_queue) {
                dataqueue_destroy(astream->audio_queue);
                astream->audio_queue = NULL;
            }
	    
	    free(core->source_stream[current_source].audio_stream[audio_source]);
	    core->source_stream[current_source].audio_stream[audio_source] = NULL;
	}
	vstream = (video_stream_struct*)core->source_stream[current_source].video_stream;
        if (vstream->video_queue) {
            dataqueue_destroy(vstream->video_queue);
            vstream->video_queue = NULL;
        }
	free(core->source_stream[current_source].video_stream);
	core->source_stream[current_source].video_stream = NULL;		 
    }
    if (core->event_queue) {
        dataqueue_destroy(core->event_queue);
        core->event_queue = NULL;
    }
    if (core->webdav_queue) {
        dataqueue_destroy(core->webdav_queue);
        core->webdav_queue = NULL;
    }
    if (core->signal_queue) {
        dataqueue_destroy(core->signal_queue);
        core->signal_queue = NULL;
    }
    memory_destroy(core->fillet_msg_pool);
    memory_destroy(core->frame_msg_pool);
    memory_destroy(core->compressed_video_pool);
    memory_destroy(core->compressed_audio_pool);
    memory_destroy(core->raw_video_pool);
    memory_destroy(core->raw_audio_pool);
    
    free(core);

    return 0;
}

static fillet_app_struct *create_fillet_core(config_options_struct *cd, int num_sources)
{
    fillet_app_struct *core;
    int current_source;

    core = (fillet_app_struct*)malloc(sizeof(fillet_app_struct));
    if (!core) {
	return NULL;
    }
    
    core->num_sources = num_sources;
    core->cd = cd;
#if defined(ENABLE_TRANSCODE)
    num_sources = MAX_TRANS_OUTPUTS;
#endif // ENABLE_TRANSCODE        
    core->source_stream = (source_stream_struct*)malloc(sizeof(source_stream_struct)*num_sources);
    core->event_queue = (void*)dataqueue_create();
    core->webdav_queue = (void*)dataqueue_create();
    core->signal_queue = (void*)dataqueue_create();
    
    memset(core->source_stream, 0, sizeof(source_stream_struct)*num_sources);

#if defined(ENABLE_TRANSCODE)
    // pre-allocate these for the transcode
    core->fillet_msg_pool = memory_create(MAX_MSG_BUFFERS, sizeof(dataqueue_message_struct));
    core->frame_msg_pool = memory_create(MAX_FRAME_BUFFERS, sizeof(sorted_frame_struct));
#else
    core->fillet_msg_pool = memory_create(MAX_MSG_BUFFERS, 0);
    core->frame_msg_pool = memory_create(MAX_FRAME_BUFFERS, 0);
#endif
    // this will limit memory usage for transcode so that it never turns into a
    // neverending malloc causing other instances of the app to fail
    core->compressed_video_pool = memory_create(MAX_VIDEO_COMPRESSED_BUFFERS, 0);
    core->compressed_audio_pool = memory_create(MAX_AUDIO_COMPRESSED_BUFFERS, 0);
    core->raw_video_pool = memory_create(MAX_VIDEO_RAW_BUFFERS, 0);
    core->raw_audio_pool = memory_create(MAX_AUDIO_RAW_BUFFERS, 0);

    for (current_source = 0; current_source < num_sources; current_source++) {
	video_stream_struct *vstream;
	audio_stream_struct *astream;
	int audio_source;

	for (audio_source = 0; audio_source < MAX_AUDIO_SOURCES; audio_source++) {
	    core->source_stream[current_source].audio_stream[audio_source] = (audio_stream_struct*)malloc(sizeof(audio_stream_struct));
	    if (!core->source_stream[current_source].audio_stream[audio_source]) {
		goto _fillet_create_fail;
	    }	    
	    astream = (audio_stream_struct*)core->source_stream[current_source].audio_stream[audio_source];
	    memset(astream, 0, sizeof(audio_stream_struct));
	    astream->audio_queue = (void*)dataqueue_create();
	    astream->last_timestamp_pts = -1;
	}
	
	core->source_stream[current_source].video_stream = (video_stream_struct*)malloc(sizeof(video_stream_struct));
	if (!core->source_stream[current_source].video_stream) {
	    goto _fillet_create_fail;
	}
	
	vstream = (video_stream_struct*)core->source_stream[current_source].video_stream;
	memset(vstream, 0, sizeof(video_stream_struct));
	vstream->last_timestamp_pts = -1;
	vstream->last_timestamp_dts = -1;
	vstream->video_queue = (void*)dataqueue_create();
    }
#if defined(ENABLE_TRANSCODE)
    create_transvideo_core(core);
#endif

    return core;

_fillet_create_fail:

#if defined(ENABLE_TRANSCODE)
    destroy_transvideo_core(core);
#endif    
    destroy_fillet_core(core);    
    
    return NULL;
}

static int parse_input_options(int argc, char **argv)
{
     if (argc == 1) {
	 return -1;
     }

     while (1) {
          int option_index = -1;
          int c;

          c = fgetopt_long(argc,
                           argv,
                           "C:w:s:f:i:S:r:u:o:c:e:v:a:t:d:h:A:m:M:H:F:3:2:q:p:W:T",
                           long_options,
                           &option_index);

          if (c == -1) {
               break;
          }

          switch (c) {
          case 'o':
#if defined(ENABLE_TRANSCODE)              
              if (optarg) {
                  config_data.num_outputs = atoi(optarg);
                  if (config_data.num_outputs > MAX_TRANS_OUTPUTS || config_data.num_outputs < 1) {
                      fprintf(stderr,"ERROR: Invalid number of outputs provided: %d\n", config_data.num_outputs);
                      return -1;
                  }
              } else {
                  fprintf(stderr,"ERROR: No outputs provided\n");
                  return -1;
              }
              fprintf(stderr,"STATUS: Number of transcoded outputs: %d\n", config_data.num_outputs);
#endif // ENABLE_TRANSCODE              
              break;
          case 'p':
#if defined(ENABLE_TRANSCODE)
              if (optarg) {
                  int c;                  
                  // eventually add combined h264/hevc encoding
                  if (strncmp(optarg,"base",4)==0) {
                      for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                          config_data.transvideo_info[c].encoder_profile = ENCODER_PROFILE_BASE;
                      }
                  } else if (strncmp(optarg,"main",4)==0) {
                      for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                          config_data.transvideo_info[c].encoder_profile = ENCODER_PROFILE_MAIN;
                      }
                  } else if (strncmp(optarg,"high",4)==0) {
                      for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                          config_data.transvideo_info[c].encoder_profile = ENCODER_PROFILE_HIGH;
                      }                      
                  } else {
                      fprintf(stderr,"ERROR: Invalid video codec was selected\n");
                      return -1;
                  }
              } else {
                  int c;
                  for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                      config_data.transvideo_info[c].encoder_profile = ENCODER_PROFILE_MAIN;
                  }                                        
              }
#endif // ENABLE_TRANSCODE              
              break;
          case 'q':
#if defined(ENABLE_TRANSCODE)
              if (optarg) {
                  int c;
                  for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                      config_data.transvideo_info[c].encoder_quality = atoi(optarg);
                      if (config_data.transvideo_info[c].encoder_quality < ENCODER_QUALITY_LOW ||
                          config_data.transvideo_info[c].encoder_quality > ENCODER_QUALITY_CRAZY) {
                          config_data.transvideo_info[c].encoder_quality = ENCODER_QUALITY_MEDIUM;
                          fprintf(stderr,"ERROR: Unknown quality provided- defaulting to MEDIUM\n");
                      }
                  }
              } else {
                  int c;
                  for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                      config_data.transvideo_info[c].encoder_quality = ENCODER_QUALITY_MEDIUM;
                  }
              }
#endif // ENABLE_TRANSCODE
              break;
          case 'c':
#if defined(ENABLE_TRANSCODE)              
              if (optarg) {
                  int c;                  
                  // eventually add combined h264/hevc encoding
                  if (strncmp(optarg,"hevc",4)==0) {
                      // hevc selected as output codec
                      for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                          config_data.transvideo_info[c].video_codec = STREAM_TYPE_HEVC;
                      }
                  } else if (strncmp(optarg,"h264",4)==0) {
                      // h264 selected as output codec
                      for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
                          config_data.transvideo_info[c].video_codec = STREAM_TYPE_H264;
                      }
                  } else {
                      fprintf(stderr,"ERROR: Invalid video codec was selected\n");
                      return -1;
                  }
              } else {
                  fprintf(stderr,"ERROR: No output video codec was selected\n");
                  return -1;
              }
#endif // ENABLE_TRANSCODE              
              break;
          case 'A':
#if defined(ENABLE_TRANSCODE)
              if (optarg) {
                  int c;
                  int optarg_len = strlen(optarg);
                  int nidx = 0;
                  int didx = 0;
                  int parsing_num = 1;
                  char numstr[MAX_STR_SIZE];
                  char denstr[MAX_STR_SIZE];
                  int outputidx = 0;
                  memset(numstr,0,sizeof(numstr));
                  memset(denstr,0,sizeof(denstr));
                  for (c = 0; c < optarg_len; c++) {
                      if (parsing_num) {
                          if (optarg[c] != ':' && optarg[c] != '\0') {
                              numstr[nidx++] = optarg[c];
                              if (nidx >= MAX_STR_SIZE) {
                                  fprintf(stderr,"ERROR: String size exceeded when parsing for num\n");
                                  return -1;
                              }
                          } else {
                              config_data.transvideo_info[outputidx].aspect_num = atoi(numstr);
                              parsing_num = 0;
                          }
                      } else {
                          if (optarg[c] != '\0') {
                              denstr[didx++] = optarg[c];
                              if (didx >= MAX_STR_SIZE) {
                                  fprintf(stderr,"ERROR: String size exceeded when parsing for den\n");
                                  return -1;
                              }
                          }
                          
                          if (optarg[c] == '\0' || c == optarg_len - 1) {
                              config_data.transvideo_info[outputidx].aspect_den = atoi(denstr);
                              outputidx++;
                              /*if (outputidx >= MAX_TRANS_OUTPUTS) {
                                  fprintf(stderr,"ERROR: Exceeded number of allowed video transcoder outputs (aspect parsing)\n");
                                  return -1;
                              }*/
                              nidx = 0;
                              didx = 0;

                              fprintf(stderr,"STATUS: output stream(%d) aspect ratio detected: %d:%d\n",
                                      outputidx-1,
                                      config_data.transvideo_info[outputidx-1].aspect_num,
                                      config_data.transvideo_info[outputidx-1].aspect_den);
                              break;
                          }                          
                      }
                  }                  
              }
#endif // ENABLE_TRANSCODE              
              break;
          case 'e':
#if defined(ENABLE_TRANSCODE)              
              if (optarg) {
                  int c;
                  int optarg_len = strlen(optarg);
                  int widx = 0;
                  int hidx = 0;
                  int parsing_width = 1;
                  char widthstr[MAX_STR_SIZE];
                  char heightstr[MAX_STR_SIZE];
                  int outputidx = 0;
                  memset(widthstr,0,sizeof(widthstr));
                  memset(heightstr,0,sizeof(heightstr));
                  for (c = 0; c < optarg_len; c++) {
                      if (parsing_width) {
                          if (optarg[c] != 'x' && optarg[c] != '\0') {                              
                              widthstr[widx++] = optarg[c];
                              if (widx >= MAX_STR_SIZE) {
                                  fprintf(stderr,"ERROR: String size exceeded when parsing for width\n");
                                  return -1;
                              }
                          } else {
                              config_data.transvideo_info[outputidx].width = atoi(widthstr);
                              parsing_width = 0;
                          }
                      } else {
                          if (optarg[c] != ',' && optarg[c] != '\0') {
                              heightstr[hidx++] = optarg[c];
                              if (hidx >= MAX_STR_SIZE) {
                                  fprintf(stderr,"ERROR: String size exceeded when parsing for height\n");
                                  return -1;
                              }
                          }

                          if (optarg[c] == ',' || optarg[c] == '\0' || c == optarg_len - 1) {
                              config_data.transvideo_info[outputidx].height = atoi(heightstr);
                              outputidx++;
                              if (outputidx >= MAX_TRANS_OUTPUTS) {
                                  fprintf(stderr,"ERROR: Exceeded number of allowed video transcoder outputs (resolution parsing)\n");
                                  return -1;
                              }
                              widx = 0;
                              hidx = 0;
                              fprintf(stderr,"STATUS: output stream(%d) resolution detected: %d x %d\n",
                                      outputidx-1,
                                      config_data.transvideo_info[outputidx-1].width,
                                      config_data.transvideo_info[outputidx-1].height);
                              parsing_width = 1;
                              memset(widthstr,0,sizeof(widthstr));
                              memset(heightstr,0,sizeof(heightstr));
                          }
                      }
                  }
              } else {
                  fprintf(stderr,"ERROR: No output resolutions specified\n");
                  return -1;
              }
#endif // ENABLE_TRANSCODE              
              break;
          case 'v':
#if defined(ENABLE_TRANSCODE)              
              if (optarg) {
                  int c;
                  int optarg_len = strlen(optarg);
                  char bitratestr[MAX_STR_SIZE];
                  int bidx = 0;
                  int outputidx = 0;
                  memset(bitratestr,0,sizeof(bitratestr));
                  for (c = 0; c < optarg_len; c++) {
                      if (optarg[c] != ',' && optarg[c] != '\0') {
                          bitratestr[bidx++] = optarg[c];
                          if (bidx >= MAX_STR_SIZE) {
                              fprintf(stderr,"ERROR: String size exceeded when parsing for bitrate\n");
                              return -1;
                          }
                      }
                      
                      if (optarg[c] == ',' || optarg[c] == '\0' || c == optarg_len - 1) {
                          config_data.transvideo_info[outputidx].video_bitrate = atoi(bitratestr);
                          if (config_data.transvideo_info[outputidx].video_bitrate <= 0) {
                              fprintf(stderr,"ERROR: Invalid video bitrate specified\n");
                              return -1;
                          }                              
                          outputidx++;
                          if (outputidx >= MAX_TRANS_OUTPUTS) {
                              fprintf(stderr,"ERROR: Exceeded number of allowed video transcoder outputs (bitrate parsing)\n");
                              return -1;
                          }                          
                          bidx = 0;
                          fprintf(stderr,"STATUS: output stream(%d) video bitrate: %d\n",
                                  outputidx-1,
                                  config_data.transvideo_info[outputidx-1].video_bitrate);
                          memset(bitratestr,0,sizeof(bitratestr));                          
                      }
                  }
              }
#endif // ENABLE_TRANSCODE              
              break;
          case 'a':
#if defined(ENABLE_TRANSCODE)              
              if (optarg) {
                  int c;                  
                  // eventually add combined h264/hevc encoding
                  if (strncmp(optarg,"ac3",3)==0) {
                      for (c = 0; c < MAX_AUDIO_SOURCES; c++) {
                          config_data.transaudio_info[c].audio_codec = STREAM_TYPE_AC3;
                      }
                  } else if (strncmp(optarg,"aac",3)==0) {
                      for (c = 0; c < MAX_AUDIO_SOURCES; c++) {
                          config_data.transaudio_info[c].audio_codec = STREAM_TYPE_AAC;
                      }
                  } else if (strncmp(optarg,"pass",4)==0) { // pass
                      for (c = 0; c < MAX_AUDIO_SOURCES; c++) {
                          config_data.transaudio_info[c].audio_codec = STREAM_TYPE_PASS;
                      }                      
                  } else {
                      fprintf(stderr,"ERROR: Invalid audio codec was selected\n");
                      return -1;
                  }
              } else {
                  fprintf(stderr,"ERROR: No output audio codec was selected\n");
                  return -1;
              }
#endif              
              break;
          case 't':
#if defined(ENABLE_TRANSCODE)
              if (optarg) {
                  int c;
                  int optarg_len = strlen(optarg);
                  char bitratestr[MAX_STR_SIZE];
                  int bidx = 0;
                  int outputidx = 0;
                  memset(bitratestr,0,sizeof(bitratestr));
                  for (c = 0; c < optarg_len; c++) {
                      if (optarg[c] != ',' && optarg[c] != '\0') {
                          bitratestr[bidx++] = optarg[c];
                          if (bidx >= MAX_STR_SIZE) {
                              fprintf(stderr,"ERROR: String size exceeded when parsing for bitrate\n");
                              return -1;
                          }
                      }
                      
                      if (optarg[c] == ',' || optarg[c] == '\0' || c == optarg_len - 1) {
                          config_data.transaudio_info[outputidx].audio_bitrate = atoi(bitratestr);
                          if (config_data.transaudio_info[outputidx].audio_bitrate < 40) {
                              fprintf(stderr,"ERROR: Invalid audio bitrate specified\n");
                              return -1;
                          }
                          outputidx++;
                          if (outputidx >= MAX_AUDIO_SOURCES) {
                              fprintf(stderr,"ERROR: Exceeded number of allowed audio transcoder outputs (bitrate parsing)\n");
                              return -1;
                          }                          
                          bidx = 0;
                          fprintf(stderr,"STATUS: output stream(%d) audio bitrate: %d\n",
                                  outputidx-1,
                                  config_data.transaudio_info[outputidx-1].audio_bitrate);
                          memset(bitratestr,0,sizeof(bitratestr));                          
                      }
                  }
              }              
#endif // ENABLE_TRANSCODE              
              break;                     
          case 'C':
              if (optarg) {
                  snprintf(config_data.youtube_cid,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: YouTube CID provided: %s\n", config_data.youtube_cid);
                  config_data.enable_youtube_output = 1;
                  config_data.enable_ts_output = 0;
                  config_data.enable_fmp4_output = 0;
              } else {
                  fprintf(stderr,"ERROR: Invalid YouTube CID provided\n");
                  return -1;
              }
              break;
          case 'T':
              if (optarg) {
                  audio_streams = atoi(optarg);
                  fprintf(stderr,"STATUS: Limiting audio streams to: %d\n", audio_streams);
              }
              break;
          case '7':
              if (optarg) {
                  snprintf(config_data.cdn_username,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: CDN username specified: %s\n", config_data.cdn_username);
              } else {
                  fprintf(stderr,"ERROR: Invalid username specified\n");
                  return -1;
              }
              break;
          case '8':
              if (optarg) {
                  snprintf(config_data.cdn_password,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: CDN password specified\n");
              } else {
                  fprintf(stderr,"ERROR: Invalid password specified\n");
                  return -1;
              }              
              break;
          case '9':
              if (optarg) {
                  snprintf(config_data.cdn_server,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: CDN url specified: %s\n", config_data.cdn_server);
              } else {
                  fprintf(stderr,"ERROR: Invalid url specified\n");
                  return -1;
              }              
              break;            
          case 'u':
	      if (optarg) {
		  config_data.identity = atoi(optarg);
		  if (config_data.identity < 0) {
		      fprintf(stderr,"ERROR: INVALID CONFIG IDENTITY: %d\n", config_data.identity);
		      return -1;
		  }
		  fprintf(stderr,"STATUS: Using config identity: %d\n", config_data.identity);
	      }
	      break;
	  case 'w':
	      if (optarg) {
		  config_data.window_size = atoi(optarg);
		  if (config_data.window_size < MIN_WINDOW_SIZE || config_data.window_size > MAX_WINDOW_SIZE) {
		      fprintf(stderr,"ERROR: INVALID WINDOW SIZE: %d\n", config_data.window_size);
		      return -1;
		  }
		  fprintf(stderr,"STATUS: Using window size: %d\n", config_data.window_size);
	      }
	      break;
	  case 'r':
	      if (optarg) {
		  config_data.rollover_size = atoi(optarg);
		  if (config_data.rollover_size < MIN_ROLLOVER_SIZE || config_data.rollover_size > MAX_ROLLOVER_SIZE) {
		      fprintf(stderr,"ERROR: INVALID ROLLOVER SIZE: %d\n", config_data.rollover_size);
		      return -1;
		  }
		  fprintf(stderr,"STATUS: Using rollover size: %d\n", config_data.rollover_size);
	      }
	      break;
	  case 's':
	      if (optarg) {
		  config_data.segment_length = atoi(optarg);
		  if (config_data.segment_length < MIN_SEGMENT_LENGTH || config_data.segment_length > MAX_SEGMENT_LENGTH) {
		      fprintf(stderr,"ERROR: INVALID SEGMENT LENGTH: %d\n", config_data.segment_length);
		      return -1;
		  }
		  fprintf(stderr,"STATUS: Using segment length: %d\n", config_data.segment_length);
	      }
	      break;
	  case 'f':
	      if (optarg) {
		  snprintf((char*)config_data.active_interface,UDP_MAX_IFNAME-1,"%s",optarg);
		  fprintf(stderr,"STATUS: Using supplied interface: %s\n", config_data.active_interface);
	      } else {
		  fprintf(stderr,"STATUS: No interface provided - defaulting to loopback (lo)\n");
		  snprintf((char*)config_data.active_interface,UDP_MAX_IFNAME-1,"lo");
	      }
	      break;
          case 'M':
              if (optarg) {  // manifest name DASH
                  snprintf(config_data.manifest_dash,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: Using supplied manifest filename for DASH: %s\n", config_data.manifest_dash);                  
              } else {
                  fprintf(stderr,"STATUS: No DASH manifest specified - using default: %s\n", config_data.manifest_dash);
              }
              break;
          case 'H':
              if (optarg) {  // manifest name HLS
                  snprintf(config_data.manifest_hls,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: Using supplied manifest filename for HLS: %s\n", config_data.manifest_hls);
              } else {
                  fprintf(stderr,"STATUS: No HLS manifest specified - using default: %s\n", config_data.manifest_hls);
              }
              break;
          case 'F':
              if (optarg) {  // manifest name fragmented MP4 (HLS)
                  snprintf(config_data.manifest_fmp4,MAX_STR_SIZE-1,"%s",optarg);
                  fprintf(stderr,"STATUS: Using supplied manifest filename for FMP4: %s\n", config_data.manifest_fmp4);
              } else {
                  fprintf(stderr,"STATUS: No fMP4 manifest specified - using default: %s\n", config_data.manifest_fmp4);
              }
              break;
	  case 'm':
	      if (optarg) {
		  int slen;
		  
		  snprintf((char*)config_data.manifest_directory,MAX_STR_SIZE-1,"%s",optarg);
		  slen = strlen(config_data.manifest_directory)-1;
		  if (config_data.manifest_directory[slen] == '/') {
		      config_data.manifest_directory[slen] = '\0';
		      fprintf(stderr,"STATUS: Removing trailing '/' from directory name\n");
		  }
		  
		  fprintf(stderr,"STATUS: Using supplied manifest directory: %s\n", config_data.manifest_directory);
	      } else {
		  fprintf(stderr,"STATUS: No manifest directory supplied - using default: %s\n", config_data.manifest_directory);
	      }
	      break;
	  case 'i': // ip addresses
	      if (optarg) {
		  int l;
		  int f;
		  int a;
		  int p;
		  char current_ip[UDP_MAX_IFNAME];
		  char current_port[UDP_MAX_IFNAME];
		  int optlen = strlen(optarg);
		  int parsing_port = 0;
		  
		  memset(current_ip,0,sizeof(current_ip));
		  memset(current_port,0,sizeof(current_port));
		  a = 0;
		  f = 0;
		  p = 0;
		  for (l = 0; l < optlen; l++) {
		      int colon = 0;
		      
		      if (optarg[l] != ',' && optarg[l] != '\0') {
			  if (optarg[l] == ':') {
			      parsing_port = 1;
			      colon = 1;
			  }

			  if (parsing_port) {
			      if (!colon) {
				  current_port[p] = optarg[l];
				  p++;
			      }
			  } else {
			      current_ip[f] = optarg[l];
			      f++;
			  }
		      } else {			  
			  parsing_port = 0;
			  snprintf(config_data.active_source[a].active_ip,UDP_MAX_IFNAME-1,"%s",current_ip);
			  config_data.active_source[a].active_port = atol(current_port);
			  fprintf(stderr,"STATUS: Source %d IP: %s:%d\n",
				  a,
				  config_data.active_source[a].active_ip,
				  config_data.active_source[a].active_port);
			  f = 0;
			  p = 0;
			  a++;
		      }
		  }

		  snprintf(config_data.active_source[a].active_ip,UDP_MAX_IFNAME-1,"%s",current_ip);
		  config_data.active_source[a].active_port = atol(current_port);
		  fprintf(stderr,"STATUS: Source %d IP: %s:%d\n",
			  a,
			  config_data.active_source[a].active_ip,
			  config_data.active_source[a].active_port);
		  a++;		  

		  for (f = 0; f < a; f++) {
		      struct in_addr addr;
		      
		      if (config_data.active_source[f].active_port == 0 ||
			  config_data.active_source[f].active_port >= 65535) {
			  fprintf(stderr,"ERROR: INVALID PORT SELECTED: %d\n",
				  config_data.active_source[f].active_port);
			  return -1;
		      }

		      if (inet_aton(config_data.active_source[f].active_ip, &addr) == 0) {
			  fprintf(stderr,"ERROR: INVALID IP ADDRESS: %s\n",
				  config_data.active_source[f].active_ip);
                          return -1;
		      }		      
		  }
	      }
	      break;
	  case 'S':
	      if (optarg) {
		  config_data.active_sources = atoi(optarg);
		  if (config_data.active_sources < 0 ||
		      config_data.active_sources > 10) {
		      fprintf(stderr,"ERROR: INVALID NUMBER OF SOURCES: %d\n", config_data.active_sources);
		      return -1;
		  }
	      } else {
		  return -1;
	      }
	      break;
          }
     }
     return 0;
}

const float framerate_lut[9] = {0.000f,
                                23.976f,
                                24.000f,
                                25.000f,
                                29.970f,
                                30.000f,
                                50.000f,
                                59.940f,
                                60.000f
                               };


static int message_dispatch(int p1, int64_t p2, int64_t p3, int64_t p4, int64_t p5, int source, void *context)
{
     //fillet_app_struct *core = (fillet_app_struct*)context;
     int message_handled = 1;

     if (p1 == 2000) {
          switch (p2) {
          case 100:
               fprintf(stderr,"PAT TABLE FOUND\n");
               break;
          case 200:
 	       fprintf(stderr,"PMT TABLE FOUND ON PID:%d\n", (int)p3);
               break;
          case 400:  // GET THE CALCULATED MUX RATE
               calculated_mux_rate = p4;
               break;
          case 600:
               //fprintf(stderr,"UNKNOWN DESCRIPTOR RECEIVED: 0x%x FOR PID:%d\n", p3, p4);
               break;
          case 610:
               //fprintf(stderr,"PMT REGISTRATION DESCRIPTOR 0x%x FOR PID:%d\n", p3, p4);
               break;
          case 709:
               //fprintf(stderr,"VIDEO ALIGNMENT DESCRIPTOR FOUND (0x06) - ALIGNMENT-TYPE:%d\n", p3);
               break;
          case 711:
               //fprintf(stderr,"H.264 VIDEO DESCRIPTOR (0x28) FOR PID:%d\n", p4);
               break;
          case 715:
               //fprintf(stderr,"SMOOTHING BUFFER DESCRIPTOR - TAG:0x%x\n", p3);
               break;
          case 717:
               //fprintf(stderr,"REGISTRATION DESCRIPTOR - TAG:0x%x\n", p3);
               break;
          case 718:
               //fprintf(stderr,"AC3 AUDIO DESCRIPTOR - TAG:0x%x\n", p3);
               break;
          case 719:
	       {
		   fprintf(stderr,"PMT DECODED: LANGUAGE DESCRIPTOR: %c%c%c\n", (char)p4, (char)p5, (char)source);
	       }
               break;
          case 799:
               //fprintf(stderr,"STREAM DESCRIPTOR - TAG:0x%x\n", p3);
               break;
          case 800:
               //fprintf(stderr,"MPEG2 VIDEO STREAM FOUND ON PID:%d BELONGS TO PMT:%d\n", p3, p4);
               break;
          case 801:
               //fprintf(stderr,"H.264 VIDEO STREAM FOUND ON PID:%d BELONGS TO PMT:%d\n", p3, p4);
               break;
          case 805:
               //fprintf(stderr,"AAC AUDIO STREAM FOUND ON PID:%d BELONGS TO PMT:%d\n", p3, p4);
               break;
          case 806:
               //fprintf(stderr,"AC3 AUDIO STREAM FOUND ON PID:%d BELONGS TO PMT:%d\n", p3, p4);
               break;
          case 812:              
               fprintf(stderr,"SCTE35 SPLICE PID:%ld BELONGS TO PMT:%ld\n", p3, p4);
               syslog(LOG_INFO,"SCTE35 SPLICE PID:%ld BELONGS TO PMT:%ld\n", p3, p4);
               break;
          case 900:
          case 901:
          case 902:
          case 903:
          case 904:
          case 905:
          case 906:
          case 907:
          case 908:
          case 909:
          case 910:
          case 911:
          case 912:
          case 913:
          case 914:
          case 915:
               //fprintf(stderr,"CONTINUITY COUNTER ERROR ON PID:%d  EXPECTED:%d INSTEAD FOUND:%d\n", p4, p3, p2 - 900);
               //error_data[error_count].packet_number = p5;
               // error_data[error_count].pid = p4;
               // sprintf(error_data[error_count].error_message,"CONTINUITY COUNTER ERROR ON PID:%4d (0x%4x)  LOOKING FOR %2d BUT FOUND %2d",
               //         (int)p4, (int)p4,
               //         (int)p3, (int)p2 - 900);
               //error_count = (error_count + 1) % MAX_ERROR_SIZE;
               error_count++;
               break;
          case 1000: {
               int framecode = p3;
               if (framecode > 8 || framecode < 0) {
                    framecode = 0;
               }

               fprintf(stderr,"PUBLISHED VIDEO FRAMERATE FOR PID(%d) IS: %.2f fps\n",
                       (int)p4, framerate_lut[framecode]);
          }
          break;
          case 1001: {
               fprintf(stderr,"PUBLISHED BITRATE FOR PID(%d) IS: %d bps\n",
                       (int)p4, (int)p3);
          }
          break;
          case 1002: {
	       fprintf(stderr,"PUBLISHED RESOLUTION FOR PID(%d) IS: %d x %d\n",
                       (int)p5, (int)p3, (int)p4);
          }
          break;
          case 1003: {
               if (p4 == 1) {
                    fprintf(stderr,"PUBLISHED SOURCE STREAM IS PROGRESSIVE FOR PID(%ld)\n", p5);
               } else {
                    fprintf(stderr,"PUBLISHED SOURCE STREAM IS INTERLACED FOR PID(%ld)\n", p5);
               }
               if (p3 == 0) {
                    fprintf(stderr,"PUBLISHED ASPECT RATIO IS UNDEFINED\n");
               } else if (p3 == 1) {
                    fprintf(stderr,"PUBLISHED ASPECT RATIO IS 1:1\n");
               } else if (p3 == 2) {
                    fprintf(stderr,"PUBLISHED ASPECT RATIO IS 4:3\n");
               } else if (p3 == 3) {
                    fprintf(stderr,"PUBLISHED ASPECT RATIO IS 16:9\n");
               } else {
                    fprintf(stderr,"PUBLISHED ASPECT RATIO IS UNKNOWN\n");
               }
          }
          break;
          default:
               message_handled = 0;
               break;
          }
     } else {
          message_handled = 0;
     }

     if (!(p1 >= 4000 && p1 <= 4500)) {
          if (!message_handled) {
               fprintf(stderr,"STATUS(%d)  P2=%ld  P3=%ld  P4=%ld  P5=%ld  CONTEXT:%p\n",
                       p1, p2, p3, p4, p5, context);
          }
     }
     return 0;
}

int peek_frame(sorted_frame_struct **frame_data, int entries, int pos, int64_t *current_time, int *sync)
{
    sorted_frame_struct *get_frame;

    pos = 0;
    get_frame = (sorted_frame_struct*)frame_data[pos];
    if (get_frame) {
	if (get_frame->frame_type == FRAME_TYPE_VIDEO) {
	    *current_time = get_frame->full_time;
	    *sync = get_frame->sync_frame;
	} else {
	    *current_time = get_frame->full_time;
	    *sync = 1;
	}
	return 0;
    }
    return -1;    
}

int use_frame(fillet_app_struct *core, sorted_frame_struct **frame_data, int entries, int pos, int64_t *current_time, int dump_sample, sorted_frame_struct **output_frame)
{
    int i;
    sorted_frame_struct *get_frame;

    // start from the top
    pos = 0;
    get_frame = (sorted_frame_struct*)frame_data[pos];
    for (i = pos; i < entries; i++) {	
        frame_data[i] = frame_data[i+1];
    }

    if (!get_frame) {
        fprintf(stderr,"warning: no frame returned in use_frame\n");
        return 0;
    }          
    
    if (get_frame->frame_type == FRAME_TYPE_VIDEO) {
	*current_time = get_frame->full_time;	
    } else {
	*current_time = get_frame->full_time;
    }	    

    if (!dump_sample) {
	*output_frame = get_frame;
    } else {
	*output_frame = NULL;
        //possibility for orphaned pool memory here- not using this right now
        memory_return(core->frame_msg_pool, (void*)get_frame);
        get_frame = NULL;
    }
 
    return entries - 1;
}

int add_frame(sorted_frame_struct **frame_data, int entries, sorted_frame_struct *new_frame, int capacity)
{
    int i;
    int new_count;

    if (entries >= capacity) {
	return entries;
    }

    for (i = entries - 1; (i >= 0 && frame_data[i]->full_time > new_frame->full_time); i--) {
	frame_data[i+1] = frame_data[i];
    } 
    frame_data[i+1] = (sorted_frame_struct*)new_frame;
    
    new_count = entries + 1;
    
    return new_count;
}

int dump_frames(fillet_app_struct *core, sorted_frame_struct **frame_data, int max)
{
    int i;

    fprintf(stderr,"-------------- dumping out frame data ------------\n");
    for (i = 0; i < max; i++) {
	if (frame_data[i]) {
	    if (frame_data[i]->frame_type == FRAME_TYPE_VIDEO) {
		fprintf(stderr,"[I:%4d] VIDEO:  SOURCE:%d PTS:%ld\n",
			i,
			frame_data[i]->source,
			frame_data[i]->full_time);
                memory_return(core->compressed_video_pool, frame_data[i]->buffer);
		frame_data[i]->buffer = NULL;
                memory_return(core->frame_msg_pool, frame_data[i]);
		frame_data[i] = NULL;	    	    
	    } else {	    
		fprintf(stderr,"[I:%4d] AUDIO:  SOURCE:%d PTS:%ld\n",
			i,
			frame_data[i]->source,
			frame_data[i]->full_time);
                memory_return(core->compressed_audio_pool, frame_data[i]->buffer);
		frame_data[i]->buffer = NULL;
                memory_return(core->frame_msg_pool, frame_data[i]);
		frame_data[i] = NULL;
	    }
	} else {
	    //fprintf(stderr,"[I:%4d] NO FRAME DATA\n", i);
	}
    }
    return 0;
}

static void *frame_sync_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    sorted_frame_struct *output_frame = NULL;
    int64_t current_video_time = 0;
    int64_t current_audio_time = 0;
    int audio_sync = 0;
    int video_sync = 0;
    int first_grab = 1;
    int no_grab = 0;
    int source_discontinuity = 1;
    int print_entries = 0;
    int print_current_time = 0;
    int active_sources;

    fprintf(stderr,"SESSION:%d (MAIN) STATUS: STARTING NEW SYNC THREAD\n", core->session_id);
    while (1) {
	audio_sync = 0;
	video_sync = 0;

	if (quit_sync_thread) {
	    pthread_mutex_lock(&sync_lock);		    	    
	    dump_frames(core, core->video_frame_data, MAX_FRAME_DATA_SYNC_VIDEO);
	    dump_frames(core, core->audio_frame_data, MAX_FRAME_DATA_SYNC_AUDIO);
	    pthread_mutex_unlock(&sync_lock);
	    video_synchronizer_entries = 0;
	    audio_synchronizer_entries = 0;	    
	    fprintf(stderr,"SESSION:%d (MAIN) STATUS: LEAVING SYNC THREAD - DISCONTINUITY\n", core->session_id);
            sync_thread_running = 0;

            if (enable_transcode) {
                fprintf(stderr,"SESSION:%d (MAIN) STATUS: RESTARTING APPLICATION\n", core->session_id);
                // setup Docker container do a restart
                exit(0);
            }
                
	    return NULL;
	}

	// if audio > video, then not yet ready...
        if (enable_verbose) {
            if (print_entries > 200) {
                fprintf(stderr,"SESSION:%d (MAIN) STATUS: audio_synchronizer_entries:%d  video_synchronizer_entries:%d\n",
                        core->session_id,
                        audio_synchronizer_entries,
                        video_synchronizer_entries);
                print_entries = 0;
            }
            print_entries++;
        }

#if defined(ENABLE_TRANSCODE)
        if (core->transcode_enabled) {
            active_sources = config_data.num_outputs;
        } else {
            active_sources = config_data.active_sources;            
        }
#else
        active_sources = config_data.active_sources;
#endif        
            
	if (audio_synchronizer_entries > active_sources && video_synchronizer_entries > active_sources) {
	    output_frame = NULL;
	    
	    pthread_mutex_lock(&sync_lock);	
	    peek_frame(core->audio_frame_data, audio_synchronizer_entries, 0, &current_audio_time, &audio_sync);
	    peek_frame(core->video_frame_data, video_synchronizer_entries, 0, &current_video_time, &video_sync);	
	    pthread_mutex_unlock(&sync_lock);

            if (enable_verbose) {
                if (print_current_time > 200) {
                    fprintf(stderr,"SESSION:%d (MAIN) STATUS: current_audio_time:%ld current_video_time:%ld\n",
                            core->session_id,
                            current_audio_time,
                            current_video_time);
                    print_current_time = 0;
                }
                print_current_time++;
            }
            
	    if (current_audio_time <= current_video_time) {
		no_grab = 0;
		while (current_audio_time < current_video_time && audio_synchronizer_entries > active_sources && !quit_sync_thread) {
		    pthread_mutex_lock(&sync_lock);
		    audio_synchronizer_entries = use_frame(core, core->audio_frame_data, audio_synchronizer_entries, 0, &current_audio_time, first_grab, &output_frame);
		    pthread_mutex_unlock(&sync_lock);
		    if (output_frame) {
			dataqueue_message_struct *msg;			
			msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
			if (msg) {
			    msg->buffer = output_frame;
			    msg->source_discontinuity = source_discontinuity;
			    source_discontinuity = 0;
                            dataqueue_put_front(core->hlsmux->input_queue, msg);
                            output_frame = NULL;
                        } else {
                            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                                    core->session_id);
                            exit(0);
                        }
		    } else {
                        usleep(1000);
                    }
		}
	    } else {
		fprintf(stderr,"NOT GRABBING AUDIO: DELTA:%ld (CVT:%ld CAT:%ld) AS:%d ASE:%d VSE:%d\n",
			current_video_time - current_audio_time,
			current_video_time,
			current_audio_time,
			active_sources,
			audio_synchronizer_entries,
			video_synchronizer_entries);
		first_grab = 0;
                
		no_grab++;
		if (no_grab >= 300) {  // was 15
		    quit_sync_thread = 1;
		    continue;
		}
	    }

            if (quit_sync_thread) {
                continue;
            }
			
	    if (!first_grab) {
		pthread_mutex_lock(&sync_lock);
		video_synchronizer_entries = use_frame(core, core->video_frame_data, video_synchronizer_entries, 0, &current_video_time, first_grab, &output_frame);
		pthread_mutex_unlock(&sync_lock);

		if (output_frame) {
		    dataqueue_message_struct *msg;
                    msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
		    if (msg) {
			msg->buffer = output_frame;
			msg->source_discontinuity = source_discontinuity;
			source_discontinuity = 0;		    
                        dataqueue_put_front(core->hlsmux->input_queue, msg);
                        output_frame = NULL;
                    } else {
                        fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                                core->session_id);
                        exit(0);
                    }
		} else {
                    usleep(1000);
                }
	    }
	} else {
	    usleep(5000);
	}
    }

    //never gets here
    return NULL;
}

static int64_t time_difference(struct timespec *now, struct timespec *start)
{
    int64_t tsec;
    int64_t tnsec;
    
    if (now->tv_nsec < start->tv_nsec) {
	tsec = (now->tv_sec - start->tv_sec);
	tsec--;
	tnsec = 1000000000;
	tnsec += (now->tv_nsec - start->tv_nsec);
    } else {
	tsec = now->tv_sec - start->tv_sec;
	tnsec = now->tv_nsec - start->tv_nsec;
    }
    
    return ((tnsec / 1000) + (tsec * 1000000));
}

int audio_sink_frame_callback(fillet_app_struct *core, uint8_t *new_buffer, int sample_size, int64_t pts, int sub_stream)
{
    sorted_frame_struct *new_frame;
    audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[0].audio_stream[sub_stream];
    int restart_sync_thread = 0;

    new_frame = (sorted_frame_struct*)memory_take(core->frame_msg_pool, sizeof(sorted_frame_struct));
    if (!new_frame) {
        fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain frame message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                core->session_id);
        exit(0);
    }
    
    new_frame->buffer = new_buffer;
    new_frame->buffer_size = sample_size;
    new_frame->pts = pts;
    new_frame->dts = pts;
    new_frame->full_time = pts;
    //new_frame->duration = 1920; // single aac frame
    // duration should be 1920 for single aac frame
    new_frame->duration = new_frame->full_time - astream->last_full_time;
    new_frame->first_timestamp = 0;
    new_frame->source = 0;
    new_frame->sub_stream = sub_stream;
    astream->last_full_time = new_frame->full_time;

    new_frame->frame_type = FRAME_TYPE_AUDIO;
    new_frame->media_type = MEDIA_TYPE_AAC;
    new_frame->time_received = 0;
    memset(new_frame->lang_tag,0,sizeof(new_frame->lang_tag));

   
    pthread_mutex_lock(&sync_lock);	
    audio_synchronizer_entries = add_frame(core->audio_frame_data, audio_synchronizer_entries, new_frame, MAX_FRAME_DATA_SYNC_AUDIO);
    if (audio_synchronizer_entries >= MAX_FRAME_DATA_SYNC_AUDIO) {
        restart_sync_thread = 1;
    }
    pthread_mutex_unlock(&sync_lock);

    if (restart_sync_thread) {
        fprintf(stderr,"GETTING SYNC LOCK\n");
	pthread_mutex_lock(&sync_lock);
	dump_frames(core, core->video_frame_data, MAX_FRAME_DATA_SYNC_VIDEO);
	dump_frames(core, core->audio_frame_data, MAX_FRAME_DATA_SYNC_AUDIO);        
	pthread_mutex_unlock(&sync_lock);
        fprintf(stderr,"DONE WITH SYNC LOCK\n");
	video_synchronizer_entries = 0;
	audio_synchronizer_entries = 0;
	quit_sync_thread = 1;
#if defined(ENABLE_TRANSCODE)
        stop_video_transcode_threads(core);
        stop_audio_transcode_threads(core);        
#endif // ENABLE_TRANSCODE
        fprintf(stderr,"WAITING FOR SYNC THREAD TO STOP\n");      
	pthread_join(frame_sync_thread_id, NULL);
        fprintf(stderr,"DONE WAITING FOR SYNC THREAD TO STOP\n");
    }    
    
    return 0;
}

#if defined(ENABLE_TRANSCODE)
int video_sink_frame_callback(fillet_app_struct *core, uint8_t *new_buffer, int sample_size, int64_t pts, int64_t dts, int source, int splice_point, int64_t splice_duration, int64_t splice_duration_remaining)
{
    sorted_frame_struct *new_frame;
    video_stream_struct *vstream = (video_stream_struct*)core->source_stream[source].video_stream;
    int restart_sync_thread = 0;
    int i;
    int sync_frame = 0;

    new_frame = (sorted_frame_struct*)memory_take(core->frame_msg_pool, sizeof(sorted_frame_struct));
    if (!new_frame) {
        fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain frame message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                core->session_id);
        exit(0);
    }  
    new_frame->buffer = new_buffer;
    new_frame->buffer_size = sample_size;
    new_frame->pts = pts;
    new_frame->dts = dts;
    new_frame->full_time = dts;// + vstream->overflow_dts;

    if (splice_point > 0 || splice_duration > 0 || splice_duration_remaining > 0) {
        syslog(LOG_INFO,"SCTE35: VIDEO SINK CALLBACK:  SPLICE:%d  DURATION:%ld  REMAINING:%ld (%ld seconds)\n",
               splice_point, splice_duration, splice_duration_remaining, splice_duration_remaining / 90000);
        new_frame->splice_duration_remaining = splice_duration_remaining;
        new_frame->splice_duration = splice_duration;
        new_frame->splice_point = splice_point;
    } else {
        new_frame->splice_duration_remaining = 0;
        new_frame->splice_duration = 0;
        new_frame->splice_point = 0;
    }
    
    /*if (dts > 0) {
        new_frame->full_time = dts;// + vstream->overflow_dts;
    } else {
        new_frame->full_time = pts;// + vstream->overflow_dts;
    }*/
    
    new_frame->duration = new_frame->full_time - vstream->last_full_time;	
    new_frame->first_timestamp = vstream->first_timestamp;
    new_frame->source = source;
    new_frame->sub_stream = 0;

    /*fprintf(stderr,"\n\n\nRECEIVED FEEDER CALLBACK: PTS:%ld DTS:%ld FIRSTVIDEO:%ld  DURATION:%ld\n\n\n",
            pts,
            dts,
            vstream->first_timestamp,
            new_frame->duration);*/

    vstream->last_full_time = new_frame->full_time; 

    new_frame->frame_type = FRAME_TYPE_VIDEO;
    if (config_data.transvideo_info[0].video_codec == STREAM_TYPE_HEVC) {
        for (i = 0; i < sample_size - 3; i++) {
            if (new_buffer[i] == 0x00 &&
                new_buffer[i+1] == 0x00 &&
                new_buffer[i+2] == 0x01) {
                int nal_type = (new_buffer[i+3] & 0x7f) >> 1;
                if (nal_type == 19 ||
                    nal_type == 20) {   // IDR
                    fprintf(stderr,"\n\nHEVC SYNC FRAME FOUND\n\n");
                    sync_frame = 1;
                    break;
                }
            }
        }           
        new_frame->media_type = MEDIA_TYPE_HEVC;
    } else {
        for (i = 0; i < sample_size - 4; i++) {
            if (new_buffer[i] == 0x00 &&
                new_buffer[i+1] == 0x00 &&
                new_buffer[i+2] == 0x00 &&
                new_buffer[i+3] == 0x01) {
                int nal_type = new_buffer[i+4] & 0x1f;
                if (nal_type == 7 ||
                    nal_type == 8 ||
                    nal_type == 5) {   // IDR
                    sync_frame = 1;
                    break;
                }
            }
        }   
        new_frame->media_type = MEDIA_TYPE_H264;
    }
    new_frame->sync_frame = sync_frame;   
    
    new_frame->time_received = 0;
    //if (lang_tag) {
    //        new_frame->lang_tag[0] = lang_tag[0];
    //        new_frame->lang_tag[1] = lang_tag[1];
    //        new_frame->lang_tag[2] = lang_tag[2];
    //        new_frame->lang_tag[3] = lang_tag[3];	    
    //    } else {
    // add lang tag passthrough
    memset(new_frame->lang_tag,0,sizeof(new_frame->lang_tag));
    //    }
    
    pthread_mutex_lock(&sync_lock);	
    video_synchronizer_entries = add_frame(core->video_frame_data, video_synchronizer_entries, new_frame, MAX_FRAME_DATA_SYNC_VIDEO);
    if (video_synchronizer_entries >= MAX_FRAME_DATA_SYNC_VIDEO) {
        restart_sync_thread = 1;	    	   
    }
    pthread_mutex_unlock(&sync_lock);

    if (restart_sync_thread) {
        fprintf(stderr,"GETTING SYNC LOCK\n");
	pthread_mutex_lock(&sync_lock);
	dump_frames(core, core->video_frame_data, MAX_FRAME_DATA_SYNC_VIDEO);
	dump_frames(core, core->audio_frame_data, MAX_FRAME_DATA_SYNC_AUDIO);        
	pthread_mutex_unlock(&sync_lock);
        fprintf(stderr,"DONE WITH SYNC LOCK\n");
	video_synchronizer_entries = 0;
	audio_synchronizer_entries = 0;
	quit_sync_thread = 1;
        stop_video_transcode_threads(core);
        stop_audio_transcode_threads(core);        
        fprintf(stderr,"WAITING FOR SYNC THREAD TO STOP\n");      
	pthread_join(frame_sync_thread_id, NULL);
        fprintf(stderr,"DONE WAITING FOR SYNC THREAD TO STOP\n");
    }
    return 0;
}
#endif // ENABLE_TRANSCODE        

static int receive_frame(uint8_t *sample, int sample_size, int sample_type, uint32_t sample_flags, int64_t pts, int64_t dts, int64_t last_pcr, int source, int sub_source, char *lang_tag, void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    int restart_sync_thread = 0;

    core->error_count = error_count;

    if (sample_type == STREAM_TYPE_SCTE35) {
        if (core->cd->enable_scte35) {
            scte35_data_struct *scte35_data = (scte35_data_struct*)sample;

            fprintf(stderr,"STATUS: RECEIVE_FRAME: (SCTE35): SPLICE-TYPE:%x PTS:%ld DURATION:%ld PTSADJ:%ld IMMEDIATE:%d PROGRAMID:%d CANCEL:%d OUTOFNETWORK:%d\n",
                    scte35_data->splice_command_type,
                    scte35_data->pts_time,
                    scte35_data->pts_duration,
                    scte35_data->pts_adjustment,
                    scte35_data->splice_immediate,
                    scte35_data->program_id,                
                    scte35_data->cancel,
                    scte35_data->out_of_network_indicator);
            
            syslog(LOG_INFO,"STATUS: RECEIVE_FRAME: (SCTE35): SPLICE-TYPE:0x%x EVENT:%ld PTS:%ld DURATION:%ld PTSADJ:%ld IMMEDIATE:%d PROGRAMID:%d CANCEL:%d OUTOFNETWORK:%d\n",
                   scte35_data->splice_command_type,
                   scte35_data->splice_event_id,
                   scte35_data->pts_time,
                   scte35_data->pts_duration,
                   scte35_data->pts_adjustment,
                   scte35_data->splice_immediate,
                   scte35_data->program_id,                
                   scte35_data->cancel,
                   scte35_data->out_of_network_indicator);
            
            if (scte35_data->splice_command_type == 0x05) {
                if (scte35_data->pts_duration > 0 && scte35_data->cancel == 0 && scte35_data->out_of_network_indicator) {
                    core->scte35_ready = 1;
                    core->scte35_pts = scte35_data->pts_time;
                    core->scte35_duration = scte35_data->pts_duration;
                    core->scte35_duration_remaining = scte35_data->pts_duration;
                    core->scte35_triggered = 0;
                    //pts_adjustment- non-zero
                } else {
                    core->scte35_ready = 0;
                }
            } else {
                core->scte35_ready = 0;
            }
        } else {
            core->scte35_ready = 0;
        }
    } else if (sample_type == STREAM_TYPE_H264 || sample_type == STREAM_TYPE_MPEG2 || sample_type == STREAM_TYPE_HEVC) {
	video_stream_struct *vstream = (video_stream_struct*)core->source_stream[source].video_stream;
	sorted_frame_struct *new_frame;
	uint8_t *new_buffer;
	struct timespec current_time;
	int64_t br;
	int64_t diff;

        if (enable_verbose || sample_flags) {
            fprintf(stderr,"STATUS: RECEIVE_FRAME (H264/HEVC :%2d): TYPE:%2d PTS:%15ld DTS:%15ld KEY:%d\n",
                    source,
                    sample_type,
                    pts,
                    dts,
                    sample_flags);
        }   

	if (vstream->total_video_bytes == 0) {
	    clock_gettime(CLOCK_REALTIME, &vstream->video_clock_start);
	}
	vstream->total_video_bytes += sample_size;
	clock_gettime(CLOCK_REALTIME, &current_time);

	diff = time_difference(&current_time, &vstream->video_clock_start) / 1000;
	if (diff > 0) {
	    br = (vstream->total_video_bytes * 8) / diff;
	    vstream->video_bitrate = br * 1000;
	}

        core->uptime = diff / 1000;
	
	if (!vstream->found_key_frame && sample_flags) {
	    vstream->found_key_frame = 1;
	    if (dts > 0) {
		vstream->first_timestamp = dts;
	    } else {
		vstream->first_timestamp = pts;
	    }
	}

	if (dts > 0) {
	    if (vstream->last_timestamp_dts != -1) {	    
		int64_t delta_dts = dts + vstream->overflow_dts - vstream->last_timestamp_dts;
		int64_t mod_overflow = vstream->last_timestamp_dts % 8589934592;
		if (delta_dts < -OVERFLOW_DTS && mod_overflow > OVERFLOW_DTS) {
		    vstream->overflow_dts += 8589934592;
		    fprintf(stderr,"FILLET: DELTA_DTS:%ld OVERFLOW DTS:%ld\n", delta_dts, vstream->overflow_dts);
		    syslog(LOG_INFO,"(%d) VIDEO STREAM TIMESTAMP OVERFLOW - DELTA:%ld  OVERFLOW DTS:%ld  LAST:%ld  DTS:%ld\n",
			   source,
			   delta_dts,
			   vstream->overflow_dts,
			   vstream->last_timestamp_dts,
			   dts);
		} else if (delta_dts < 0 || delta_dts > 60000) {
		    fprintf(stderr,"FILLET: DELTA_DTS:%ld OVERFLOW DTS:%ld (SIGNAL DISCONTINUITY)\n", delta_dts, vstream->overflow_dts);
		    syslog(LOG_INFO,"(%d) VIDEO STREAM TIMESTAMP OVERFLOW - DELTA:%ld  OVERFLOW DTS:%ld  LAST:%ld  DTS:%ld (SIGNAL DISCONTINUITY)\n",
			   source,
			   delta_dts,
			   vstream->overflow_dts,
			   vstream->last_timestamp_dts,
			   dts);
                    fprintf(stderr,"(%d) VIDEO STREAM TIMESTAMP OVERFLOW - DELTA:%ld  OVERFLOW DTS:%ld  LAST:%ld  DTS:%ld (SIGNAL DISCONTINUITY)\n",
                            source,
                            delta_dts,
                            vstream->overflow_dts,
                            vstream->last_timestamp_dts,
                            dts);
		    fprintf(stderr,"FILLET: (%d) RESTARTING SYNC THREAD\n", source);
		    restart_sync_thread = 1;
		}
	    }

	    vstream->last_timestamp_dts = dts + vstream->overflow_dts;
	}
	vstream->last_timestamp_pts = pts + vstream->overflow_dts;
	vstream->current_receive_count++;
	if (sample_flags) {
	    vstream->last_intra_count = vstream->current_receive_count;
	}

	if (!vstream->found_key_frame) {
	    return 0;
	}

	new_buffer = (uint8_t*)memory_take(core->compressed_video_pool, sample_size);
        if (!new_buffer) {
            fprintf(stderr,"SESSION:%d (MAIN) STATUS: unable to obtain compressed video buffer! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }
	memcpy(new_buffer, sample, sample_size);
	
	new_frame = (sorted_frame_struct*)memory_take(core->frame_msg_pool, sizeof(sorted_frame_struct));
        if (!new_frame) {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain frame message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);          
        }      
	new_frame->buffer = new_buffer;
	new_frame->buffer_size = sample_size;

	new_frame->pts = pts;
	new_frame->dts = dts;
	if (dts > 0) {
	    new_frame->full_time = dts + vstream->overflow_dts;
	} else {
	    new_frame->full_time = pts + vstream->overflow_dts;
	}

        new_frame->splice_point = 0;
        new_frame->splice_duration = 0;
        new_frame->splice_duration_remaining = 0;
        
        if (core->scte35_ready) {
            int64_t scte35_time_diff;
            int64_t anchor_time = new_frame->full_time % 8589934592;
            scte35_time_diff = core->scte35_pts - anchor_time;
            fprintf(stderr,"SCTE35 TIME DIFF:%ld\n", scte35_time_diff);
            syslog(LOG_INFO,"SCTE35 TIME DIFF:%ld    PTS:%ld    SCTE35PTS:%ld   DURATION:%ld   REMAINING:%ld  TRIGGERED:%d\n",
                   scte35_time_diff,
                   anchor_time,
                   core->scte35_pts,
                   core->scte35_duration,
                   core->scte35_duration_remaining,
                   core->scte35_triggered);

            if (core->scte35_triggered) {
                core->scte35_duration_remaining = core->scte35_duration - abs(scte35_time_diff);
                new_frame->splice_duration_remaining = core->scte35_duration_remaining;
                if (new_frame->splice_duration_remaining < 0) {
                    new_frame->splice_duration_remaining = 0;
                    core->scte35_duration_remaining = 0;
                    core->scte35_triggered = 0;
                    core->scte35_duration = 0;
                    new_frame->splice_point = 2; // splice back point
                    core->scte35_ready = 0;
                    core->scte35_last_pts_diff = 0;
                } else {
                    new_frame->splice_point = 0;
                    core->scte35_last_pts_diff = scte35_time_diff;
                }
                fprintf(stderr,"SCTE35 TIME REMAINING:%ld\n", new_frame->splice_duration_remaining);
                syslog(LOG_INFO,"SCTE35 TIME REMAINING:%ld\n", new_frame->splice_duration_remaining);
                new_frame->splice_duration = core->scte35_duration;               
            } else if (scte35_time_diff < 0 && core->scte35_last_pts_diff >= 0) {
                core->scte35_last_pts_diff = 0;
                new_frame->splice_point = 1;
                new_frame->splice_duration = core->scte35_duration;
                new_frame->splice_duration_remaining = core->scte35_duration;
                core->scte35_triggered = 1;
                syslog(LOG_INFO,"SCTE35 - SETTING IT TO TRIGGERED\n");
                //trigger point
            } else if (scte35_time_diff < 0) {
                //it's already too late- not sure what happened
                core->scte35_ready = 0;
                core->scte35_last_pts_diff = 0;
                core->scte35_duration = 0;
                core->scte35_duration_remaining = 0;
                core->scte35_last_pts_diff = 0;
                new_frame->splice_point = 0;
                new_frame->splice_duration = 0;
                new_frame->splice_duration_remaining = 0;
            } else {
                core->scte35_last_pts_diff = scte35_time_diff;
                new_frame->splice_point = 0;
                new_frame->splice_duration = 0;
                new_frame->splice_duration_remaining = 0;                
            }
        }
        
	new_frame->duration = new_frame->full_time - vstream->last_full_time;
        if (!enable_transcode) {            
            vstream->last_full_time = new_frame->full_time;
        }
	new_frame->first_timestamp = vstream->first_timestamp;
	new_frame->source = source;
        new_frame->sub_stream = 0;
	new_frame->sync_frame = sample_flags;
	new_frame->frame_type = FRAME_TYPE_VIDEO;
        if (sample_type == STREAM_TYPE_H264) {            
            new_frame->media_type = MEDIA_TYPE_H264;
        } else if (sample_type == STREAM_TYPE_MPEG2) {
            new_frame->media_type = MEDIA_TYPE_MPEG2;
        } else if (sample_type == STREAM_TYPE_HEVC) {
            new_frame->media_type = MEDIA_TYPE_HEVC;
        }
	new_frame->time_received = 0;
	if (lang_tag) {
	    new_frame->lang_tag[0] = lang_tag[0];
	    new_frame->lang_tag[1] = lang_tag[1];
	    new_frame->lang_tag[2] = lang_tag[2];
	    new_frame->lang_tag[3] = lang_tag[3];	    
	} else {
	    memset(new_frame->lang_tag,0,sizeof(new_frame->lang_tag));
	}

        if (enable_transcode) {
#if defined(ENABLE_TRANSCODE)            
            dataqueue_message_struct *decode_msg;

            decode_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
            if (decode_msg) {
                decode_msg->buffer = new_frame;
                decode_msg->buffer_size = sizeof(sorted_frame_struct);
                dataqueue_put_front(core->transvideo->input_queue, decode_msg);
                decode_msg = NULL;
            } else {
                fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                        core->session_id);
                exit(0);
            }
#endif // ENABLE_TRANSCODE            
        } else {
            if (sync_thread_running && !quit_sync_thread) {
                pthread_mutex_lock(&sync_lock);	
                video_synchronizer_entries = add_frame(core->video_frame_data, video_synchronizer_entries, new_frame, MAX_FRAME_DATA_SYNC_VIDEO);
                if (video_synchronizer_entries >= MAX_FRAME_DATA_SYNC_VIDEO) {
                    restart_sync_thread = 1;	    	   
                }
                pthread_mutex_unlock(&sync_lock);
            } else {
                memory_return(core->compressed_video_pool, new_frame->buffer);
                new_frame->buffer = NULL;
                memory_return(core->frame_msg_pool, new_frame);
                new_frame = NULL;
            }
        }
    } else if (sample_type == STREAM_TYPE_AAC || sample_type == STREAM_TYPE_AC3 || sample_type == STREAM_TYPE_MPEG) {
	audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[sub_source];
	video_stream_struct *vstream = (video_stream_struct*)core->source_stream[source].video_stream;
	uint8_t *new_buffer;
	sorted_frame_struct *new_frame;	
	struct timespec current_time;
	int64_t br;
	int64_t diff;

        if (enable_verbose) {
            fprintf(stderr,"STATUS: RECEIVE_FRAME (AUDIO:%2d): TYPE:%2d PTS:%15ld DTS:%15ld KEY:%d (AUDIO STREAM:%d)\n",
                    source,
                    sample_type,
                    pts,
                    dts,
                    vstream->found_key_frame,
                    sub_source);
        }

        if (sub_source < 0 || sub_source > MAX_AUDIO_SOURCES) {
            //error            
        }

        if (audio_streams != -1) {
            if (sub_source >= audio_streams) {
                //rejecting additional audio streams
                return 0;
            }
        }

        if (source != core->cd->audio_source_index && core->cd->audio_source_index != -1) {
            // take audio only from the specified stream
            // if we are being fed with multiple spts of audio then we should just grab the first set
            // this should be made configurable
            return 0;
        }

        if (!vstream->found_key_frame) {
	    return 0;
	}

	if (astream->total_audio_bytes == 0) {
	    clock_gettime(CLOCK_REALTIME, &astream->audio_clock_start);
	}
	astream->total_audio_bytes += sample_size;
	clock_gettime(CLOCK_REALTIME, &current_time);

	diff = time_difference(&current_time, &astream->audio_clock_start) / 1000;
	if (diff > 0) {
	    br = (astream->total_audio_bytes * 8) / diff;
	    astream->audio_bitrate = br * 1000;
	}

	new_buffer = (uint8_t*)memory_take(core->compressed_audio_pool, sample_size);
        if (!new_buffer) {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain compressed audio buffer! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }        
	memcpy(new_buffer, sample, sample_size);

	if (astream->last_timestamp_pts != -1) {
	    int64_t delta_pts = pts + astream->overflow_pts - astream->last_timestamp_pts;
	    int64_t mod_overflow = astream->last_timestamp_pts % 8589934592;
	    if (delta_pts < -OVERFLOW_DTS && mod_overflow > OVERFLOW_DTS) {	    
		astream->overflow_pts += 8589934592;
		fprintf(stderr,"FILLET: DELTA_PTS:%ld OVERFLOW PTS:%ld\n", delta_pts, astream->overflow_pts);
		syslog(LOG_INFO,"(%d) AUDIO STREAM TIMESTAMP OVERFLOW - DELTA:%ld  OVERFLOW PTS:%ld  PTS:%ld\n",
		       source,
		       delta_pts,
		       astream->overflow_pts,
		       pts);
	    } else if (delta_pts < 0 || delta_pts > 60000) {
		fprintf(stderr,"FILLET: DELTA_PTS:%ld OVERFLOW PTS:%ld\n", delta_pts, astream->overflow_pts);
		syslog(LOG_INFO,"(%d) AUDIO STREAM TIMESTAMP OVERFLOW - DELTA:%ld  OVERFLOW PTS:%ld  PTS:%ld  LAST PTS:%ld\n",
		       source,
		       delta_pts,
		       astream->overflow_pts,
		       pts,
                       astream->last_timestamp_pts);
		fprintf(stderr,"(%d) AUDIO STREAM TIMESTAMP OVERFLOW - DELTA:%ld  OVERFLOW PTS:%ld  PTS:%ld  LAST PTS:%ld\n",
                        source,
                        delta_pts,
                        astream->overflow_pts,
                        pts,
                        astream->last_timestamp_pts);

		fprintf(stderr,"FILLET: (%d) RESTARTING SYNC THREAD\n", source);
		restart_sync_thread = 1;
	    } 	    
	}   	

	astream->last_timestamp_pts = pts + astream->overflow_pts;
	
	new_frame = (sorted_frame_struct*)memory_take(core->frame_msg_pool, sizeof(sorted_frame_struct));
        if (!new_frame) {
            fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain frame message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                    core->session_id);
            exit(0);
        }
	new_frame->buffer = new_buffer;
	new_frame->buffer_size = sample_size;
	new_frame->pts = pts;
	new_frame->dts = dts;
	new_frame->full_time = pts + astream->overflow_pts;
	new_frame->duration = new_frame->full_time - astream->last_full_time;
#if defined(ENABLE_TRANSCODE)
        if (!enable_transcode) {            // gets set in audio_sink_frame_callback
            astream->last_full_time = new_frame->full_time;
        }
#else // ENABLE_TRANSCODE
        astream->last_full_time = new_frame->full_time;
#endif        
	new_frame->first_timestamp = 0;
	new_frame->source = source;
        new_frame->sub_stream = sub_source;
	new_frame->sync_frame = sample_flags;
	new_frame->frame_type = FRAME_TYPE_AUDIO;
        if (sample_type == STREAM_TYPE_AAC) {
            new_frame->media_type = MEDIA_TYPE_AAC;
        } else if (sample_type == STREAM_TYPE_AC3) {
            new_frame->media_type = MEDIA_TYPE_AC3;
        } else if (sample_type == STREAM_TYPE_MPEG) {
            new_frame->media_type = MEDIA_TYPE_MPEG;
        }
	new_frame->time_received = 0;
	if (lang_tag) {
	    new_frame->lang_tag[0] = lang_tag[0];
	    new_frame->lang_tag[1] = lang_tag[1];
	    new_frame->lang_tag[2] = lang_tag[2];
	    new_frame->lang_tag[3] = lang_tag[3];	    
	} else {
	    memset(new_frame->lang_tag,0,sizeof(new_frame->lang_tag));
	}
        if (enable_transcode) {
#if defined(ENABLE_TRANSCODE)            
            dataqueue_message_struct *decode_msg;

            decode_msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
            if (decode_msg) {
                decode_msg->buffer = new_frame;
                decode_msg->buffer_size = sizeof(sorted_frame_struct);
                dataqueue_put_front(core->transaudio[sub_source]->input_queue, decode_msg);
                decode_msg = NULL;
            } else {
                fprintf(stderr,"SESSION:%d (MAIN) ERROR: unable to obtain message! CHECK CPU RESOURCES!!! UNRECOVERABLE ERROR!!!\n",
                        core->session_id);
                exit(0);
            }
#endif // ENABLE_TRANSCODE
        } else {
            if (sync_thread_running && !quit_sync_thread) {
                pthread_mutex_lock(&sync_lock);	
                audio_synchronizer_entries = add_frame(core->audio_frame_data, audio_synchronizer_entries, new_frame, MAX_FRAME_DATA_SYNC_AUDIO);
                if (audio_synchronizer_entries >= MAX_FRAME_DATA_SYNC_AUDIO) {
                    restart_sync_thread = 1;
                }
                pthread_mutex_unlock(&sync_lock);
            } else {
                memory_return(core->compressed_audio_pool, new_frame->buffer);
                new_frame->buffer = NULL;
                memory_return(core->frame_msg_pool, new_frame);
                new_frame = NULL;                
            }
        }
    } else {
	fprintf(stderr,"UNKNOWN SAMPLE RECEIVED\n");
    }

    if (restart_sync_thread) {
        fprintf(stderr,"GETTING SYNC LOCK\n");
	pthread_mutex_lock(&sync_lock);
	dump_frames(core, core->video_frame_data, MAX_FRAME_DATA_SYNC_VIDEO);
	dump_frames(core, core->audio_frame_data, MAX_FRAME_DATA_SYNC_AUDIO);        
	pthread_mutex_unlock(&sync_lock);
        fprintf(stderr,"DONE WITH SYNC LOCK\n");
	video_synchronizer_entries = 0;
	audio_synchronizer_entries = 0;
	quit_sync_thread = 1;
#if defined(ENABLE_TRANSCODE)
        stop_video_transcode_threads(core);
        stop_audio_transcode_threads(core);        
#endif // ENABLE_TRANSCODE        
        fprintf(stderr,"WAITING FOR SYNC THREAD TO STOP: %d\n", sync_thread_running);        
	pthread_join(frame_sync_thread_id, NULL);
        fprintf(stderr,"DONE WAITING FOR SYNC THREAD TO STOP\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
     int ret;
     int i;
     fillet_app_struct *core;
     pthread_t client_thread_id;
     int c;
     int loop_count = 0;

     socket_udp_global_init();
     
     signal(SIGSEGV, crash_handler);

     config_data.window_size = DEFAULT_WINDOW_SIZE;
     config_data.segment_length = DEFAULT_SEGMENT_LENGTH;
     config_data.rollover_size = MAX_ROLLOVER_SIZE;
     config_data.active_sources = 0;
     config_data.identity = 1000;
     config_data.enable_youtube_output = 0;
     config_data.enable_ts_output = 0;
     config_data.enable_fmp4_output = 0;
     config_data.audio_source_index = 0;

#if defined(ENABLE_TRANSCODE)     
     for (c = 0; c < MAX_TRANS_OUTPUTS; c++) {
         config_data.transvideo_info[c].encoder_quality = ENCODER_QUALITY_MEDIUM;
         config_data.transvideo_info[c].encoder_profile = ENCODER_PROFILE_MAIN;
     }
#endif // ENABLE_TRANSCODE     
     
     memset(config_data.youtube_cid,0,sizeof(config_data.youtube_cid));
     
     snprintf(config_data.manifest_directory,MAX_STR_SIZE-1,"/var/www/hls/");
     snprintf(config_data.manifest_dash,MAX_STR_SIZE-1,"masterdash.mpd");
     snprintf(config_data.manifest_hls,MAX_STR_SIZE-1,"master.m3u8");
     snprintf(config_data.manifest_fmp4,MAX_STR_SIZE-1,"masterfmp4.m3u8");     
     
     ret = parse_input_options(argc, argv);
     if ((ret < 0 || config_data.active_sources == 0)) {
         fprintf(stderr,"\n");
	 fprintf(stderr,"fillet is a live packaging service/application for IP based OTT content redistribution\n");
	 fprintf(stderr,"\n");
	 fprintf(stderr,"usage: fillet [options]\n");
	 fprintf(stderr,"\n\n");
         fprintf(stderr,"PACKAGING OPTIONS\n");
	 fprintf(stderr,"       --sources       [NUMBER OF ABR SOURCES - MUST BE >= 1 && <= 10]\n");
	 fprintf(stderr,"       --ip            [IP:PORT,IP:PORT,etc.] (Please make sure this matches the number of sources)\n");
	 fprintf(stderr,"       --interface     [SOURCE INTERFACE - lo,eth0,eth1,eth2,eth3]\n");
	 fprintf(stderr,"                       If multicast, make sure route is in place\n\n");
	 fprintf(stderr,"       --window        [WINDOW IN SEGMENTS FOR MANIFEST]\n");
	 fprintf(stderr,"       --segment       [SEGMENT LENGTH IN SECONDS]\n");
	 fprintf(stderr,"       --manifest      [MANIFEST DIRECTORY \"/var/www/hls/\"]\n");
	 fprintf(stderr,"       --identity      [RUNTIME IDENTITY - any number, but must be unique across multiple instances of fillet]\n");
         fprintf(stderr,"       --hls           [ENABLE TRADITIONAL HLS TRANSPORT STREAM OUTPUT - NO ARGUMENT REQUIRED]\n");
         fprintf(stderr,"       --dash          [ENABLE FRAGMENTED MP4 STREAM OUTPUT (INCLUDES DASH+HLS FMP4) - NO ARGUMENT REQUIRED]\n");
         fprintf(stderr,"       --manifest-dash [NAME OF THE DASH MANIFEST FILE - default: masterdash.mpd]\n");
         fprintf(stderr,"       --manifest-hls  [NAME OF THE HLS MANIFEST FILE - default: master.m3u8]\n");
         fprintf(stderr,"       --manifest-fmp4 [NAME OF THE fMP4/CMAF MANIFEST FILE - default: masterfmp4.m3u8]\n");
         fprintf(stderr,"       --webvtt        [ENABLE WEBVTT CAPTION OUTPUT]\n");         
         fprintf(stderr,"       --cdnusername   [USERNAME FOR WEBDAV ACCOUNT]\n");
         fprintf(stderr,"       --cdnpassword   [PASSWORD FOR WEBDAV ACCOUNT]\n");
         fprintf(stderr,"       --cdnserver     [HTTP(S) URL FOR WEBDAV SERVER]\n");
	 fprintf(stderr,"\n");
#if defined(ENABLE_TRANSCODE)
         fprintf(stderr,"TRANSCODE OPTIONS\n");
         fprintf(stderr,"       --transcode   [ENABLE TRANSCODER AND NOT JUST PACKAGING]\n");
         fprintf(stderr,"       --outputs     [NUMBER OF OUTPUT LADDER BITRATE PROFILES TO BE TRANSCODED]\n");
         fprintf(stderr,"       --vcodec      [VIDEO CODEC - needs to be hevc or h264]\n");
         fprintf(stderr,"       --resolutions [OUTPUT RESOLUTIONS - formatted as: 320x240,640x360,960x540,1280x720]\n");
         fprintf(stderr,"       --vrate       [VIDEO BITRATES IN KBPS - formatted as: 800,1250,2500,500]\n");
         fprintf(stderr,"       --acodec      [AUDIO CODEC - needs to be aac, ac3 or pass]\n");
         fprintf(stderr,"       --arate       [AUDIO BITRATES IN KBPS - formatted as: 128,96]\n");
         fprintf(stderr,"       --aspect      [FORCE THE ASPECT RATIO - needs to be 16:9, 4:3, or other]\n");
         fprintf(stderr,"       --scte35      [PASSTHROUGH SCTE35 TO MANIFEST (still finishing up)]\n");
         fprintf(stderr,"       --stereo      [FORCE ALL AUDIO OUTPUTS TO STEREO- will downmix if source is 5.1 or upmix if source is 1.0]\n");
         fprintf(stderr,"       --quality     [VIDEO ENCODING QUALITY LEVEL 0-3 (0-LOW,1-MED,2-HIGH,3-CRAZY)\n");
         fprintf(stderr,"                      LOADING WILL AFFECT CHANNEL DENSITY-SOME PLATFORMS MAY NOT RUN HIGHER QUALITY REAL-TIME\n");
         fprintf(stderr,"\n");
         fprintf(stderr,"H.264 SPECIFIC OPTIONS (valid when --vcodec is h264)\n");
         fprintf(stderr,"       --profile     [H264 ENCODING PROFILE - needs to be base,main or high]\n");
         fprintf(stderr,"\n");
         fprintf(stderr,"PACKAGING AND TRANSCODING OPTIONS CAN BE COMBINED\n");
         fprintf(stderr,"\n");
#endif // ENABLE_TRANSCODE         
	 return 1;
     }

     if (enable_ts == 0 && enable_fmp4 == 0 && enable_youtube == 0) {
         fprintf(stderr,"FILLET: ERROR: Please select TS output (--hls) and/or fMP4 output mode (--dash) OR YouTube DASH publishing mode\n");
         fprintf(stderr,"\n");
         return 1;
     }
     if ((enable_ts || enable_fmp4) && enable_youtube) {
         fprintf(stderr,"FILLET: ERROR: Incompatible runtime mode- YouTube mode and TS/fMP4 are not compatible\n");
         fprintf(stderr,"\n");
         return 1;
     }
     
     config_data.enable_ts_output = !!enable_ts;
     config_data.enable_fmp4_output = !!enable_fmp4;

#if defined(ENABLE_TRANSCODE)
     if (enable_transcode && config_data.transvideo_info[0].video_codec == STREAM_TYPE_HEVC) {
         if (config_data.enable_ts_output) {
             fprintf(stderr,"FILLET: ERROR: Incompatible runtime mode- TS output enabled with HEVC encoding\n");
             fprintf(stderr,"\n");
             return 1;
         }
     }   
     
     if (enable_transcode && config_data.num_outputs == 0) {
         fprintf(stderr,"FILLET: ERROR: Incompatible runtime mode- Transcoding enabled but no outputs were selected\n");
         fprintf(stderr,"\n");
         return 1;
     }
#endif // ENABLE_TRANSCODE     

     core = create_fillet_core(&config_data, config_data.active_sources);
     
     // basic command line mode for testing purposes
     core->session_id = 1;
     core->transcode_enabled = !!enable_transcode;
     core->sync_thread_restart_count = 0;
     core->cd->enable_scte35 = !!enable_scte35;
     core->cd->enable_stereo = !!enable_stereo;
     core->cd->enable_webvtt = !!enable_webvtt;
     
     register_message_callback(message_dispatch, (void*)core);
     register_frame_callback(receive_frame, (void*)core);

     for (i = 0; i < config_data.active_sources; i++) {
         struct in_addr addr;	     
         snprintf(core->fillet_input[i].interface,UDP_MAX_IFNAME-1,"%s",config_data.active_interface);
         snprintf(core->fillet_input[i].udp_source_ipaddr,UDP_MAX_IFNAME-1,"%s",config_data.active_source[i].active_ip);
         if (inet_aton(config_data.active_source[i].active_ip, &addr) == 0) {
             fprintf(stderr,"\nERROR: INVALID IP ADDRESS: %s\n\n",
                     config_data.active_source[i].active_ip);
             if (strlen(config_data.active_source[i].active_ip) == 0) {
                 fprintf(stderr,"\n\nNO IP ADDRESS WAS SPECIFIED\n\n");                
             }
             exit(0);
         }
         core->fillet_input[i].udp_source_port = config_data.active_source[i].active_port;
     }

     hlsmux_create(core);
     start_webdav_threads(core);
     
     sync_thread_running = 1;
     pthread_create(&frame_sync_thread_id, NULL, frame_sync_thread, (void*)core);  
     core->source_running = 1;
     for (i = 0; i < config_data.active_sources; i++) {	
         pthread_create(&core->source_stream[i].udp_source_thread_id, NULL, udp_source_thread, (void*)core);
     }

#if defined(ENABLE_TRANSCODE)
     pthread_create(&client_thread_id, NULL, status_thread, (void*)core);     
#else     
     pthread_create(&client_thread_id, NULL, client_thread, (void*)core);
#endif     
     while (core->source_running) {
         if (quit_sync_thread) {
             while (sync_thread_running) {
                 // this is signaled in the sync_thread
                 usleep(10000);
             }
             quit_sync_thread = 0;
             // sync thread will restart if things choke up on the front-end and the sorted queue
             // gets messed up (could be a result of missing/corrupt/lost data)
             //
             // for the pure packaging mode, the content is ingested and put into a sorted queue
             // so we are able to make sure all of the data is present but also absorb some
             // interstream delays on the input since it is basically a bunch of spts coming in on udp streams..
             //
             // if the sync thread restarts, it flags a discontinuity into the hls manifest
             // and also keeps the dash manifest going from where it left off
             // the docker container will restart the application if it happens to exit (if you've set it to restart)
             //
             // my goal is to make this as resilient as possible to source stream issues
             // and to just keep packaging so a proper output stream is available
             // can't stand having signal interruptions
             fprintf(stderr,"STATUS: RESTARTING FRAME SYNC THREAD\n");             
             sync_thread_running = 1;
             core->sync_thread_restart_count++;
#if defined(ENABLE_TRANSCODE)
             start_video_transcode_threads(core);
             start_audio_transcode_threads(core);        
#endif // ENABLE_TRANSCODE             
             pthread_create(&frame_sync_thread_id, NULL, frame_sync_thread, (void*)core);
         }

         loop_count++;
#if defined(ENABLE_TRANSCODE)
#define WAIT_THRESHOLD_WARNING 3
#define WAIT_THRESHOLD_ERROR   10
#define WAIT_THRESHOLD_FAIL    30        
#define LEVEL_CHECK_THRESHOLD  500
         if (core->transcode_enabled) {
             if (loop_count >= LEVEL_CHECK_THRESHOLD) {
                 int n;                 

                 int video_decode_frames_waiting;
                 int video_deinterlace_frames_waiting;

                 syslog(LOG_INFO,"SESSION:%d (MAIN): FLM:%d FRM:%d CV:%d CA:%d RV:%d RA:%d\n",
                        core->session_id,
                        memory_unused(core->fillet_msg_pool),
                        memory_unused(core->frame_msg_pool),
                        memory_unused(core->compressed_video_pool),
                        memory_unused(core->compressed_audio_pool),
                        memory_unused(core->raw_video_pool),
                        memory_unused(core->raw_audio_pool));
                 fprintf(stderr,"SESSION:%d (MAIN): FLM:%d FRM:%d CV:%d CA:%d RV:%d RA:%d\n",
                         core->session_id,
                         memory_unused(core->fillet_msg_pool),
                         memory_unused(core->frame_msg_pool),
                         memory_unused(core->compressed_video_pool),
                         memory_unused(core->compressed_audio_pool),
                         memory_unused(core->raw_video_pool),
                         memory_unused(core->raw_audio_pool));                 

                 video_decode_frames_waiting = dataqueue_get_size(core->transvideo->input_queue);
                 if (video_decode_frames_waiting > WAIT_THRESHOLD_ERROR) {
                     syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: ERROR: DECODE(%d): %d (DECODE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!!)\n",
                            core->session_id,
                            n,
                            video_decode_frames_waiting);                                              
                 } else if (video_decode_frames_waiting > WAIT_THRESHOLD_WARNING) {
                     syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: WARNING: DECODE(%d): %d (DECODE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!!)\n",
                            core->session_id,
                            n,
                            video_decode_frames_waiting);                    
                 } 

                 video_deinterlace_frames_waiting = dataqueue_get_size(core->preparevideo->input_queue);
                 if (video_deinterlace_frames_waiting > WAIT_THRESHOLD_ERROR) {
                     syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: ERROR: DEINTERLACE(%d): %d (DEINTERLACE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!!)\n",
                            core->session_id,
                            n,
                            video_deinterlace_frames_waiting);                                              
                 } else if (video_deinterlace_frames_waiting > WAIT_THRESHOLD_WARNING) {
                     syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: WARNING: DEINTERLACE(%d): %d (DEINTERLACE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!!)\n",
                            core->session_id,
                            n,
                            video_deinterlace_frames_waiting);                    
                 }                 
                 
                 for (n = 0; n < core->cd->num_outputs; n++) {
                     int video_encode_frames_waiting;
                     
                     video_encode_frames_waiting = dataqueue_get_size(core->encodevideo->input_queue[n]);

                     // these are good numbers to start raising the alarm
                     if (video_encode_frames_waiting > WAIT_THRESHOLD_FAIL) {
                         syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: ERROR: ENCODE(%d): %d (ENCODE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!! UNRECOVERABLE!!!)\n",
                                core->session_id,
                                n,
                                video_encode_frames_waiting);
                         fprintf(stderr,"SESSION:%d (MAIN): STATUS: ERROR: ENCODE(%d): %d (ENCODE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!! UNRECOVERABLE!!!)\n",
                                 core->session_id,
                                 n,
                                 video_encode_frames_waiting);
                         exit(0);
                     } else if (video_encode_frames_waiting > WAIT_THRESHOLD_ERROR) {
                         syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: ERROR: ENCODE(%d): %d (ENCODE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!!)\n",
                                core->session_id,
                                n,
                                video_encode_frames_waiting);                         
                     } else if (video_encode_frames_waiting > WAIT_THRESHOLD_WARNING) {
                         syslog(LOG_INFO,"SESSION:%d (MAIN): STATUS: WARNING: ENCODE(%d): %d (ENCODE QUEUE FALLING BEHIND!!! CHECK CPU RESOURCES!!!)\n",
                                core->session_id,
                                n,
                                video_encode_frames_waiting);
                     }
                 }
                 loop_count = 0;
             }
         }
#endif // ENABLE_TRANSCODE         
         
         int msgid;

         usleep(1000);
         
         msgid = wait_for_event(core);
         if (msgid == -1) {
             //placeholder - this is the kill
             syslog(LOG_INFO,"SESSION:%d (MAIN) STATUS: DONE WAITING FOR EVENT- TIMEOUT/KILL\n", core->session_id);
             break;
         }
         if (msgid == MSG_START) {
             fprintf(stderr,"SESSION: %d (MAIN) STATUS: RECEIVED START MESSAGE\n", core->session_id);
             // this is not hooked up right now
         } else if (msgid == MSG_STOP) {
             fprintf(stderr,"SESSION: %d (MAIN) STATUS: RECEIVED STOP MESSAGE\n", core->session_id);
             core->source_running = 0;
             // this is not hooked up right now
         } else if (msgid == MSG_RESTART) {
             fprintf(stderr,"SESSION: %d (MAIN) STATUS: RECEIVED RESTART MESSAGE\n", core->session_id);
             // this is not hooked up right now
         } else if (msgid == MSG_RESPAWN) {
             exit(0);  // force respawn - if your docker container is set to restart
         } else {
             //fprintf(stderr,"SESSION: %d (MAIN) STATUS: NO MESSAGE TO PROCESS\n", core->session_id);
         }
     }
     pthread_join(client_thread_id, NULL);
cleanup_main_app:
     stop_webdav_threads(core);     
     if (core->hlsmux) {
         hlsmux_destroy(core->hlsmux);
         core->hlsmux = NULL;
     }
     destroy_fillet_core(core);
     fprintf(stderr,"STATUS: LEAVING APPLICATION\n");
     
     return 0;
}
