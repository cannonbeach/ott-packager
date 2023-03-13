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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "fillet.h"
#include "dataqueue.h"
#include "esignal.h"
#include "background.h"
#if defined(ENABLE_TRANSCODE)
#include "curl.h"
#endif // ENABLE_TRANSCODE

#define MAX_CONNECTIONS    8
#define MAX_REQUEST_SIZE   65535
#define MAX_RESPONSE_SIZE  MAX_REQUEST_SIZE

int wait_for_event(fillet_app_struct *core)
{
    dataqueue_message_struct *msg;
    int msgid = -1;

    msg = (dataqueue_message_struct*)dataqueue_take_back(core->event_queue);
    if (msg) {
        msgid = msg->flags;
        memory_return(core->fillet_msg_pool, msg);
        msg = NULL;
    } else {
        msgid = 0;
    }

    return msgid;
}

int build_response_repackage(fillet_app_struct *core, char *response_buffer, int *content_length, int full)
{
    char status_response[MAX_RESPONSE_SIZE];
    char response_timestamp[MAX_STR_SIZE];
#define MAX_LIST_SIZE MAX_RESPONSE_SIZE/4
    char input_streams[MAX_LIST_SIZE];
    char output_streams[MAX_LIST_SIZE];
    time_t current;
    struct tm currentUTC;
    int source;
    int output;

    memset(input_streams,0,sizeof(input_streams));
    memset(output_streams,0,sizeof(output_streams));

    for (source = 0; source < core->cd->active_video_sources; source++) {
        char scratch[MAX_STR_SIZE];
        int sub_source = 0;

        //audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[sub_source];
        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[source].video_stream;

        snprintf(scratch,MAX_STR_SIZE-1,"            \"stream%d\": {\n", source);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
#if defined(ENABLE_TRANSCODE)
        snprintf(scratch,MAX_STR_SIZE-1,"                \"source-ip\": \"%s:%d\",\n",
                 core->cd->active_video_source[source].active_ip,
                 core->cd->active_video_source[source].active_port);
#else
        snprintf(scratch,MAX_STR_SIZE-1,"                \"source-video-ip\": \"%s:%d\",\n",
                 core->cd->active_video_source[source].active_ip,
                 core->cd->active_video_source[source].active_port);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"source-audio-ip\": \"%s:%d\",\n",
                 core->cd->active_audio_source[source].active_ip,
                 core->cd->active_audio_source[source].active_port);
#endif
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-bitrate\": %ld,\n", vstream->video_bitrate);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-first-timestamp\": %ld,\n", vstream->first_timestamp);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-current-duration\": %ld,\n", vstream->last_full_time);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-received-frames\": %ld\n", vstream->current_receive_count);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        if (source == core->cd->active_video_sources - 1) {
            snprintf(scratch,MAX_STR_SIZE-1,"            }\n");
        } else {
            snprintf(scratch,MAX_STR_SIZE-1,"            },\n");
        }
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
    }

    current = time(NULL);
    gmtime_r(&current, &currentUTC);
    strftime(response_timestamp,MAX_STR_SIZE-1,"%Y-%m-%dT%H:%M:%SZ", &currentUTC);

    snprintf(status_response, MAX_RESPONSE_SIZE-1,
             "{\n"
             "    \"application\": \"fillet\",\n"
             "    \"version\": \"1.0.0\",\n"
             "    \"timestamp\": \"%s\",\n"
             "    \"status\": \"success\",\n"
             "    \"code\": 200,\n"
             "    \"message\": \"OK\",\n"
             "    \"data\": {\n"
             "        \"system\": {\n"
             "            \"input-signal\": %d,\n"
             "            \"uptime\": %ld,\n"
             "            \"transcoding\": %d,\n"
             "            \"source-interruptions\": %d,\n"
             "            \"source-errors\": %ld,\n"
             "            \"window-size\": %d,\n"
             "            \"segment-length\": %d,\n"
             "            \"youtube-active\": %d,\n"
             "            \"hls-active\": %d,\n"
             "            \"dash-fmp4-active\": %d,\n"
             "            \"scte35\": %d\n"
             "        },\n"
             "        \"source\": {\n"
             "            \"inputs\": %d,\n"
             "            \"interface\": \"%s\",\n"
             "%s"
             "        },\n"
             "        \"ad-insert\": {\n"
             "        },\n"
             "        \"output\": {\n"
             "            \"output-directory\": \"%s\",\n"
             "            \"hls-manifest\": \"%s\",\n"
             "            \"dash-manifest\": \"%s\",\n"
             "            \"fmp4-manifest\": \"%s\"\n"
             "        },\n"
             "        \"publish\": {\n"
             "        }\n"
             "    }\n"
             "}\n",
             response_timestamp,
             core->input_signal,
             core->uptime,
             core->transcode_enabled,
             core->source_interruptions,
             core->error_count,
             core->cd->window_size,
             core->cd->segment_length,
             core->cd->enable_youtube_output,
             core->cd->enable_ts_output,
             core->cd->enable_fmp4_output,
             core->cd->enable_scte35,
             core->cd->active_video_sources,
             core->cd->active_interface,
             input_streams,
             core->cd->manifest_directory,
             core->cd->manifest_hls,
             core->cd->manifest_dash,
             core->cd->manifest_fmp4);

    memset(response_buffer, 0, MAX_RESPONSE_SIZE);
    if (full) {
        *content_length = strlen(status_response)+4;
        snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                 "HTTP/1.1 200 OK\r\n"
                 "Server: FILLET\r\n"
                 "Access-Control-Allow-Methods: GET, POST\r\n"
                 "Content-Length: %d\r\n"
                 "\r\n"
                 "%s\r\n\r\n",
                 *content_length,
                 status_response);
    } else {
        snprintf(response_buffer, MAX_RESPONSE_SIZE-1, "%s", status_response);
        *content_length = strlen(response_buffer);
    }
    return 0;
}

#if defined(ENABLE_TRANSCODE)
int build_response_transcode(fillet_app_struct *core, char *response_buffer, int *content_length, int full)
{
    char status_response[MAX_RESPONSE_SIZE];
    char response_timestamp[MAX_STR_SIZE];
#define MAX_LIST_SIZE MAX_RESPONSE_SIZE/4
    char input_streams[MAX_LIST_SIZE];
    char output_streams[MAX_LIST_SIZE];
    time_t current;
    struct tm currentUTC;
    int source;
    int num_outputs = core->cd->num_outputs;
    int output;
    double latency;

    memset(input_streams,0,sizeof(input_streams));
    memset(output_streams,0,sizeof(output_streams));

    latency = 0;
    if (core->video_receive_time_set &&
        core->video_decode_time_set &&
        core->video_encode_time_set &&
        core->video_output_time_set) {
        latency = time_difference(&core->video_output_time, &core->video_receive_time) / 1000.0;
    }

    for (source = 0; source < core->cd->active_video_sources; source++) {
        char scratch[MAX_STR_SIZE];
        int sub_source = 0;

        //audio_stream_struct *astream = (audio_stream_struct*)core->source_stream[source].audio_stream[sub_source];
        video_stream_struct *vstream = (video_stream_struct*)core->source_video_stream[source].video_stream;

        snprintf(scratch,MAX_STR_SIZE-1,"            \"stream%d\": {\n", source);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"source-ip\": \"%s:%d\",\n",
                 core->cd->active_video_source[source].active_ip,
                 core->cd->active_video_source[source].active_port);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-bitrate\": %ld,\n", vstream->video_bitrate);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-first-timestamp\": %ld,\n", vstream->first_timestamp);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-current-duration\": %ld,\n", vstream->last_full_time);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-received-frames\": %ld\n", vstream->current_receive_count);
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
        if (source == core->cd->active_video_sources - 1) {
            snprintf(scratch,MAX_STR_SIZE-1,"            }\n");
        } else {
            snprintf(scratch,MAX_STR_SIZE-1,"            },\n");
        }
        strncat(input_streams, scratch, MAX_LIST_SIZE-1);
    }

    for (output = 0; output < num_outputs; output++) {
        char scratch[MAX_STR_SIZE];
        int output_width = core->cd->transvideo_info[output].width;
        int output_height = core->cd->transvideo_info[output].height;
        int video_bitrate = core->cd->transvideo_info[output].video_bitrate;

        snprintf(scratch,MAX_STR_SIZE-1,"            \"stream%d\": {\n", output);
        strncat(output_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"output-width\": %d,\n",
                 output_width);
        strncat(output_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"output-height\": %d,\n",
                 output_height);
        strncat(output_streams, scratch, MAX_LIST_SIZE-1);
        snprintf(scratch,MAX_STR_SIZE-1,"                \"video-bitrate\": %d\n",
                 video_bitrate);
        strncat(output_streams, scratch, MAX_LIST_SIZE-1);

        if (output == num_outputs - 1) {
            snprintf(scratch,MAX_STR_SIZE-1,"            }\n");
        } else {
            snprintf(scratch,MAX_STR_SIZE-1,"            },\n");
        }
        strncat(output_streams, scratch, MAX_LIST_SIZE-1);
    }

    current = time(NULL);
    gmtime_r(&current, &currentUTC);
    strftime(response_timestamp,MAX_STR_SIZE-1,"%Y-%m-%dT%H:%M:%SZ", &currentUTC);

    snprintf(status_response, MAX_RESPONSE_SIZE-1,
             "{\n"
             "    \"application\": \"fillet\",\n"
             "    \"version\": \"1.0.0\",\n"
             "    \"timestamp\": \"%s\",\n"
             "    \"status\": \"success\",\n"
             "    \"code\": 200,\n"
             "    \"message\": \"OK\",\n"
             "    \"data\": {\n"
             "        \"system\": {\n"
             "            \"input-signal\": %d,\n"
             "            \"uptime\": %ld,\n"
             "            \"transcoding\": %d,\n"
             "            \"codec\": %d,\n"
             "            \"profile\": %d,\n"
             "            \"quality\": %d,\n"
             "            \"source-interruptions\": %d,\n"
             "            \"source-errors\": %ld,\n"
             "            \"window-size\": %d,\n"
             "            \"segment-length\": %d,\n"
             "            \"youtube-active\": %d,\n"
             "            \"hls-active\": %d,\n"
             "            \"dash-fmp4-active\": %d,\n"
             "            \"scte35\": %d,\n"
             "            \"latency\": \"%.2f ms\"\n"
             "        },\n"
             "        \"source\": {\n"
             "            \"inputs\": %d,\n"
             "            \"stream-select\": %d,\n"
             "            \"width\": %d,\n"
             "            \"height\": %d,\n"
             "            \"fpsnum\": %d,\n"
             "            \"fpsden\": %d,\n"
             "            \"aspectnum\": %d,\n"
             "            \"aspectden\": %d,\n"
             "            \"videomediatype\": %d,\n"
             "            \"audiomediatype0\": %d,\n"
             "            \"audiomediatype1\": %d,\n"
             "            \"audiochannelsinput0\": %d,\n"
             "            \"audiochannelsinput1\": %d,\n"
             "            \"audiochannelsoutput0\": %d,\n"
             "            \"audiochannelsoutput1\": %d,\n"
             "            \"audiosamplerate0\": %d,\n"
             "            \"audiosamplerate1\": %d,\n"
             "            \"interface\": \"%s\",\n"
             "%s"
             "        },\n"
             "        \"ad-insert\": {\n"
             "        },\n"
             "        \"output\": {\n"
             "            \"output-directory\": \"%s\",\n"
             "            \"hls-manifest\": \"%s\",\n"
             "            \"dash-manifest\": \"%s\",\n"
             "            \"fmp4-manifest\": \"%s\",\n"
             "            \"outputs\": %d,\n"
             "%s"
             "        },\n"
             "        \"publish\": {\n"
             "        }\n"
             "    }\n"
             "}\n",
             response_timestamp,
             core->input_signal,
             core->uptime,
             core->transcode_enabled,
             core->cd->transvideo_info[0].video_codec,
             core->cd->transvideo_info[0].encoder_profile,
             core->cd->transvideo_info[0].encoder_quality,
             core->source_interruptions,
             core->error_count,
             core->cd->window_size,
             core->cd->segment_length,
             core->cd->enable_youtube_output,
             core->cd->enable_ts_output,
             core->cd->enable_fmp4_output,
             core->cd->enable_scte35,
             latency,
             core->cd->active_video_sources,
             core->cd->stream_select,
             core->decoded_source_info.decoded_width,
             core->decoded_source_info.decoded_height,
             core->decoded_source_info.decoded_fps_num,
             core->decoded_source_info.decoded_fps_den,
             core->decoded_source_info.decoded_aspect_num,
             core->decoded_source_info.decoded_aspect_den,
             core->decoded_source_info.decoded_video_media_type,
             core->decoded_source_info.decoded_audio_media_type[0],
             core->decoded_source_info.decoded_audio_media_type[1],
             core->decoded_source_info.decoded_audio_channels_input[0],
             core->decoded_source_info.decoded_audio_channels_input[1],
             core->decoded_source_info.decoded_audio_channels_output[0],
             core->decoded_source_info.decoded_audio_channels_output[1],
             core->decoded_source_info.decoded_audio_sample_rate[0],
             core->decoded_source_info.decoded_audio_sample_rate[1],
             core->cd->active_interface,
             input_streams,
             core->cd->manifest_directory,
             core->cd->manifest_hls,
             core->cd->manifest_dash,
             core->cd->manifest_fmp4,
             num_outputs,
             output_streams);

    memset(response_buffer, 0, MAX_RESPONSE_SIZE);
    if (full) {
        *content_length = strlen(status_response)+4;
        snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                 "HTTP/1.1 200 OK\r\n"
                 "Server: FILLET\r\n"
                 "Access-Control-Allow-Methods: GET, POST\r\n"
                 "Content-Length: %d\r\n"
                 "\r\n"
                 "%s\r\n\r\n",
                 *content_length,
                 status_response);
    } else {
        snprintf(response_buffer, MAX_RESPONSE_SIZE-1, "%s", status_response);
        *content_length = strlen(response_buffer);
    }
    return 0;
}
#endif // ENABLE_TRANSCODE

#if defined(ENABLE_TRANSCODE)
void *status_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    CURLcode curlresponse;
    CURL *curl = NULL;
    long http_code = 200;
    char *response_buffer = NULL;
    char signal_url[MAX_STR_SIZE];

    curl_global_init(CURL_GLOBAL_ALL);
    response_buffer = (char*)malloc(MAX_RESPONSE_SIZE);
    while (1) {
        struct curl_slist *optional_data = NULL;
        int content_length = 0;

        curl = curl_easy_init();
        optional_data = curl_slist_append(optional_data, "Content-Type: application/json");
        optional_data = curl_slist_append(optional_data, "Expect:");

        if (core->transcode_enabled) {
            build_response_transcode(core, response_buffer, &content_length, 0);
        } else {
            build_response_repackage(core, response_buffer, &content_length, 0);
        }

        fprintf(stderr,"%s", response_buffer);

        snprintf(signal_url,MAX_STR_SIZE-1,"http://127.0.0.1:8080/api/v1/status_update/%d",core->cd->identity);

        curl_easy_setopt(curl, CURLOPT_URL, signal_url);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (char*)response_buffer);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)content_length);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, optional_data);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2);
        curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

        curlresponse = curl_easy_perform(curl);
        if (curlresponse != CURLE_OK) {
            syslog(LOG_ERR,"SESSION:%d (RESTFUL) FATAL ERROR: UNABLE TO PROVIDE STATUS INFO\n",
                   core->session_id);
        }
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, (long*)&http_code);
        fprintf(stderr,"SESSION:%d (RESTFUL) STATUS: HTTPCODE:%lu\n",
                core->session_id,
                (long)http_code);
        curl_easy_cleanup(curl);
        curl_slist_free_all(optional_data);
        optional_data = NULL;

        sleep(1);
    }
    free(response_buffer);
    return NULL;
}
#endif // ENABLE_TRANSCODE

void *client_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    int server = -1;
    int port = 18000;
    fd_set commset;
    int err;
    char *request_buffer = NULL;
    char *request = NULL;
    char *response_buffer = NULL;
    int request_size;
    int client = -1;
    struct timeval server_wait;

    server_wait.tv_sec = 0;
    server_wait.tv_usec = 150000;

    request_buffer = (char*)malloc(MAX_REQUEST_SIZE);
    response_buffer = (char*)malloc(MAX_RESPONSE_SIZE);
    while (1) {
        if (server == -1) {
            server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (server > 0) {
                struct sockaddr_in commserver;
                int yesflag = 1;

                memset(&commserver, 0, sizeof(commserver));

                // placeholder
                // add option for local server loopback only
                commserver.sin_family = AF_INET;
                commserver.sin_port = htons(port);
                commserver.sin_addr.s_addr = htonl(INADDR_ANY);

                setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&yesflag, sizeof(yesflag));

                err = bind(server, (struct sockaddr*)&commserver, sizeof(commserver));
                if (err < 0) {
                    syslog(LOG_ERR,"SESSION:%d (RESTFUL) FATAL ERROR: UNABLE TO BIND TO INTERFACE\n",
                           core->session_id);
                    close(server);
                    server = -1;
                    sleep(1);
                    //placeholder - should instance quit and reinstantiate
                    continue;
                } else {
                    setsockopt(server, IPPROTO_TCP, TCP_NODELAY, (char*)&yesflag, sizeof(yesflag));
                    err = listen(server, MAX_CONNECTIONS);
                    if (err < 0) {
                        syslog(LOG_ERR,"SESSION:%d (RESTFUL) FATAL ERROR: UNABLE TO LISTEN TO INTERFACE\n",
                               core->session_id);
                        close(server);
                        server = -1;
                        sleep(1);
                        //placeholder - should instance quit and reinstantiate
                        continue;
                    }
                }
            } else {
                syslog(LOG_ERR,"SESSION:%d (RESTFUL) FATAL ERROR: UNABLE TO OPEN RESTFUL SOCKET\n",
                       core->session_id);
                server = -1;
                sleep(1);
                //placeholder - should instance quit and reinstantiate
                continue;
            }
        }

        FD_ZERO(&commset);
        FD_SET(server, &commset);

        err = select(server + 1, &commset, NULL, NULL, &server_wait);
        if (err == 0) {
            // nothing - go back around again
            continue;
        }
        //incoming connection

        //spawn off to new thread if needed
        if (FD_ISSET(server, &commset)) {
            struct sockaddr_in newclientaddr;
            unsigned int newclientsize = sizeof(newclientaddr);

            client = accept(server, (struct sockaddr *)&newclientaddr, &newclientsize);
            syslog(LOG_INFO,"SESSION:%d (RESTFUL) STATUS: RECEIVED CONNECTION ON PORT %d FROM %s:%d\n",
                   core->session_id, port, inet_ntoa(newclientaddr.sin_addr), ntohs(newclientaddr.sin_port));

            memset(request_buffer, 0, MAX_REQUEST_SIZE);
            request_size = recv(client, request_buffer, MAX_REQUEST_SIZE-1, 0);
            if (request_size > 0) {
                char method[MAX_STR_SIZE];
                char url[MAX_STR_SIZE];
                char httpver[MAX_STR_SIZE];
                int vals;

                memset(method, 0, sizeof(method));
                memset(url, 0, sizeof(url));
                memset(httpver, 0, sizeof(httpver));

                request = request_buffer;
                //fix string handling- we don't want sscanf to go out of bounds
                vals = sscanf((char*)request,"%20s %128s HTTP/%15s", method, url, httpver);
                if (vals != 3) {
                    syslog(LOG_WARNING,"SESSION:%d (RESTFUL) WARNING: RECEIVED MALFORMED DATA\n",
                           core->session_id);
                    continue;
                }
                syslog(LOG_INFO,"SESSION:%d (RESTFUL) STATUS: RECEIVED HTTP REQUEST (METHOD:%s,URL:%s,VER:%s)\n",
                       core->session_id, method, url, httpver);

                if (strncmp(method,"GET",3) == 0) {
                    int ping_event = 0;
                    int status_event = 0;
                    // "GET" methods
                    if (strncmp(url,"/api/v1/ping",12) == 0) { // ping event - no action
                        ping_event = 1;
                    }
                    if (strncmp(url,"/api/v1/status",14) == 0) { // status event
                        status_event = 1;
                    }

                    if (ping_event) {
                        char *status_response = "{\"status\":[\"pong\"]}";
                        int content_length = strlen(status_response)+4;
                        memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                        snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                                 "HTTP/1.1 200 OK\r\n"
                                 "Server: fillet\r\n"
                                 "Access-Control-Allow-Methods: GET, POST\r\n"
                                 "Content-Length: %d\r\n"
                                 "\r\n"
                                 "%s\r\n\r\n",
                                 content_length,
                                 status_response);
                        send(client, response_buffer, strlen(response_buffer), 0);
                        close(client);
                        client = -1;
                        syslog(LOG_WARNING,"SESSION:%d (RESTFUL) STATUS: RECEIVED PING REQUEST\n",
                               core->session_id);
                    } else if (status_event) {
                        if (!core->source_running) {
                            char *status_response = "{\"status\":[\"inactive session\"]}";
                            int content_length = strlen(status_response)+4;
                            memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                            snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                                     "HTTP/1.1 200 OK\r\n"
                                     "Server: FILLET\r\n"
                                     "Access-Control-Allow-Methods: GET, POST\r\n"
                                     "Content-Length: %d\r\n"
                                     "\r\n"
                                     "%s\r\n\r\n",
                                     content_length,
                                     status_response);
                        } else {
                            int content_length = 0;
                            build_response_repackage(core, response_buffer, &content_length, 1);
                        }
                        send(client, response_buffer, strlen(response_buffer), 0);
                        close(client);
                        client = -1;
                        syslog(LOG_WARNING,"SESSION:%d (RESTFUL) STATUS: RECEIVED STATUS REQUEST\n",
                               core->session_id);
                    } else {
                        char *status_response = "{\"warning\":[\"invalid request\"]}";
                        int content_length = strlen(status_response)+4;
                        memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                        snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                                 "HTTP/1.1 200 OK\r\n"
                                 "Server: FILLET\r\n"
                                 "Access-Control-Allow-Methods: GET, POST\r\n"
                                 "Content-Length: %d\r\n"
                                 "\r\n"
                                 "%s\r\n\r\n",
                                 content_length,
                                 status_response);
                        send(client, response_buffer, strlen(response_buffer), 0);
                        close(client);
                        client = -1;
                        syslog(LOG_WARNING,"SESSION:%d (RESTFUL) STATUS: RECEIVED INVALID REQUEST\n",
                               core->session_id);
                    }
                    continue;
                } else if (strncmp(method,"POST",4) == 0) {
                    int start_event = 0;
                    int stop_event = 0;
                    int restart_event = 0;
                    int respawn_event = 0;
                    dataqueue_message_struct *msg;

                    // "POST" methods
                    if (strncmp(url,"/api/v1/start",13) == 0) {  // puts into run state
                        start_event = 1;
                    }
                    if (strncmp(url,"/api/v1/stop",12) == 0) {  // puts into stop state
                        stop_event = 1;
                    }
                    if (strncmp(url,"/api/v1/restart",15) == 0) {  // restarts the stack
                        restart_event = 1;
                    }
                    if (strncmp(url,"/api/v1/respawn",15) == 0) {  // kills the process and respawns it
                        // identify the pid
                        // kill the pid
                        respawn_event = 1;
                    }

                    if (respawn_event || restart_event || start_event || stop_event) {
                        //post to main thread something is ready
                        msg = (dataqueue_message_struct*)memory_take(core->fillet_msg_pool, sizeof(dataqueue_message_struct));
                        if (msg) {
                            memset(msg, 0, sizeof(dataqueue_message_struct));
                            if (start_event) {
                                msg->flags = MSG_START;
                            } else if (stop_event) {
                                msg->flags = MSG_STOP;
                            } else if (restart_event) {
                                msg->flags = MSG_RESTART;
                            } else if (respawn_event) {
                                msg->flags = MSG_RESPAWN;
                            }
                            syslog(LOG_INFO,"SESSION:%d (RESTFUL) STATUS: PROCESSING REQUEST (0x%x)\n",
                                   core->session_id,
                                   msg->flags);
                            dataqueue_put_front(core->event_queue, msg);
                        } else {
                            //placeholder
                            //unhandled message - we have bigger problems - maybe just quit?
                            char *status_response = "{\"error\":[\"internal error\"]}";
                            int content_length = strlen(status_response)+4;
                            memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                            snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                                     "HTTP/1.1 200 OK\r\n"
                                     "Server: fillet\r\n"
                                     "Access-Control-Allow-Methods: GET, POST\r\n"
                                     "Content-Length: %d\r\n"
                                     "\r\n"
                                     "%s\r\n\r\n",
                                     content_length,
                                     status_response);
                            send(client, response_buffer, strlen(response_buffer), 0);
                            close(client);
                            client = -1;
                            syslog(LOG_WARNING,"SESSION:%d (RESTFUL) WARNING: RECEIVED UNHANDLED REQUEST\n",
                                   core->session_id);
                            continue;
                        }
                        char *status_response = "{\"status\":[\"processing request\"]}";
                        int content_length = strlen(status_response)+4;
                        memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                        snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                                 "HTTP/1.1 200 OK\r\n"
                                 "Server: fillet\r\n"
                                 "Access-Control-Allow-Methods: GET, POST\r\n"
                                 "Content-Length: %d\r\n"
                                 "\r\n"
                                 "%s\r\n\r\n",
                                 content_length,
                                 status_response);
                        send(client, response_buffer, strlen(response_buffer), 0);
                        close(client);
                        client = -1;
                        continue;
                    } else {
                        char *status_response = "{\"error\":[\"invalid request\"]}";
                        int content_length = strlen(status_response)+4;
                        memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                        snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                                 "HTTP/1.1 200 OK\r\n"
                                 "Server: fillet\r\n"
                                 "Access-Control-Allow-Methods: GET, POST\r\n"
                                 "Content-Length: %d\r\n"
                                 "\r\n"
                                 "%s\r\n\r\n",
                                 content_length,
                                 status_response);
                        send(client, response_buffer, strlen(response_buffer), 0);
                        close(client);
                        client = -1;
                        syslog(LOG_WARNING,"SESSION:%d (RESTFUL) WARNING: RECEIVED INVALID REQUEST\n",
                               core->session_id);
                    }
                    continue;
                } else {
                    char *status_response = "{\"error\":[\"invalid request\"]}";
                    int content_length = strlen(status_response)+4;
                    memset(response_buffer, 0, MAX_RESPONSE_SIZE);
                    snprintf(response_buffer, MAX_RESPONSE_SIZE-1,
                             "HTTP/1.1 200 OK\r\n"
                             "Server: fillet\r\n"
                             "Access-Control-Allow-Methods: GET, POST\r\n"
                             "Content-Length: %d\r\n"
                             "\r\n"
                             "%s\r\n\r\n",
                             content_length,
                             status_response);
                    send(client, response_buffer, strlen(response_buffer), 0);
                    close(client);
                    client = -1;
                    syslog(LOG_WARNING,"SESSION:%d (RESTFUL) WARNING: RECEIVED INVALID REQUEST\n",
                           core->session_id);
                    continue;
                }
            }
        }
    }

    free(request_buffer);
    free(response_buffer);
    return NULL;
}
