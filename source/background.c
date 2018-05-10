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
#include "background.h"

#define MAX_CONNECTIONS    8
#define MAX_REQUEST_SIZE   65535
#define MAX_RESPONSE_SIZE  MAX_REQUEST_SIZE

static int load_kvp_config(fillet_app_struct *core)
{
    struct stat sb;
    FILE *kvp;
    char kvp_filename[MAX_STR_SIZE];
    char local_dir[MAX_STR_SIZE];
    int source_streams = 0;
    int enable_hls_ts = 0;
    int enable_hls_fmp4 = 0;
    int enable_dash = 0;
    int ip0, ip1, ip2, ip3;
    int port;
    char interface[MAX_STR_SIZE];
    int segment_length = DEFAULT_SEGMENT_LENGTH;
    int window_size = DEFAULT_WINDOW_SIZE;
    int rollover_size = MAX_ROLLOVER_SIZE;    

    if (!core) {
	return -1;
    }

    snprintf(local_dir,MAX_STR_SIZE-1,"/opt/fillet");
    if (stat(local_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
	// placeholder
    } else {
	syslog(LOG_ERR,"SESSION:%d (CONFIG) ERROR: UNABLE TO FIND /opt/fillet CONFIGURATION DIRECTORY\n", core->session_id);
	return -1;
    }
    snprintf(kvp_filename,MAX_STR_SIZE-1,"/opt/fillet/session%d.config", core->session_id);

    syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: LOADING CONFIGURATION: %s\n", core->session_id, kvp_filename);    

    kvp = fopen(kvp_filename,"r");
    if (!kvp) {
	syslog(LOG_ERR,"SESSION:%d (CONFIG) ERROR: UNABLE TO OPEN /opt/fillet/%s\n", core->session_id, kvp_filename);
	return -1;
    }
    syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: READING /opt/fillet/%s\n", core->session_id, kvp_filename);
    
    while (!feof(kvp)) {
	char kvpdata[MAX_STR_SIZE];
	char *pkvpdata;
	int vals;

	pkvpdata = (char*)fgets(kvpdata,MAX_STR_SIZE-1,kvp);
	if (pkvpdata) {
	    //streams=X
	    //source=X  ip:port:interface
	    //source=X  ip:port:interface
	    //source=X  ip:port:interface
	    //hls_ts=yes
	    //hls_fmp4=yes
	    //dash=yes
	    //segment_length=5
	    //window_size=5
	    //rollover=128
	    if (strncmp(pkvpdata,"streams=",8) == 0) {
		vals = sscanf(pkvpdata,"streams=%d",
			      &source_streams);
		syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: SOURCE STREAMS CONFIGURED: %d\n",
		       core->session_id, source_streams);
		if (source_streams == 0) {
		    syslog(LOG_ERR,"SESSION:%d (CONFIG) ERROR: INVALID NUMBER OF SOURCE STREAMS: %d\n",
			   core->session_id, source_streams);
		}	
	    }
	    if (strncmp(pkvpdata,"source=",7) == 0) {
		vals = sscanf(pkvpdata,"source=%d.%d.%d.%d:%d:%s",
			      &ip0,&ip1,&ip2,&ip3,
			      &port,
			      (char*)&interface);
		// format - ip:port:interface
	    }
	    if (strncmp(pkvpdata,"hls_ts=yes",10) == 0) {
		enable_hls_ts = 1;
		syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: HLS TS ENABLED\n", core->session_id);
	    }
	    if (strncmp(pkvpdata,"hls_fmp4=yes",12) == 0) {
		enable_hls_fmp4 = 1;
		syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: HLS fMP4 ENABLED\n", core->session_id);
	    }
	    if (strncmp(pkvpdata,"dash=yes",8) == 0) {
		enable_dash = 1;
		syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: DASH fMP4 ENABLED\n", core->session_id);
	    }
	    if (strncmp(pkvpdata,"segment_length=",15) == 0) {
		vals = sscanf(pkvpdata,"segment_length=%d", &segment_length);
		if (segment_length > MAX_SEGMENT_LENGTH || segment_length < MIN_SEGMENT_LENGTH) {
		    segment_length = DEFAULT_SEGMENT_LENGTH;
		}	    
	    }
	    if (strncmp(pkvpdata,"window_size=",12) == 0) {
		vals = sscanf(pkvpdata,"window_size=%d", &window_size);
		if (window_size > MAX_WINDOW_SIZE || window_size < MIN_WINDOW_SIZE) {
		    window_size = DEFAULT_WINDOW_SIZE;
		}
	    }
	    if (strncmp(pkvpdata,"rollover=",9) == 0) {
		vals = sscanf(pkvpdata,"rollover=%d", &rollover_size);
		if (rollover_size > MAX_ROLLOVER_SIZE || rollover_size < MIN_ROLLOVER_SIZE) {
		    rollover_size = MAX_ROLLOVER_SIZE;
		}	
	    }
	}
    }

    core->cd->window_size = window_size;
    core->cd->segment_length = segment_length;
    core->cd->rollover_size = rollover_size;
    core->cd->active_sources = source_streams;
    core->cd->identity = (core->session_id+1)*1000;
    core->cd->enable_ts_output = enable_hls_ts;
    core->cd->enable_fmp4_output = enable_hls_fmp4 || enable_dash;
    // placeholder

    if (!enable_hls_ts && !enable_dash && !enable_hls_fmp4) {
	syslog(LOG_INFO,"SESSION:%d (CONFIG) ERROR: NO OUTPUT MODE ENABLED\n", core->session_id);
    }
    
    syslog(LOG_INFO,"SESSION:%d (CONFIG) STATUS: DONE READING /opt/fillet/%s\n", core->session_id, kvp_filename);    

    fclose(kvp);

    return 0;
}

void *server_thread(void *context)
{
    fillet_app_struct *core = (fillet_app_struct*)context;
    int server = -1;
    int port = 50000+(core->session_id*10);
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
	    server = socket(AF_INET, SOCK_STREAM, 0);
	    if (server > 0) {
		struct sockaddr_in commserver;
		uint8_t yesflag = 1;
		
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
		vals = sscanf((char*)request,"%s %s HTTP/%s", method, url, httpver);
		if (vals != 3) {
		    syslog(LOG_WARNING,"SESSION:%d (RESTFUL) WARNING: RECEIVED MALFORMED DATA\n",
			   core->session_id);
		    continue;
		}
		syslog(LOG_INFO,"SESSION:%d (RESTFUL) STATUS: RECEIVED HTTP REQUEST (METHOD:%s,URL:%s,VER:%s)\n",
		       core->session_id, method, url, httpver);

		if (strncmp(method,"GET",3)) {		    
		    // "GET" methods
		    char *status_response = "{\"warning\":[\"inactive\"]}";
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
		    client = 0;
		    continue;
		} else if (strncmp(method,"POST",4)) {
		    // "POST" methods
		    if (strncmp(url,"/start",6)) {  // puts into run state
		    }
		    if (strncmp(url,"/stop",5)) {
		    }
		    if (strncmp(url,"/restart",8)) {
		    }
		    if (strncmp(url,"/respawn",8)) {
		    }
		    //placeholder
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
		    client = 0;
		    syslog(LOG_WARNING,"SESSION:%d (RESTFUL) WARNING: RECEIVED INVALID REQUEST\n",
			   core->session_id);		    
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
		    client = 0;
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


int launch_new_fillet(fillet_app_struct *core, int new_session)
{
    pid_t new_pid_id;
    int err;

    new_pid_id = fork();

    if (new_pid_id < 0) {
	// failed as a parent and child
	syslog(LOG_ERR,"FATAL ERROR: FILLET FAILED AS A PARENT AND CHILD!\n");
	return -1;
    } else if (new_pid_id == 0) {
	// child
	core->session_id = (new_session+1);
	err = load_kvp_config(core);
	if (err < 0) {
	    syslog(LOG_ERR,"FATAL ERROR: FILLET FAILED TO CREATE CHILD PROCESS!\n");
	    // placeholder	    
	}
    } else {
	// parent
	// save the process identifier
    }

    return new_pid_id;
}



