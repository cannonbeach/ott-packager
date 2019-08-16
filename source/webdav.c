/*****************************************************************************                                                                                                                             
  Copyright (C) 2018-2019 John William
                                                                                                                                                                                                           
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

*******************************************************************************/

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
#include "webdav.h"
#if defined(ENABLE_TRANSCODE)
#include "curl.h"
#endif

static volatile int webdav_upload_thread_running = 0;
static pthread_t webdav_upload_thread_id;

static void *webdav_upload_thread(void *context);

int start_webdav_threads(fillet_app_struct *core)
{
    webdav_upload_thread_running = 1;
    pthread_create(&webdav_upload_thread_id, NULL, webdav_upload_thread, (void*)core);    
    return 0;
}

int stop_webdav_threads(fillet_app_struct *core)
{
    webdav_upload_thread_running = 0;
    pthread_join(webdav_upload_thread_id, NULL);
    return 0;
}

#if defined(ENABLE_TRANSCODE)
int webdav_delete_file(fillet_app_struct *core, char *directory, char *filename)
{
    if (!webdav_upload_thread_running) {
        //buffer_type = WEBDAV_DELETE
        return -1;
    }
    return 0;
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
    size_t retcode;
    curl_off_t nread;

    if (!webdav_upload_thread_running) {
        //abort?  do we return -1 or let it complete
    }  

    retcode = fread(ptr, size, nmemb, stream);
    nread = (curl_off_t)retcode;

    fprintf(stderr, "status: Callback read %" CURL_FORMAT_CURL_OFF_T " bytes from file\n", nread);

    return retcode;
}

static int seek_callback(void *stream, curl_off_t offset, int origin)
{
    int retcode;
    if (stream) {
        retcode = fseek(stream, offset, origin);
        if (retcode == -1) {
            return CURL_SEEKFUNC_CANTSEEK;
        } else {
            return CURL_SEEKFUNC_OK;
        }
    }
    //catchall
    return CURL_SEEKFUNC_FAIL;    
}

int webdav_upload_file(fillet_app_struct *core, char *filename)
{
    CURL *curl;
    char authentication_string[MAX_STR_SIZE];
    char cdn_url[MAX_STR_SIZE];    
    FILE *stream = NULL;
    char *actual_filename = NULL;
    int successful = 0;
    int success;
    int retry_count = 0;
    int retcode;
    long http_response;
    struct stat file_stats;
#define MAX_RETRIES 5    
    
    if (!webdav_upload_thread_running) {
        //buffer_type = WEBDAV_UPLOAD
        return -1;
    }

    while (!successful && retry_count < MAX_RETRIES) {
        stream = fopen(filename,"rb");
        if (!stream) {
            fprintf(stderr,"error: unable to transfer filename: %s\n", filename);
            //signal- file doesn't exist- error
            return -1;
        }
        actual_filename = rindex(filename,'/');
        if (actual_filename) {
            actual_filename++;
        } else {
            actual_filename = filename;
        }
        fprintf(stderr,"status: full filename w/path: %s\n", filename);
        fprintf(stderr,"status: actual filename (no path): %s\n", actual_filename);

        retcode = stat(filename, &file_stats);
        if (retcode != 0) {
            fprintf(stderr,"error: unable to obtain file stats: %s\n", filename);
            //signal- file has a problem? access rights?
            return -1;
        }
        
        curl = curl_easy_init();
        
        snprintf(authentication_string, MAX_STR_SIZE-1, "%s:%s",
                 core->cd->cdn_username,
                 core->cd->cdn_password);
        snprintf(cdn_url, MAX_STR_SIZE-1, "%s/%s",
                 core->cd->cdn_server,
                 actual_filename);

        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
        curl_easy_setopt(curl, CURLOPT_USERPWD, authentication_string);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
        curl_easy_setopt(curl, CURLOPT_PUT, 1);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
        curl_easy_setopt(curl, CURLOPT_URL, cdn_url);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, seek_callback);
        curl_easy_setopt(curl, CURLOPT_SEEKDATA, (void*)stream);    
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
        curl_easy_setopt(curl, CURLOPT_READDATA, (void*)stream);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_stats.st_size);

        //check for Expect: headers?

        success = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);

        if (http_response >= 400) {
            fprintf(stderr,"error: unable to upload file, retrying: %d\n", retry_count);
            successful = 0;
            retry_count++;
        }
        if (http_response >= 200 && http_response < 300) {
            double upload_time;
            double upload_speed;
            
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &upload_time);
            curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &upload_speed);
            fprintf(stderr,"status: file uploaded successfully on try %d\n",
                    retry_count);
            fprintf(stderr,"status: upload took %.2f seconds\n", upload_time);
            fprintf(stderr,"status: upload rate was %.2fkbytes per second\n", upload_speed / 1024.0);
            
            successful = 1;
        }
        curl_easy_cleanup(curl);
        if (stream) {
            fclose(stream);
            stream = NULL;
        }
    }
    if (retry_count >= MAX_RETRIES) {
        fprintf(stderr,"error: unable to upload file, no more retries!\n");
        //signal
    }
    
    return 0;
}

int webdav_create_directory(fillet_app_struct *core)
{
    CURL *curl;
    char authentication_string[MAX_STR_SIZE];
    char cdn_url[MAX_STR_SIZE];
    int success;
    long http_response;
    
    if (!webdav_upload_thread_running) {
        //buffer_type = WEBDAV_CREATE
        return -1;
    }

    curl = curl_easy_init();

    snprintf(authentication_string, MAX_STR_SIZE-1, "%s:%s",
             core->cd->cdn_username,
             core->cd->cdn_password);
    snprintf(cdn_url, MAX_STR_SIZE-1, "%s/",
             core->cd->cdn_server);
    
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_USERPWD, authentication_string);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "MKCOL");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_URL, cdn_url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10); // configure?

    success = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);

    if (http_response >= 200 && http_response <= 299) {
        fprintf(stderr,"status: created directory on server: %s http code:%ld\n",
                core->cd->cdn_server,
                http_response);
    } else {
        fprintf(stderr,"error: unable to create directory on server: %s http code:%ld\n",
                core->cd->cdn_server,
                http_response);
        //signal
    }

    curl_easy_cleanup(curl);
    
    return 0;
}

void *webdav_upload_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    dataqueue_message_struct *msg;
    int ret;

    while (webdav_upload_thread_running) {
        msg = (dataqueue_message_struct*)dataqueue_take_back(core->webdav_queue);
        while (!msg && webdav_upload_thread_running) {
            usleep(100000);
            msg = (dataqueue_message_struct*)dataqueue_take_back(core->webdav_queue);
        }
        if (webdav_upload_thread_running) {
            int buffer_type = msg->buffer_type;

            buffer_type = msg->buffer_type;
            if (buffer_type == WEBDAV_DELETE) {
                //tbd
            }
            
            if (buffer_type == WEBDAV_UPLOAD) {
                ret = webdav_upload_file(core,
                                         msg->smallbuf);
                if (ret < 0) {
                    //we may have already signaled
                }
            }
            if (buffer_type == WEBDAV_CREATE) {
                ret = webdav_create_directory(core);
                if (ret < 0) {
                    //we may have already signaled
                }
            }
        }
        memory_return(core->fillet_msg_pool, msg);
        msg = NULL;
    }
        
    return NULL;
}
#endif // ENABLE_TRANSCODE

#if !defined(ENABLE_TRANSCODE)
void *webdav_upload_thread(void *context)
{
    return NULL;
}
#endif // !ENABLE_TRANSCODE
