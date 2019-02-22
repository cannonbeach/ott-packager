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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <math.h>
#include <syslog.h>

#include "dataqueue.h"
#include "mempool.h"
#include "fgetopt.h"
#include "crc.h"
#include "tsdecode.h"

static uint64_t total_input_packets = 0;

typedef int (*MYCALLBACK)(int p1, int64_t p2, int64_t p3, int64_t p4, int64_t p5, int source, void *context);
typedef int (*SAMPLE_CALLBACK)(uint8_t *sample, int sample_size, int sample_type, uint32_t sample_flags, int64_t pts, int64_t dts, int64_t last_pcr, int source, int sub_source, char *lang_tag, void *context);

static MYCALLBACK backup_caller = NULL;
static void *backup_context = NULL;

static SAMPLE_CALLBACK send_frame_func = NULL;
static void *send_frame_context = NULL;

static pthread_mutex_t pmt_lock = PTHREAD_MUTEX_INITIALIZER;

void register_frame_callback(int (*cbfn)(uint8_t *sample, int sample_size, int sample_type, uint32_t sample_flags, int64_t pts, int64_t dts, int64_t last_pcr, int source, int sub_source, char *lang_tag, void *context), void *context)
{
    send_frame_func = cbfn;
    send_frame_context = context;
}

void register_message_callback(int (*cbfn)(int p1,int64_t p2,int64_t p3,int64_t p4, int64_t p5, int source, void* context), void*context)
{
    backup_caller = cbfn;
    backup_context = context;
}

int64_t get_time_difference(struct timeval *stoptime, struct timeval *starttime)
{
     int64_t delta_sec;
     int64_t delta_usec;
     int64_t temp_delta_sec;
     int64_t temp_delta_usec;
     int64_t final_time;

     delta_sec = stoptime->tv_sec - starttime->tv_sec;
     delta_usec = stoptime->tv_usec - starttime->tv_usec;
     if (delta_usec < 0) {
          temp_delta_sec = delta_sec - 1;
          temp_delta_usec = 1000000 + delta_usec;
     } else {
          temp_delta_sec = delta_sec;
          temp_delta_usec = delta_usec;
     }
     final_time = (temp_delta_sec * 1000000) + temp_delta_usec;

     return final_time;
}

static int decode_tvct_table(unsigned char *tvct_data, int tvct_data_size, int current_pid)
{
     return 0;
}

static int decode_pmt_table(pat_table_struct *master_pat_table, pmt_table_struct *master_pmt_table, unsigned char *pmt_data, int pmt_data_size, int current_pid)
{
     unsigned char *pdata = (unsigned char *)pmt_data;
     int pmt_program = (*(pdata+4) << 8) + *(pdata+5);
     int pmt_version = (*(pdata+6) & 0x1e) >> 1;
     int pmt_valid = *(pdata+6) & 0x01;
     int current_pmt_section = *(pdata+7);
     int previous_pmt_section = *(pdata+8);
     int pcr_pid = ((*(pdata+9) << 8) + *(pdata+10)) & 0x1fff;
     int program_info_length = ((*(pdata+11) << 8) + *(pdata+12)) & 0x0fff;
     int pmt_remaining = pmt_data_size;
     int descriptor_count = 0;
     int stream_count = 0;
     int pmt_count;
     int current_pmt_index = 0;
     int pmt_found = 0;
     pmt_table_struct *current_pmt_table = NULL;

     pthread_mutex_lock(&pmt_lock);
     for (pmt_count = 0; pmt_count < MAX_PMT_PIDS; pmt_count++) {
	 if (master_pmt_table[pmt_count].pmt_pid == current_pid) {
	     current_pmt_index = pmt_count;
	     pmt_found = 1;
	     break;
	 }
	 if (master_pmt_table[pmt_count].pmt_pid == 0) {
	     break;
	 }
     }
     if (!pmt_found) {
	 current_pmt_index = pmt_count;
	 master_pat_table->pmt_table_entries++;
     }
     
     current_pmt_table = (pmt_table_struct *)&master_pmt_table[current_pmt_index];

     current_pmt_table->pmt_pid = current_pid;
     current_pmt_table->pmt_data_size = pmt_data_size;
     current_pmt_table->pmt_program_number = pmt_program;
     current_pmt_table->pmt_valid = pmt_valid;
     current_pmt_table->current_pmt_section = current_pmt_section;
     current_pmt_table->previous_pmt_section = previous_pmt_section;
     current_pmt_table->pcr_pid = pcr_pid;
     current_pmt_table->program_info_length = program_info_length;
     current_pmt_table->pmt_version = pmt_version;     
     current_pmt_table->audio_stream_count = 0;

     if (pmt_data_size <= MAX_TABLE_SIZE) {
	 memcpy(current_pmt_table->pmt_data, pmt_data, pmt_data_size);
     }

     pdata += 13;
     pmt_remaining = pmt_data_size - 13;

     if (program_info_length > pmt_remaining ||
	 program_info_length < 0 ||
	 program_info_length > MAX_TABLE_SIZE) {

         syslog(LOG_ERR,"PMT TABLE ERROR: TABLE SIZE INVALID: %d\n", pmt_remaining);
	 
	 //backup_caller(2000, 503, 0, 0, 0, 0, backup_context);

         pthread_mutex_unlock(&pmt_lock);
	 return -1;
     }

     while (program_info_length > 0) {
          int descriptor;
          int descriptor_size;

          descriptor = *(pdata+0);
          descriptor_size = *(pdata+1);

          if (descriptor_size < 0 ||
	      descriptor_size > program_info_length) {
	      backup_caller(2000, 504, 0, 0, 0, 0, backup_context);

              pthread_mutex_unlock(&pmt_lock);              
	      return -1;
          }

          current_pmt_table->descriptor_id[descriptor_count] = descriptor;
          current_pmt_table->descriptor_size[descriptor_count] = descriptor_size;
          current_pmt_table->descriptor_count++;
          descriptor_count++;

          if (descriptor == PMT_DESCRIPTOR_PRIVATE1) {
	      backup_caller(2000, 601, descriptor, current_pid, 0, 0, backup_context);
          } else if (descriptor == PMT_DESCRIPTOR_PRIVATE2) {
	      backup_caller(2000, 602, descriptor, current_pid, 0, 0, backup_context);
          } else if (descriptor == PMT_DESCRIPTOR_REGISTRATION) {
	      backup_caller(2000, 610, descriptor, current_pid, 0, 0, backup_context);
          } else if (descriptor == PMT_DESCRIPTOR_MAX_BITRATE) {
	      int max_bitrate = ((int)pdata[3] << 8) + (int)pdata[4];
	      backup_caller(2000, 611, descriptor, max_bitrate, current_pid, 0, backup_context);
          } else if (descriptor == PMT_DESCRIPTOR_MUX_BUFFER) {
	      backup_caller(2000, 612, descriptor, current_pid, 0, 0, backup_context);
          } else {
	      backup_caller(2000, 600, descriptor, current_pid, 0, 0, backup_context);
          }

          pdata += (descriptor_size + 2);
          program_info_length -= (descriptor_size + 2);
          pmt_remaining -= (descriptor_size + 2);
     }

     if (pmt_remaining < 0 || program_info_length < 0) {
	 backup_caller(2000, 504, 0, 0, 0, 0, backup_context);
         pthread_mutex_unlock(&pmt_lock);         
	 return -1;
     }

     while (pmt_remaining > 2) {
          int current_stream_type = *(pdata+0);
          int current_stream_pid = (int)(*(pdata+1) << 8) | (int)*(pdata+2);
          int pmt_info_length;
          int saved_position;
          unsigned char *saved_position_data;
          int saved_info_length;
          int local_count;
          int local_tag;
          int tag_index;
          int waiting_for_descriptor;

          current_stream_pid = current_stream_pid & 0x1fff;
          pmt_info_length = (((int)*(pdata+3) << 8) | (int)*(pdata+4)) & 0x0fff;

          current_pmt_table->stream_pid[stream_count] = current_stream_pid;
          current_pmt_table->stream_type[stream_count] = current_stream_type;
          current_pmt_table->audio_stream_index[stream_count] = -1;
          current_pmt_table->first_pts[stream_count] = -1;
          current_pmt_table->first_dts[stream_count] = -1;
          current_pmt_table->last_pts[stream_count] = -1;
          current_pmt_table->last_dts[stream_count] = -1;
          waiting_for_descriptor = 0;

          current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_UNKNOWN; // default to unknown
          if (current_stream_type == 0x02) {
	      backup_caller(2000, 800, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_MPEG2;
          } else if (current_stream_type == 0x1b) {
	      backup_caller(2000, 801, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_H264;
          } else if (current_stream_type == 0x24) {
	      backup_caller(2000, 814, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_HEVC;
          } else if (current_stream_type == 0x01) {
	      backup_caller(2000, 802, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_MPEG;
          } else if (current_stream_type == 0x03) {
	      backup_caller(2000, 803, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_MPEG;
          } else if (current_stream_type == 0x04) {
	      backup_caller(2000, 804, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_MPEG;
          } else if (current_stream_type == 0x0f) {
	      backup_caller(2000, 805, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_AAC;
              current_pmt_table->audio_stream_index[stream_count] = current_pmt_table->audio_stream_count;
              current_pmt_table->audio_stream_count++;
          } else if (current_stream_type == 0x81) {
	      backup_caller(2000, 806, current_stream_pid, current_pid, 0, 0, backup_context);
	      current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_AC3;
              current_pmt_table->audio_stream_index[stream_count] = current_pmt_table->audio_stream_count;
              current_pmt_table->audio_stream_count++;
          } else if (current_stream_type == 0x27) {
	      backup_caller(2000, 807, current_stream_pid, current_pid, 0, 0, backup_context);
          } else if (current_stream_type == 0x06) {
	      waiting_for_descriptor = 0x06;
          } else if (current_stream_type == 0x05) {
	      backup_caller(2000, 809, current_stream_pid, current_pid, 0, 0, backup_context);
          } else if (current_stream_type == 0x0b) {
	      backup_caller(2000, 810, current_stream_pid, current_pid, 0, 0, backup_context);
          } else if (current_stream_type == 0x82) {
	      backup_caller(2000, 811, current_stream_pid, current_pid, 0, 0, backup_context);
          } else if (current_stream_type == 0x86) { // scte35
	      backup_caller(2000, 812, current_stream_pid, current_pid, 0, 0, backup_context);
              current_pmt_table->decoded_stream_type[stream_count] = STREAM_TYPE_SCTE35;             
          } else if (current_stream_type == 0xC0) {
	      backup_caller(2000, 813, current_stream_pid, current_pid, 0, 0, backup_context);
          }

          current_pmt_table->stream_count++;
          stream_count++;

          saved_position = pmt_remaining;
          saved_position_data = pdata;
          saved_info_length = pmt_info_length;

          pmt_remaining -= 5;
          pdata += 5;

_redo_decode:
          local_count = pmt_info_length;
          tag_index = 0;
          while (local_count > 0) {
               int h;
               int local_tag_size;

               local_tag = *(pdata+tag_index);
               local_tag_size = *(pdata+tag_index+1);
               tag_index += 2;
               local_count -= local_tag_size;
               local_count -= 2;

               fprintf(stderr,"PMT TABLE LOCAL TAG: 0x%x  WAITING:%d\n", local_tag, waiting_for_descriptor);

               if (local_tag == STREAM_DESCRIPTOR_IDENTIFIER) {
                    for (h = 0; h < local_tag_size; h++) {
                         // do something with the data here
                         tag_index++;
                    }
               } else if (local_tag == STREAM_DESCRIPTOR_VIDEO) {
                    for (h = 0; h < local_tag_size; h++) {
                         // do something with the data here
                         tag_index++;
                    }
               } else if (local_tag == STREAM_DESCRIPTOR_ALIGNMENT) {
                    int alignment_type;
                    for (h = 0; h < local_tag_size; h++) {
                         // do something with the data here
                         tag_index++;
                    }
                    if (local_tag_size == 1) {
                         alignment_type = *(pdata+3);
                    } else {
                         alignment_type = 0;
                    }
                    backup_caller(2000, 709, local_tag, alignment_type, 0, 0, backup_context);
               } else if (local_tag == STREAM_DESCRIPTOR_MAX_BITRATE) {
                    for (h = 0; h < local_tag_size; h++) {
                         // do something with the data here
                         tag_index++;
                    }
               } else if (local_tag == STREAM_DESCRIPTOR_AVC) {
                    for (h = 0; h < local_tag_size; h++) {
                         // do something with the data here
                         tag_index++;
                    }
                    backup_caller(2000, 711, local_tag, 0, 0, 0, backup_context);
               } else if (local_tag == STREAM_DESCRIPTOR_SUBTITLE1 ||
                          local_tag == STREAM_DESCRIPTOR_SUBTITLE2) {
                    if (waiting_for_descriptor) {
			backup_caller(2000, 808, current_stream_pid, current_pid, 0, 0, backup_context);
                         waiting_for_descriptor = 0;
                         goto _redo_decode;
                    } else {
			backup_caller(2000, 712, local_tag, 0, 0, 0, backup_context);
                    }
                    tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_EAC3) {
                    if (waiting_for_descriptor) {
			backup_caller(2000, 814, current_stream_pid, current_pid, 0, 0, backup_context);
                         waiting_for_descriptor = 0;
                         goto _redo_decode;
                    } else {
			backup_caller(2000, 713, local_tag, 0, 0, 0, backup_context);
                    }
                    tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_STD) {
		   backup_caller(2000, 714, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_SMOOTH) {
		   backup_caller(2000, 715, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_CAPTION) {
		   backup_caller(2000, 716, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_REGISTRATION) {
		   backup_caller(2000, 717, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_AC3) {
		   if (waiting_for_descriptor) {
		       waiting_for_descriptor = 0;
		       current_pmt_table->decoded_stream_type[stream_count - 1] = STREAM_TYPE_AC3;
                       current_pmt_table->stream_type[stream_count - 1] = 0x81;                       
                       current_pmt_table->audio_stream_index[stream_count - 1] = current_pmt_table->audio_stream_count;
                       current_pmt_table->audio_stream_count++;                       
		       goto _redo_decode;
		   }                   
		   backup_caller(2000, 718, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;                   
               } else if (local_tag == STREAM_DESCRIPTOR_LANGUAGE) {
		   uint8_t l1 = *(pdata+tag_index+0);
		   uint8_t l2 = *(pdata+tag_index+1);
		   uint8_t l3 = *(pdata+tag_index+2);

		   current_pmt_table->decoded_language_tag[stream_count - 1].lang_tag[0] = (char)l1;
		   current_pmt_table->decoded_language_tag[stream_count - 1].lang_tag[1] = (char)l2;
		   current_pmt_table->decoded_language_tag[stream_count - 1].lang_tag[2] = (char)l3;
		   current_pmt_table->decoded_language_tag[stream_count - 1].lang_tag[3] = '\0';
		   
		   backup_caller(2000, 719, local_tag, l1, l2, l3, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_APPLICATION) {
		   backup_caller(2000, 720, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_MPEGAUDIO) {
		   backup_caller(2000, 721, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else if (local_tag == STREAM_DESCRIPTOR_AC3_2) {
		   if (waiting_for_descriptor) {
                       fprintf(stderr,"status: setting decoded stream type to AC3 (stream_count:%d)\n",
                               stream_count);
		       waiting_for_descriptor = 0;
		       current_pmt_table->decoded_stream_type[stream_count - 1] = STREAM_TYPE_AC3;
                       current_pmt_table->stream_type[stream_count - 1] = 0x81;
                       current_pmt_table->audio_stream_index[stream_count - 1] = current_pmt_table->audio_stream_count;
                       current_pmt_table->audio_stream_count++;                                             
                       goto _redo_decode;
		   }
		   backup_caller(2000, 722, local_tag, 0, 0, 0, backup_context);
		   tag_index += local_tag_size;
               } else {
		   // the catch-all
		   tag_index += local_tag_size;
		   backup_caller(2000, 799, local_tag, 0, 0, 0, backup_context);
               }
          }

          pmt_remaining = saved_position;
          pdata = saved_position_data;
          pmt_info_length = saved_info_length;

          pmt_remaining -= (pmt_info_length + 5);
          pdata += (pmt_info_length + 5);
     }
     pthread_mutex_unlock(&pmt_lock);     
     return 0;
}

int decode_packets(uint8_t *transport_packet_data, int packet_count, transport_data_struct *tsdata)
{
     int packet_num;
     int each_pmt;

     for (packet_num = 0; packet_num < packet_count; packet_num++) {
          unsigned char *pdata = (unsigned char *)transport_packet_data + (packet_num * 188);
          unsigned char *pdata_initial = pdata + 4;

          if (*(pdata+0) == 0x47) {
               int pusi = (*(pdata+1) >> 6) & 0x1;
               int current_pid = (((int)*(pdata+1) << 8) + (int)*(pdata+2)) & 0x1fff;
               int afc = (*(pdata+3) >> 4) & 0x03;
               int cc = *(pdata+3) & 0x0F;
               int adaptation_size = 0;
               int pcr_flag = 0;

               int discontinuity_flag;
               int random_access_point;
               int pid_counter;
               int64_t current_ext;
               int64_t current_pcr;
               int64_t received_pcr;
               int64_t offset_pcr;
               int pid_in_list = 0;
               int new_pid = 0;
               int64_t update_pid_time = 0;

               pdata += 4;
               tsdata->received_ts_packets++;
               pdata_initial = pdata;

               if (total_input_packets == 0) {
                    gettimeofday(&tsdata->pid_start_time, NULL);
               }
               gettimeofday(&tsdata->pid_stop_time, NULL);
               update_pid_time = (int64_t)get_time_difference(&tsdata->pid_stop_time, &tsdata->pid_start_time);

               for (pid_counter = 0; pid_counter < MAX_PIDS; pid_counter++) {
		    if (tsdata->master_packet_table[pid_counter].pid == current_pid &&
			tsdata->master_packet_table[pid_counter].valid) {
			tsdata->master_packet_table[pid_counter].input_packets++;
			gettimeofday(&tsdata->master_packet_table[pid_counter].last_seen, NULL);
			pid_in_list = 1;
			break;
                    }
                    if (!tsdata->master_packet_table[pid_counter].valid) {
			
			// SEND MESSAGE INDICATING A NEW PID WAS FOUND
			// backup_caller();
			
			new_pid = pid_counter;
			break;
                    }
               }
               if (!pid_in_list) {
		   tsdata->master_packet_table[new_pid].valid = 1;
		   tsdata->master_packet_table[new_pid].input_packets = 1;
		   tsdata->master_packet_table[new_pid].pid = current_pid;
		   gettimeofday(&tsdata->master_packet_table[new_pid].last_seen, NULL);
               }

               total_input_packets++;

               if (afc & 2) {
                    if (afc == 2) {
                         adaptation_size = 183;
                    } else {
                         adaptation_size = *(pdata+0);
                    }
                    if (adaptation_size > 0) {
                         discontinuity_flag = !!(*(pdata+1) & 0x80);
                         if (discontinuity_flag) {
			     backup_caller(2000, 502, 0, 0, 0, 0, backup_context);
                         }
                         random_access_point = !!(*(pdata+1) & 0x40);
                         pcr_flag = !!(*(pdata+1) & 0x10);
                         if (pcr_flag) {
                              current_pcr = *(pdata+2);
                              current_pcr = (current_pcr << 8) | *(pdata+3);
                              current_pcr = (current_pcr << 8) | *(pdata+4);
                              current_pcr = (current_pcr << 8) | *(pdata+5);
                              current_pcr = current_pcr << 1;
                              if ((*(pdata+6) & 0x80) != 0) {
                                   current_pcr |= 1;
                              }
                              current_ext = (*(pdata+6) & 0x1) << 8;
                              current_ext = current_ext | *(pdata+7);

                              received_pcr = (current_pcr * 300) + current_ext;

                              if (tsdata->initial_pcr_base[current_pid] == -1) {
				  tsdata->initial_pcr_base[current_pid] = received_pcr;
				  tsdata->initial_pcr_ext = 0;
				  gettimeofday(&tsdata->pcr_start_time, NULL);
				  gettimeofday(&tsdata->pcr_update_start_time, NULL);
                              } else {
				  int64_t pcr_update_delta_time;
				  int check_mux_rate;
				  
				  gettimeofday(&tsdata->pcr_stop_time, NULL);
				  offset_pcr = received_pcr - tsdata->initial_pcr_base[current_pid];
				  pcr_update_delta_time = (int64_t)get_time_difference(&tsdata->pcr_stop_time, &tsdata->pcr_update_start_time);
				  
				  check_mux_rate = (27000000 * ((((double)tsdata->received_ts_packets*188.0)+10)*8.0))/(double)offset_pcr;

				  backup_caller(2000, 400, current_pid, check_mux_rate, 0, 0, backup_context);
				  if (pcr_update_delta_time > 1000000) {
				      gettimeofday(&tsdata->pcr_update_start_time, NULL);
				  }
                              }
                         }
                         pdata += adaptation_size;
                         pdata++;
                         adaptation_size++;
                    } else {
                         pdata++;
                         adaptation_size = 1;
                    }
               }

               if (afc & 1) {
		   if (pusi) {
		       int pid_count = 0;
                       int scte35_pid = 0;
                       int pid_loop;

                       if (tsdata->pmt_pid_count > 0) {
                           pmt_table_struct *current_pmt_table = (pmt_table_struct *)&tsdata->master_pmt_table[0];
                           for (pid_loop = 0; pid_loop < current_pmt_table->stream_count; pid_loop++) {
                               if (current_pmt_table->decoded_stream_type[pid_loop] == STREAM_TYPE_SCTE35) {
                                   scte35_pid = current_pmt_table->stream_pid[pid_loop];
                                   break;
                               }
                           }
                           if (current_pid == scte35_pid && scte35_pid != 0) {
                               int acquired_data_so_far = pdata - pdata_initial;
                               unsigned short section_size = ((*(pdata+2) << 8) + *(pdata+3)) & 0x0fff;
                               unsigned char table_id = pdata[1];
                               int protocol_version = pdata[4];
                               
                               int64_t pts_adjustment = ((int64_t)(pdata[5] & 0x01) << 32) | (int64_t)(pdata[6] << 24) | (int64_t)(pdata[7] << 16) | (int64_t)(pdata[8] << 8) | (int64_t)pdata[9];
                               int cw_index = pdata[10];
                               int tier = ((*(pdata+11) << 4) | ((*(pdata+12) & 0xf0) >> 4)) & 0x0fff;
                               int splice_command_length = (((*(pdata+12) & 0x0f) << 8) + *(pdata+13)) & 0x0fff;
                               int splice_command_type = pdata[14];
                               int64_t pts_time = 0;
                               int64_t pts_duration = 0;
                               int auto_return = 0;
                               int splice_immediate_flag = 0;
                               int unique_program_id = 0;
                               int cancel_indicator = 0;
                               int out_of_network_indicator = 0;
                               int64_t splice_event_id = 0;
                               /*
                               syslog(LOG_INFO,"SCTE35 TABLE ID: 0x%x  SECTIONSIZE:%d  VERSION:%d CW:%d TIER:%d CMDLEN:%d TYPE:0x%x  PTS-ADJUSTMENT:%ld\n",
                                      table_id,
                                      section_size, protocol_version,
                                      cw_index,
                                      tier,
                                      splice_command_length,
                                      splice_command_type,
                                      pts_adjustment);
                               */

                               if (splice_command_type == 0x05) {  // splice insert
                                   uint8_t *splice = (uint8_t*)pdata+15;
                                   splice_event_id = (int64_t)(splice[0] << 24) |
                                       (int64_t)(splice[1] << 16) |
                                       (int64_t)(splice[2] << 8) |
                                       (int64_t)splice[3];
                                   cancel_indicator = !!(splice[4] & 0x80);       // cancel_indicator- bottom 7-bits reserved
                                   syslog(LOG_INFO,"SCTE35: splice_event_id: %ld  0x%x   CANCEL:%d\n",
                                          splice_event_id, splice_event_id, cancel_indicator);
                                   splice += 5;
                                   if (!cancel_indicator) {
                                       out_of_network_indicator = !!(splice[0] & 0x80);
                                       int program_splice_flag = !!(splice[0] & 0x40);
                                       int duration_flag = !!(splice[0] & 0x20);
                                       splice_immediate_flag = !!(splice[0] & 0x10);
                                       // next 4-bits are reserved
                                       /*syslog(LOG_INFO,"SCTE35: out_of_network_indicator:%d program_splice_flag:%d duration_flag:%d splice_immediate_flag:%d\n",
                                              out_of_network_indicator,
                                              program_splice_flag,
                                              duration_flag,
                                              splice_immediate_flag);
                                       if (out_of_network_indicator) {
                                           syslog(LOG_INFO,"SCTE35: opportunity to exit from the network feed!\n");
                                       } else {
                                           syslog(LOG_INFO,"SCTE35: let's get back to the program - pts_adjustment is the intended point to head back!\n");
                                       }
                                       */
                                       splice++;
                                       if (program_splice_flag == 1 && splice_immediate_flag == 0) {
                                           // splice time table
                                           int time_specified_flag = !!(splice[0] & 0x80);
                                           if (time_specified_flag) {
                                               pts_time = ((int64_t)(splice[0] & 0x01) << 32) +
                                                   (int64_t)(splice[1] << 24) +
                                                   (int64_t)(splice[2] << 16) +
                                                   (int64_t)(splice[3] << 8) +
                                                   (int64_t)splice[4];
                                               pts_time = pts_time & 0x1ffffffff;
                                               //syslog(LOG_INFO,"SCTE35: PTS TIME (1/0): %ld\n", pts_time);
                                               splice += 5;
                                           } else {
                                               // next 7-bits are reserved
                                               splice++;
                                           }                                           
                                       } else if (program_splice_flag == 0) {
                                           // component_count
                                           int component_count = splice[0];
                                           int c;
                                           syslog(LOG_INFO,"SCTE35: COMPONENT COUNT: %d\n", component_count);
                                           splice++;
                                           for (c = 0; c < component_count; c++) {
                                               int component_tag = splice[0];
                                               splice++;
                                               if (splice_immediate_flag == 0) {
                                                   int time_specified_flag = !!(splice[0] & 0x80);
                                                   if (time_specified_flag) {
                                                       int64_t pts_time;
                                                       pts_time = ((int64_t)(splice[0] & 0x01) << 32) |
                                                           (int64_t)(splice[1] << 24) |
                                                           (int64_t)(splice[2] << 16) |
                                                           (int64_t)(splice[3] << 8) |
                                                           (int64_t)splice[4];
                                                       syslog(LOG_INFO,"SCTE35: PTS TIME (0/0): %ld\n", pts_time);
                                                       splice += 5;
                                                   } else {
                                                       // next 7-bits are reserved
                                                       splice++;
                                                   }                                                   
                                               }
                                           }
                                       }
                                       if (duration_flag) {
                                           // break_duration()
                                           auto_return = !!(splice[0] & 0x80);
                                           pts_duration = ((int64_t)(splice[0] & 0x01) << 32) |
                                               (int64_t)(splice[1] << 24) |
                                               (int64_t)(splice[2] << 16) |
                                               (int64_t)(splice[3] << 8) |
                                               (int64_t)splice[4];
                                           splice += 5;

                                           // when auto_return is 1- safety mechanism
                                           /*syslog(LOG_INFO,"SCTE35: auto_return:%d  pts_duration:%ld\n",
                                                  auto_return,
                                                  pts_duration);*/
                                       }
                                       int avail_num;
                                       int avails_expected;
                                       unique_program_id = (int)(splice[0] << 8) | (int)(splice[1]);
                                       avail_num = splice[2];
                                       avails_expected = splice[3];
                                       /*syslog(LOG_INFO,"SCTE35: unique program id: %d  avail: %d avails_expected:%d\n",
                                         unique_program_id, avail_num, avails_expected);*/
                                   }

                                   /*typedef struct _scte35_data_struct_ {
                                       int splice_command_type;
                                       int64_t pts_time;
                                       int64_t pts_duration;
                                       int64_t pts_adjustment;
                                       int splice_immediate;
                                       int program_id;
                                       int cancel;
                                       
                                   }*/
                                   
                                   scte35_data_struct *scte35_data;
                                   scte35_data = (scte35_data_struct*)malloc(sizeof(scte35_data_struct));
                                   if (scte35_data) {
                                       scte35_data->splice_command_type = 0x05;
                                       scte35_data->splice_event_id = splice_event_id;
                                       scte35_data->pts_time = pts_time;
                                       scte35_data->pts_duration = pts_duration;
                                       scte35_data->pts_adjustment = pts_adjustment;
                                       scte35_data->splice_immediate = splice_immediate_flag;
                                       scte35_data->program_id = unique_program_id;
                                       scte35_data->cancel = cancel_indicator;
                                       scte35_data->out_of_network_indicator = out_of_network_indicator;
                                   
                                       send_frame_func((uint8_t*)scte35_data, sizeof(scte35_data_struct), STREAM_TYPE_SCTE35, 1,
                                                       0, //pts
                                                       0, //dts
                                                       0, // PCR
                                                       tsdata->source,
                                                       0,
                                                       NULL,
                                                       send_frame_context);
                                       
                                       free(scte35_data);
                                       scte35_data = NULL;
                                   }
                               } // splice_command_type == 0x05
                           }
                       }
                       
		       if (current_pid == 7680) {
			   if (!tsdata->eit0_present) {
			       backup_caller(2000, 850, current_pid, current_pid, 0, 0, backup_context);
			       tsdata->eit0_present = 1;
			   }
		       } else if (current_pid == 7681) {
			   if (!tsdata->eit1_present) {
			       backup_caller(2000, 851, current_pid, current_pid, 0, 0, backup_context);
			       tsdata->eit1_present = 1;
			   }
		       } else if (current_pid == 7682) {
			   if (!tsdata->eit2_present) {
			       backup_caller(2000, 852, current_pid, current_pid, 0, 0, backup_context);
			       tsdata->eit2_present = 1;
			   }
		       } else if (current_pid == 7683) {
			   if (!tsdata->eit3_present) {
			       backup_caller(2000, 853, current_pid, current_pid, 0, 0, backup_context);
			       tsdata->eit3_present = 1;
			   }
		       } else if (current_pid == 0x1ffb) {
			   int acquired_data_so_far = pdata - pdata_initial;
			   unsigned short section_size = ((*(pdata+2) << 8) + *(pdata+3)) & 0x0fff;
			   unsigned char table_id = pdata[1];
			   
			   if (table_id == TABLE_ID_TVCT) {
			       tsdata->tvct_table_acquired = 184 - acquired_data_so_far;
			       tsdata->tvct_table_expected = section_size;
			       
			       if ((section_size+4) > tsdata->tvct_table_acquired) {
				   if (tsdata->tvct_table_acquired < 0 ||
				       tsdata->tvct_table_acquired > MAX_TABLE_SIZE ||
				       tsdata->tvct_table_expected > MAX_TABLE_SIZE ||
				       tsdata->tvct_table_expected < 0) {
				       
				       tsdata->tvct_table_acquired = 0;
				       tsdata->tvct_table_expected = 0;
				   } else {
				       memcpy(tsdata->tvct_data, pdata, tsdata->tvct_table_acquired);
				   }
			       } else {
				   if (tsdata->tvct_table_acquired < 0 ||
				       tsdata->tvct_table_acquired > MAX_TABLE_SIZE ||
				       tsdata->tvct_table_expected > MAX_TABLE_SIZE ||
				       tsdata->tvct_table_expected < 0) {
				       tsdata->tvct_table_acquired = 0;
				       tsdata->tvct_table_expected = 0;
				   } else {
				       unsigned short crc_position;
				       unsigned long crc32_length;
				       unsigned long *tvct_crc1;
				       unsigned long calculated_crc;
				       
				       memcpy(tsdata->tvct_data, pdata, tsdata->tvct_table_acquired);
				       
				       tsdata->tvct_data_size = tsdata->tvct_table_expected;
				       crc_position = ((tsdata->tvct_data[2] << 8) + tsdata->tvct_data[3]) & 0x0fff;
				       crc32_length = crc_position - 1;
				       if (crc_position > 4 && crc_position < 1020) {
					   unsigned long tvct_crc2;
					   tvct_crc1 = (unsigned long*)&tsdata->tvct_data[crc_position];
					   calculated_crc = getcrc32(&tsdata->tvct_data[1], crc32_length);
					   calculated_crc ^= 0xffffffff;
					   calculated_crc = htonl(calculated_crc);
					   tvct_crc2 = (unsigned long)*tvct_crc1;
					   tvct_crc2 ^= 0xffffffff;
					   
					   if (tvct_crc2 == calculated_crc) {
					       if (!tsdata->tvct_decoded) {
						   decode_tvct_table(tsdata->tvct_data, tsdata->tvct_data_size, current_pid);
						   backup_caller(2000, 854, current_pid, current_pid, 0, 0, backup_context);
					       }
					       tsdata->tvct_decoded = 1;
					   } else {
					       //backup_caller(2000, 201, calculated_crc, 0, 0, backup_context);
					   }
				       }
				       tsdata->tvct_table_acquired = 0;
				       tsdata->tvct_table_expected = 0;
				   }
			       }
			   }
		       }
		       
		       for (pid_count = 0; pid_count < tsdata->pmt_pid_count; pid_count++) {
			   if (tsdata->pmt_pid_index[pid_count] == current_pid) {
			       int acquired_data_so_far = pdata - pdata_initial;
			       int unit_size = pdata[0];
			       unsigned short section_size;
			       int pmt_version_input;
			       int table_id;
			       pdata += unit_size;
			       table_id = pdata[1];
			       
			       section_size = ((*(pdata+2) << 8) + *(pdata+3)) & 0x0fff;
			       pmt_version_input = (*(pdata+6) & 0x1e) >> 1;
			       
			       if (table_id != 0x02) {
				   goto continue_packet_processing;
			       }
			       
			       for (each_pmt = 0; each_pmt < tsdata->master_pat_table.pmt_table_entries; each_pmt++)  {
				   if (tsdata->master_pmt_table[each_pmt].pmt_pid == current_pid) {
				       if (tsdata->master_pmt_table[each_pmt].max_pmt_time == 0) {
					   tsdata->master_pmt_table[each_pmt].min_pmt_time = 999999999;
					   gettimeofday(&tsdata->master_pmt_table[each_pmt].start_pmt_time, NULL);
					   tsdata->master_pmt_table[each_pmt].max_pmt_time = 1;
				       } else {
					   int64_t delta_pmt_time;
					   gettimeofday(&tsdata->master_pmt_table[each_pmt].end_pmt_time, NULL);
					   delta_pmt_time = (int64_t)get_time_difference(&tsdata->master_pmt_table[each_pmt].end_pmt_time,
											 &tsdata->master_pmt_table[each_pmt].start_pmt_time);
					   
					   if (delta_pmt_time > tsdata->master_pmt_table[each_pmt].max_pmt_time) {
					       tsdata->master_pmt_table[each_pmt].max_pmt_time = delta_pmt_time;
					       // SIGNAL NEW MAX PMT TIME TO GUI
					       // backup_caller(2000, 505, delta_pmt_time, current_pid, 0, backup_context);
					   }
					   if (delta_pmt_time < tsdata->master_pmt_table[each_pmt].min_pmt_time) {
					       tsdata->master_pmt_table[each_pmt].min_pmt_time = delta_pmt_time;
					       // SIGNAL NEW MIN PMT TIME TO GUI
					       // backup_caller(2000, 506, delta_pmt_time, current_pid, 0, backup_context);
					   }
					   //backup_caller(2000, 505, delta_pmt_time / 1000, current_pid, 0, backup_context);
					   tsdata->master_pmt_table[each_pmt].avg_pmt_time += delta_pmt_time;
					   tsdata->master_pmt_table[each_pmt].avg_pmt_time /= 2;
					   gettimeofday(&tsdata->master_pmt_table[each_pmt].start_pmt_time, NULL);
				       }
				   }
			       }
			       
			       if (pmt_version_input != tsdata->pmt_version[pid_count] ||
				   tsdata->pmt_version[pid_count] == -1) {
				   tsdata->pmt_table_acquired = 184 - acquired_data_so_far;
				   tsdata->pmt_table_expected = section_size;
				   if (tsdata->pmt_position == 0) {
				       tsdata->pmt_position = total_input_packets;
				   }
				   if ((section_size+4) > tsdata->pmt_table_acquired) {
				       if (tsdata->pmt_table_acquired < 0 ||
					   tsdata->pmt_table_acquired > MAX_TABLE_SIZE ||
					   tsdata->pmt_table_expected > MAX_TABLE_SIZE ||
					   tsdata->pmt_table_expected < 0) {
					   tsdata->pmt_table_acquired = 0;
					   tsdata->pmt_table_expected = 0;
				       } else {
					   memcpy(tsdata->pmt_data, pdata, tsdata->pmt_table_acquired);
				       }
				   } else {
				       if (tsdata->pmt_table_acquired < 0 ||
					   tsdata->pmt_table_acquired > MAX_TABLE_SIZE ||
					   tsdata->pmt_table_expected > MAX_TABLE_SIZE ||
					   tsdata->pmt_table_expected < 0) {
					   tsdata->pmt_table_acquired = 0;
					   tsdata->pmt_table_expected = 0;
				       } else {
					   unsigned short crc_position;
					   unsigned long crc32_length;
					   uint32_t *pmt_crc1;
					   uint32_t calculated_crc;
					   
					   memcpy(tsdata->pmt_data, pdata, tsdata->pmt_table_acquired);
					   tsdata->pmt_data_size = tsdata->pmt_table_expected;
					   crc_position = ((int)(tsdata->pmt_data[2] << 8) + (int)tsdata->pmt_data[3]) & 0x0fff;
					   crc32_length = crc_position - 1;
					   if (crc_position > 4 && crc_position < 1020) {
					       uint32_t pmt_crc2;
					       uint8_t *crcdata = (uint8_t*)&tsdata->pmt_data[crc_position];
					       
					       pmt_crc1 = (uint32_t*)crcdata;
					       
					       calculated_crc = getcrc32(&tsdata->pmt_data[1], crc32_length);
					       calculated_crc ^= 0xffffffff;
					       calculated_crc = htonl(calculated_crc);
					       
					       pmt_crc2 = (uint32_t)*pmt_crc1;
					       pmt_crc2 ^= 0xffffffff;
					       
					       if (pmt_crc2 == calculated_crc) {
						   tsdata->pmt_version[pid_count] = pmt_version_input;
						   decode_pmt_table(&tsdata->master_pat_table, tsdata->master_pmt_table, tsdata->pmt_data, tsdata->pmt_data_size, current_pid);
						   tsdata->pmt_decoded[pid_count] = 1;
					       } else {
						   backup_caller(2000, 201, calculated_crc, 0, 0, 0, backup_context);
					       }
					   }
					   tsdata->pmt_table_acquired = 0;
					   tsdata->pmt_table_expected = 0;
				       }
				   }
			       }
			   }
		       }
		       
		       for (each_pmt = 0; each_pmt < tsdata->master_pat_table.pmt_table_entries; each_pmt++)  {
			     int stream_count = tsdata->master_pmt_table[each_pmt].stream_count;
			     for (pid_count = 0; pid_count < stream_count; pid_count++) {
				 if (tsdata->master_pmt_table[each_pmt].stream_pid[pid_count] == current_pid) {
				     int last_cc;
				     int pes_length;
				     int cp;
				     int id0 = *(pdata+6);
				     int id1 = *(pdata+7);
				     
				     if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index > 0) {
					 unsigned char *video_frame;
					 int is_intra = 0;
					 int core_modified = 0;
					 int video_frame_size = tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index;
					 int video_bitrate = 0;
					 int video_framerate;
					 int stream_type;
					 int aspect_ratio;
					 int seqtype;
					 
					 stream_type = tsdata->master_pmt_table[each_pmt].stream_type[pid_count];

					 if (stream_type == 0x02 || stream_type == 0x80) {
					     int64_t delta_data_time;
					     
					     video_frame = (unsigned char*)tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer;
					     if (video_frame[0] == 0x00 && video_frame[1] == 0x00 &&
						 video_frame[2] == 0x01 && video_frame[3] == 0xb3) {
						 is_intra = 1;
					     }
					     if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count == 0) {
						 gettimeofday(&tsdata->master_pmt_table[each_pmt].data_engine[pid_count].start_data_time, NULL);
					     }
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count++;

					     gettimeofday(&tsdata->master_pmt_table[each_pmt].data_engine[pid_count].end_data_time, NULL);
					     delta_data_time = (int64_t)get_time_difference(&tsdata->master_pmt_table[each_pmt].data_engine[pid_count].end_data_time,
											    &tsdata->master_pmt_table[each_pmt].data_engine[pid_count].start_data_time);

					     if (delta_data_time > 30000000) {
						 float measured_fps = (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count * 1000000.0);
						 measured_fps = measured_fps / delta_data_time * 1000.0;
						 gettimeofday(&tsdata->master_pmt_table[each_pmt].data_engine[pid_count].start_data_time, NULL);
						 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count = 0;
						 backup_caller(2000, 1004, (long long)measured_fps, current_pid, 0, 0, backup_context);
					     }
					     
					     if (core_modified & 32) {
						 backup_caller(2000, 1000, video_framerate, current_pid, 0, 0, backup_context);
						 backup_caller(2000, 1001, video_bitrate, current_pid, 0, 0, backup_context);
					     }
					     if (core_modified & 8) {
						 backup_caller(2000, 1002,
							       tsdata->master_pmt_table[each_pmt].data_engine[pid_count].width,
							       tsdata->master_pmt_table[each_pmt].data_engine[pid_count].height,
							       current_pid, 0, backup_context);
					     }
					     if (core_modified & 2) {
						 backup_caller(2000, 1003,
							       aspect_ratio,
							       seqtype,
							       current_pid,
							       0,
							       backup_context);
					     }
					     send_frame_func(video_frame, video_frame_size, STREAM_TYPE_MPEG2, is_intra,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts,
							     0, // PCR
							     tsdata->source,
                                                             0, // sub-source is 0 for video
							     (char*)&tsdata->master_pmt_table[each_pmt].decoded_language_tag[pid_count].lang_tag[0],
							     send_frame_context);						  
					 } else if (stream_type == 0x0f) {
					     uint8_t *audio_frame = (unsigned char*)tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer;
					     send_frame_func(audio_frame, video_frame_size, STREAM_TYPE_AAC, 1,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts,
							     0, // PCR
							     tsdata->source,
                                                             tsdata->master_pmt_table[each_pmt].audio_stream_index[pid_count],  //sub-source
							     (char*)&tsdata->master_pmt_table[each_pmt].decoded_language_tag[pid_count].lang_tag[0],
							     send_frame_context);
					 } else if (stream_type == 0x81) {
					     uint8_t *audio_frame = (unsigned char*)tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer;
					     send_frame_func(audio_frame, video_frame_size, STREAM_TYPE_AC3, 1,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts,
							     0, // PCR
							     tsdata->source,
                                                             tsdata->master_pmt_table[each_pmt].audio_stream_index[pid_count], //sub-source
							     (char*)&tsdata->master_pmt_table[each_pmt].decoded_language_tag[pid_count].lang_tag[0],
							     send_frame_context);
                                         } else if (stream_type == 0x86) {
					     /*uint8_t *scte35_frame = (unsigned char*)tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer;
					     send_frame_func(scte35_frame, video_frame_size, STREAM_TYPE_SCTE35, 1,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts,
							     0, // PCR
							     tsdata->source,
                                                             tsdata->master_pmt_table[each_pmt].audio_stream_index[pid_count], //sub-source
							     (char*)&tsdata->master_pmt_table[each_pmt].decoded_language_tag[pid_count].lang_tag[0],
							     send_frame_context);*/
                                         } else if (stream_type == 0x24) {
					     int vf;
					     int nal_type;
					     int is_intra = 0;
					     video_frame = (unsigned char*)tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer;
					     for (vf = 0; vf < video_frame_size - 4; vf++) {
						 if (video_frame[vf] == 0x00 &&
						     video_frame[vf+1] == 0x00 &&
						     video_frame[vf+2] == 0x01) {
						     nal_type = (video_frame[vf+3] & 0x7f) >> 1;
						     if (nal_type == 20 || nal_type == 19) { 
							 is_intra = 1;
							 if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count == 0) {
							     tsdata->first_frame_intra = 1;
							     is_intra = 1;                                                             
							 }
                                                         break;
						     }
						 }
					     }
					     
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count++;

					     send_frame_func(video_frame, video_frame_size, STREAM_TYPE_HEVC, is_intra,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts,
							     0, // PCR
							     tsdata->source,
                                                             0, // sub-source is 0 for video                                                             
							     (char*)&tsdata->master_pmt_table[each_pmt].decoded_language_tag[pid_count].lang_tag[0],
							     send_frame_context);			 
                                         } else if (stream_type == 0x1b) {
					     int vf;
					     int nal_type;
					     int is_intra = 0;
					     video_frame = (unsigned char*)tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer;
					     for (vf = 0; vf < video_frame_size - 4; vf++) {
						 if (video_frame[vf] == 0x00 &&
						     video_frame[vf+1] == 0x00 &&
						     video_frame[vf+2] == 0x01) {
						     nal_type = video_frame[vf+3] & 0x1f;
                                                     //fprintf(stderr,"nal_type:0x%x\n", nal_type);
						     if (nal_type == 0x05 || nal_type == 0x07 || nal_type == 0x08) { 
							 is_intra = 1;
							 if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count == 0) {
							     tsdata->first_frame_intra = 1;
							     is_intra = 1;                                                             
							 }
                                                         break;
						     }
						 }
					     }
					     
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].video_frame_count++;

					     send_frame_func(video_frame, video_frame_size, STREAM_TYPE_H264, is_intra,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts,
							     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts,
							     0, // PCR
							     tsdata->source,
                                                             0, // sub-source is 0 for video                                                             
							     (char*)&tsdata->master_pmt_table[each_pmt].decoded_language_tag[pid_count].lang_tag[0],
							     send_frame_context);
					 }
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index = 0;
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts = 0;
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts = 0;
				     }

				     last_cc = tsdata->master_pmt_table[each_pmt].data_engine[pid_count].last_cc;
				     if (last_cc == -1) {
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].last_cc = cc;
				     } else {
					 int expected_continuity;
					 
					 expected_continuity = (last_cc + 1) % 16;
					 if (expected_continuity != cc) {
					     backup_caller(2000, 900+cc,
							   expected_continuity, current_pid,
							   total_input_packets, 0, backup_context);
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].corruption_count++;
					 }
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].last_cc = cc;
				     }
				     pes_length = (*(pdata+4) << 8) + *(pdata+5);
				     if (pes_length) {
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].wanted_data_size = pes_length;
				     }
                                     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].actual_data_size = 0;
				     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].context = NULL;
				     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts = 0;
				     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts = 0;
				     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index = 0;
				     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].flags = 0;
				     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pes_aligned = 0;

				     cp = 1;  // force

				     if (cp == 1) {
					 int pes_header_size;
					 int pes_aligned;
					 int timestamp_present;
					 int remaining_samples = 0;
					 
					 pes_header_size = *(pdata+8);
					 if (pes_header_size > 184) {
					     backup_caller(2000, 916, current_pid, 0, 0, 0, backup_context);
					     goto continue_packet_processing;
					 }
					 pes_aligned = (id0 & 0x04) >> 3;
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pes_aligned = pes_aligned;
					 pdata += 9;
					 timestamp_present = (id1 & 0xc0) >> 6;
					 if (timestamp_present == 1) {
					     backup_caller(2000, 917, current_pid, 0, 0, 0, backup_context);
					     goto continue_packet_processing;
					 } else if (timestamp_present == 2) {
					     int64_t current_pts;
					     int stream_count;
					     int pid_index;
					     
					     current_pts = (*(pdata+0) >> 1) & 0x07;
					     current_pts <<= 8;
					     current_pts |= *(pdata+1);
					     current_pts <<= 7;
					     current_pts |= (*(pdata+2) >> 1) & 0x7f;
					     current_pts <<= 8;
					     current_pts |= *(pdata+3);
					     current_pts <<= 7;
					     current_pts |= (*(pdata+4) >> 1) & 0x7f;
					     
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts = current_pts;

					     stream_count = tsdata->master_pmt_table[each_pmt].stream_count;
					     for (pid_index = 0; pid_index < stream_count; pid_index++) {
						 if (tsdata->master_pmt_table[each_pmt].stream_pid[pid_index] == current_pid) {
						     tsdata->master_pmt_table[each_pmt].last_pts[pid_index] = current_pts;
						     if (tsdata->master_pmt_table[each_pmt].first_pts[pid_index] == -1) {
							 tsdata->master_pmt_table[each_pmt].first_pts[pid_index] = current_pts;
							 break;
						     }
						 }
					     }
					     //backup_caller(2000, 919, current_pts, current_pid, 0, backup_context);
					 } else if (timestamp_present == 3) {
					     int64_t current_pts;
					     int64_t current_dts;
					     int stream_count;
					     int pid_index;
					     
					     current_pts = (*(pdata+0) >> 1) & 0x07;
					     current_pts <<= 8;
					     current_pts |= *(pdata+1);
					     current_pts <<= 7;
					     current_pts |= (*(pdata+2) >> 1) & 0x7f;
					     current_pts <<= 8;
					     current_pts |= *(pdata+3);
					     current_pts <<= 7;
					     current_pts |= (*(pdata+4) >> 1) & 0x7f;
					     
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts = current_pts;

					     stream_count = tsdata->master_pmt_table[each_pmt].stream_count;
					     for (pid_index = 0; pid_index < stream_count; pid_index++) {
						 if (tsdata->master_pmt_table[each_pmt].stream_pid[pid_index] == current_pid) {
						     tsdata->master_pmt_table[each_pmt].last_pts[pid_index] = current_pts;
						     if (tsdata->master_pmt_table[each_pmt].first_pts[pid_index] == -1) {
							 tsdata->master_pmt_table[each_pmt].first_pts[pid_index] = current_pts;
							 break;
						     }
						 }
					     }
					     
					     current_dts = (*(pdata+5) >> 1) & 0x07;
					     current_dts <<= 8;
					     current_dts |= *(pdata+6);
					     current_dts <<= 7;
					     current_dts |= (*(pdata+7) >> 1) & 0x7f;
					     current_dts <<= 8;
					     current_dts |= *(pdata+8);
					     current_dts <<= 7;
					     current_dts |= (*(pdata+9) >> 1) & 0x7f;
					     
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts = current_dts;
					     for (pid_index = 0; pid_index < stream_count; pid_index++) {
						 if (tsdata->master_pmt_table[each_pmt].stream_pid[pid_index] == current_pid) {
						     tsdata->master_pmt_table[each_pmt].last_dts[pid_index] = current_dts;
						     if (tsdata->master_pmt_table[each_pmt].first_dts[pid_index] == -1) {
							 tsdata->master_pmt_table[each_pmt].first_dts[pid_index] = current_dts;
							 break;
						     }
						 }
					     }
					 } else {
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].dts = 0;
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].pts = 0;
					 }
					 
					 pdata += pes_header_size;
					 remaining_samples = 184 - 9 - pes_header_size - adaptation_size;
					 if (remaining_samples < 0) {
					     goto continue_packet_processing;
					 }
					 if (remaining_samples > 0) {
					     if (remaining_samples >= 184) {
						 goto continue_packet_processing;
					     }
					     if (!tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer) {
						 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer = (unsigned char *)malloc(MAX_BUFFER_SIZE);
					     }
					     if (remaining_samples <= MAX_BUFFER_SIZE) {
						 memcpy(tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer, pdata, remaining_samples);
					     }
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index = remaining_samples;
					 }
				     } // conditional parse
				     goto continue_packet_processing;
				 }
			     }
                         }

                         if (current_pid == 0) {
                              int unit_size = *(pdata+0);
                              pdata += unit_size;
                              unsigned char table_id = *(pdata+1);
                              unsigned short section_size = ((*(pdata+2) << 8) + *(pdata+3)) & 0x0fff;
                              int version_number = 0;
                              int transport_stream_id = (*(pdata+4) << 8) + *(pdata+5);
                              int section_entries;
                              int entry_index;
                              unsigned short pmt_pid;
                              int is_valid;
                              int current_section;
                              int last_section;
                              int look_for_pmt_table;
                              int pat_program_number;

                              section_size -= 9;
                              section_entries = section_size / 4;
                              entry_index = 9;

                              version_number = (*(pdata+6) & 0x1E) >> 1;
                              is_valid = *(pdata+6) & 0x01;
                              current_section = *(pdata+7);
                              last_section = *(pdata+8);

                              if (tsdata->master_pat_table.max_pat_time == 0) {
				  tsdata->master_pat_table.min_pat_time = 999999999;
				  gettimeofday(&tsdata->master_pat_table.start_pat_time, NULL);
				  tsdata->master_pat_table.max_pat_time = 1;
				  //backup_caller(2000, 505, 1000, current_pid, 0, backup_context);
                              } else {
                                   int64_t delta_pat_time;
                                   gettimeofday(&tsdata->master_pat_table.end_pat_time, NULL);
                                   delta_pat_time = (int64_t)get_time_difference(&tsdata->master_pat_table.end_pat_time,
										 &tsdata->master_pat_table.start_pat_time);

                                   if (delta_pat_time > tsdata->master_pat_table.max_pat_time) {
				       tsdata->master_pat_table.max_pat_time = delta_pat_time;
				       // SIGNAL NEW MAX PAT TIME TO GUI
				       // backup_caller(2000, 507, delta_pat_time, 0, 0, backup_context);
                                   }
                                   if (delta_pat_time < tsdata->master_pat_table.min_pat_time) {
				       tsdata->master_pat_table.min_pat_time = delta_pat_time;
				       // SIGNAL NEW MIN PAT TIME TO GUI
				       // backup_caller(2000, 508, delta_pat_time, 0, 0, backup_context);
                                   }
                                   //backup_caller(2000, 505, delta_pmt_time / 1000, current_pid, 0, backup_context);
                                   tsdata->master_pat_table.avg_pat_time += delta_pat_time;
                                   tsdata->master_pat_table.avg_pat_time /= 2;
                                   gettimeofday(&tsdata->master_pat_table.start_pat_time, NULL);
                              }

                              look_for_pmt_table = 0;
                              if (tsdata->pat_version_number == -1) {
                                   tsdata->pat_position = total_input_packets;
                                   tsdata->pat_version_number = version_number;
                                   tsdata->pat_program_count = section_entries;
                                   tsdata->pat_transport_stream_id = transport_stream_id;
                                   look_for_pmt_table = 1;
                                   backup_caller(2000, 100, tsdata->pat_program_count, 0, 0, 0, backup_context);
                              }

                              if (version_number != tsdata->pat_version_number) {
                                   tsdata->pat_version_number = version_number;
                                   tsdata->pat_program_count = section_entries;
                                   tsdata->pat_transport_stream_id = transport_stream_id;
                                   look_for_pmt_table = 1;
                                   backup_caller(2000, 100, tsdata->pat_program_count, 0, 0, 0, backup_context);
                              }

                              if (look_for_pmt_table == 1) {
                                   int program_index;

                                   tsdata->pmt_pid_count = 0;
                                   memset(tsdata->pmt_decoded, 0, sizeof(tsdata->pmt_decoded));
                                   for (program_index = 0; program_index < tsdata->pat_program_count; program_index++) {
                                        pat_program_number = (unsigned long)(((unsigned long)*(pdata+entry_index) >> 8) +
                                                                             (unsigned long)*(pdata+entry_index+1));

                                        if (pat_program_number == 0) {
					    backup_caller(2000, 300, 0, 0, 0, 0, backup_context);
                                        } else {
                                             pmt_pid = (unsigned short)((((unsigned long)*(pdata+entry_index+2) << 8)) |
                                                                        (unsigned long)(*(pdata+entry_index+3)));
                                             pmt_pid = pmt_pid & 0x1fff;

                                             backup_caller(2000, 200, pmt_pid, 0, 0, 0, backup_context);
                                             tsdata->pmt_pid_index[tsdata->pmt_pid_count] = pmt_pid;
                                             tsdata->pmt_pid_count++;
                                        }
                                        entry_index += 4;
                                   }

                                   if (!tsdata->pmt_pid_count) {
				       backup_caller(2000, 202, 0, 0, 0, 0, backup_context);
                                   }
                              }
                         }
                    } else { // NOT THE START
                         int pid_count = 0;
                         int acquired_data_so_far = 0;
                         int tempval;

                         tempval = pdata - pdata_initial;

                         acquired_data_so_far = 184 - tempval;

                         for (pid_count = 0; pid_count < tsdata->pmt_pid_count; pid_count++) {
                              if (tsdata->pmt_pid_index[pid_count] == current_pid) {
                                   if (tsdata->pmt_table_acquired > 0 &&
                                             tsdata->pmt_table_acquired < MAX_TABLE_SIZE &&
                                             tsdata->pmt_table_expected > 0) {
                                        int pmt_bytes_remaining = tsdata->pmt_table_expected - tsdata->pmt_table_acquired;

                                        if (tsdata->pmt_table_acquired + acquired_data_so_far > MAX_TABLE_SIZE) {
                                             acquired_data_so_far = pmt_bytes_remaining;
                                        }
                                        memcpy(&tsdata->pmt_data[tsdata->pmt_table_acquired], pdata, acquired_data_so_far);
                                        tsdata->pmt_table_acquired += acquired_data_so_far;

                                        if (tsdata->pmt_table_acquired >= tsdata->pmt_table_expected) {
                                             unsigned short crc_position;
                                             unsigned long crc32_length;
                                             unsigned long *pmt_crc1;
                                             unsigned long calculated_crc;

                                             tsdata->pmt_data_size = tsdata->pmt_table_expected;
                                             crc_position = ((tsdata->pmt_data[2] << 8) + tsdata->pmt_data[3]) & 0x0fff;
                                             crc32_length = crc_position - 1;
                                             if (crc_position > 4 && crc_position < 1020) {
                                                  unsigned long pmt_crc2;

                                                  pmt_crc1 = (unsigned long*)&tsdata->pmt_data[crc_position];
                                                  calculated_crc = getcrc32(&tsdata->pmt_data[1], crc32_length);
                                                  calculated_crc ^= 0xffffffff;
                                                  calculated_crc = htonl(calculated_crc);
                                                  pmt_crc2 = (unsigned long)*pmt_crc1;
                                                  pmt_crc2 ^= 0xffffffff;

                                                  if (pmt_crc2 == calculated_crc) {
                                                       int pmt_version = ((tsdata->pmt_data[6]) & 0x1e) >> 1;
                                                       if (pmt_version != tsdata->pmt_version[pid_count] ||
							   tsdata->pmt_version[pid_count] == -1) {
							   tsdata->pmt_version[pid_count] = pmt_version;
							   decode_pmt_table(&tsdata->master_pat_table, tsdata->master_pmt_table, tsdata->pmt_data, tsdata->pmt_data_size, current_pid);
                                                       }
                                                       tsdata->pmt_decoded[pid_count] = 1;
                                                  } else {
						      backup_caller(2000, 201, calculated_crc, 0, 0, 0, backup_context);
                                                  }
                                             }

                                             tsdata->pmt_table_acquired = 0;
                                             tsdata->pmt_table_expected = 0;
					} else {
					    // TODO
                                        }
                                        goto continue_packet_processing;
                                   }
                              }
                         }

                         for (each_pmt = 0; each_pmt < tsdata->master_pat_table.pmt_table_entries; each_pmt++)  {
			     int stream_count = tsdata->master_pmt_table[each_pmt].stream_count;
			     for (pid_count = 0; pid_count < stream_count; pid_count++) {
				 if (tsdata->master_pmt_table[each_pmt].stream_pid[pid_count] == current_pid) {
				     int last_cc;
				     
				     if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].actual_data_size > 0) {
					 // TODO
				     }
				     last_cc = tsdata->master_pmt_table[each_pmt].data_engine[pid_count].last_cc;
				     if (last_cc == -1) {
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].last_cc = cc;
				     } else {
					 int expected_continuity;
					 expected_continuity = (last_cc + 1) % 16;
					 if (expected_continuity != cc) {
					     backup_caller(2000, 900+cc,
							   expected_continuity, current_pid,
							   total_input_packets, 0, backup_context);
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].corruption_count++;
					 }
					 tsdata->master_pmt_table[each_pmt].data_engine[pid_count].last_cc = cc;
				     }

				     if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index > 0) {
					 int remaining_samples = 184 - adaptation_size;
					 if (remaining_samples < 0) {
					     goto continue_packet_processing;
					 }
					 if (tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index + remaining_samples <= MAX_BUFFER_SIZE) {
					     memcpy(tsdata->master_pmt_table[each_pmt].data_engine[pid_count].buffer +
						    tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index,
						    pdata,
						    remaining_samples);
					     tsdata->master_pmt_table[each_pmt].data_engine[pid_count].data_index += remaining_samples;
					 } else {
					     // TODO
					 }
				     }
				     
				 }
			     }
                         }
                    } // end of pusi
               }
          } // end of check for 0x47
	  
continue_packet_processing:
          each_pmt = 0;

     } // end of for loop
     return 0;
}
