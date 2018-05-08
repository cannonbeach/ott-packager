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

#include "fillet.h"
#include "background.h"

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
	fprintf(stderr,"STATUS: /opt/fillet configuration directory exists\n");
    } else {
	fprintf(stderr,"STATUS: /opt/fillet configuration directory does not exist!\n");
	return -1;
    }
    snprintf(kvp_filename,MAX_STR_SIZE-1,"/opt/fillet/session%d.config", core->session_id);

    kvp = fopen(kvp_filename,"r");
    if (!kvp) {
	fprintf(stderr,"STATUS: Unable to open /opt/fillet/%s\n", kvp_filename);
	return -1;
    }
    fprintf(stderr,"STATUS: Reading /opt/fillet/%s\n", kvp_filename);
    while (!feof(kvp)) {
	char kvpdata[MAX_STR_SIZE];
	char *pkvpdata;

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
		source_streams = 0;
	    }
	    if (strncmp(pkvpdata,"source=",7) == 0) {
		int vals;
		vals = sscanf(pkvpdata,"%d.%d.%d.%d:%d:%s",
			      &ip0,&ip1,&ip2,&ip3,
			      &port,
			      (char*)&interface);
		// format - ip:port:interface
	    }
	    if (strncmp(pkvpdata,"hls_ts=yes",10) == 0) {
		enable_hls_ts = 1;
	    }
	    if (strncmp(pkvpdata,"hls_fmp4=yes",12) == 0) {
		enable_hls_fmp4 = 1;
	    }
	    if (strncmp(pkvpdata,"dash=yes",8) == 0) {
		enable_dash = 1;
	    }
	    if (strncmp(pkvpdata,"segment_length=",15) == 0) {
	    }
	    if (strncmp(pkvpdata,"window_size=",12) == 0) {
	    }
	    if (strncmp(pkvpdata,"rollover=",9) == 0) {
	    }
	}
    }
    fprintf(stderr,"STATUS: Done reading /opt/fillet/%s\n", kvp_filename);

    fclose(kvp);

    return 0;
}


int launch_new_fillet(fillet_app_struct *core, int new_session)
{
    pid_t new_pid_id;
    int err;

    new_pid_id = fork();

    if (new_pid_id < 0) {
	// failed as a parent and child
	return -1;
    } else if (new_pid_id == 0) {
	// child
	core->session_id = (new_session+1);
	err = load_kvp_config(core);
	if (err < 0) {
	    // debug
	    // return/quit?
	}
    } else {
	// parent
	// save the process identifier
    }

    return new_pid_id;
}
