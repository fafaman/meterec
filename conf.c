/*

  meterec
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009 2010 2011 Fabrice Lebas
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sndfile.h>
#include <jack/jack.h>
#include <libconfig.h>
 
#include "meterec.h"

void parse_port_con(struct meterec_s *meterec, FILE *fd_conf, unsigned int port)
{

  char line[1000];
  char label[100];
  char port_name[100];
  unsigned int i, u;
  
  i = fscanf(fd_conf,"%s%[^\r\n]%*[\r\n ]",label, line);
  
  i = 0;
  while ( sscanf(line+i,"%s%n",port_name,&u ) ) {

    connect_any_port(meterec->client, port_name, port);

    i+=u;
    
    while(line[i] == ' ')
     i++;
    
    if (line[i] == '\0')
      break;
        
  }
  
}

void parse_time_index(struct meterec_s *meterec, FILE *fd_conf, unsigned int index)
{
  struct time_s time;
  unsigned int u;
  
  u = fscanf(fd_conf, "%u:%u:%u.%u%*s", &time.h, &time.m, &time.s, &time.ms);
  
  if (u==4) { 
    time.rate = jack_get_sample_rate(meterec->client);
    time_frm(&time);
  
    meterec->seek.index[index] = time.frm;
  }
  else {
    /* consume this line */
    u = fscanf(fd_conf, "%*s");
  }
  
}

void load_setup(struct meterec_s *meterec)
{

  FILE *fd_conf;
  char buf[2];
  unsigned int take=1, port=0, index=0;
  
  buf[1] = 0;
  
  if ( (fd_conf = fopen(meterec->setup_file,"r")) == NULL ) {
    fprintf(meterec->fd_log,"could not open '%s' for reading\n", meterec->setup_file);
    exit_on_error("Cannot open setup file for reading.");
  }
  
  fprintf(meterec->fd_log,"Loading '%s'\n", meterec->setup_file);
  
  while ( fread(buf, sizeof(char), 1, fd_conf) ) {
    
    if (*buf == 'L' || *buf == 'l') {
      fprintf(meterec->fd_log,"Playback LOCK on Port %d take %d\n", port+1,take);
      meterec->takes[take].port_has_lock[port] = 1 ;
    }
    
    
    /* new port / new take */
    
    if (*buf == '|') {
    
      // allocate memory for this port
      meterec->ports[port].read_disk_buffer = calloc(DISK_SIZE, sizeof(float));
      meterec->ports[port].write_disk_buffer = calloc(DISK_SIZE, sizeof(float));
      
      // create input ports
      create_input_port ( meterec->client, port );
      create_output_port ( meterec->client, port );
      
      // connect to other ports
      parse_port_con(meterec, fd_conf, port);
      
      port++;
    }
    
    switch (*buf) {
	  case '~' :
	    meterec->ports[port].mute = 1;
	  case '=' :
	take=1;
	    break;

	  case 'r' :
	    meterec->ports[port].mute = 1;
	  case 'R' :
	meterec->ports[port].record = REC;
	meterec->n_tracks++;
	take=1;
	    break;
	
	  case 'd' :
	    meterec->ports[port].mute = 1;
	  case 'D' :
	meterec->ports[port].record = DUB;
	meterec->n_tracks++;
	take=1;
	    break;
	
	  case 'o' :
	    meterec->ports[port].mute = 1;
	  case 'O' :
	meterec->ports[port].record = OVR;
	meterec->n_tracks++;
	take=1;
	    break;
	
	  case '>' :
	parse_time_index(meterec, fd_conf, index);
	index++;
	    break;
		
	  default :
	    take++;
	}
        
  }

  fclose(fd_conf);
  
  meterec->n_ports = port ;
  
}

void load_session(struct meterec_s *meterec)
{

  FILE *fd_conf;
  char buf[2];
  unsigned int take=1, port=0, track=0;
  
  buf[1] = 0;
  
  if ( (fd_conf = fopen(meterec->session_file,"r")) == NULL ) {
    fprintf(meterec->fd_log,"could not open '%s' for reading\n", meterec->session_file);
     exit_on_error("Cannot open session file for reading.");
 }

  fprintf(meterec->fd_log,"Loading '%s'\n", meterec->session_file);

  while ( fread(buf, sizeof(char), 1, fd_conf) ) {
    
    /* content for a given port/take pair */
    if (*buf == 'X') {
      
      track = meterec->takes[take].ntrack ;
      
      meterec->takes[take].track_port_map[track] = port ;
      meterec->takes[take].port_has_track[port] = 1 ;
      meterec->takes[take].ntrack++;
      
      meterec->ports[port].playback_take = take ;
    
    }

    /* end of description of all takes for a port detected on trailing 'pipe' */
    if (*buf == '|') {
      port++;
      meterec->n_takes = take - 1;
    }
    
    /* increment take unless beginning of line is detected */
    if (*buf == '=') 
      take=1;
    else 
      take++; 
    
            
  }
    
  fclose(fd_conf);
  
  if (port>meterec->n_ports) {
    fprintf(meterec->fd_log,"'%s' contains more ports (%d) than defined in .conf file (%d)\n", meterec->session_file, port, meterec->n_ports);
    exit_on_error("Session and setup not consistent");
  }
  
}


void session_tail(struct meterec_s *meterec, FILE * fd_conf) 
{
  unsigned int ntakes, take, step=1, i;
  char *spaces;
  
  ntakes = meterec->n_takes+1;

  while (step < ntakes+1) {

    spaces = (char *) malloc( step );
    
    for (i=0; i<step; i++) {
      spaces[i]= ' ' ;
    }
    spaces[step-1]= '\0';
    
    for (take=0; take<ntakes+1; take=take+step) {
      fprintf(fd_conf,"%d%s",(take/step)%10, spaces);
    }
    fprintf(fd_conf,"\n");
    step = step * 10;
    
    free(spaces);
    
  }
  
  fprintf(fd_conf,"\n");

}

void save_conf(struct meterec_s *meterec) {
	
	char *file; 
	FILE *fd_conf;
	unsigned int take, port, con, index;
	struct time_s time;
	char *rec ;
	char time_str[14] ;
	
	file = meterec->conf_file;
	
	time.rate = jack_get_sample_rate(meterec->client);
	
	if ( (fd_conf = fopen(file,"w")) == NULL ) {
		fprintf(meterec->fd_log,"could not open '%s' for writing\n", file );
		exit_on_error("Cannot open configuration file for writing.");
	}
	
	fprintf(fd_conf, "ports=\n(\n");
			
	for (port=0; port<meterec->n_ports; port++) {
		
		if (port)
			fprintf(fd_conf, ",\n");
		
		fprintf(fd_conf, "  { takes=\"");
		for (take=1; take<meterec->n_takes+1; take++) 
			if ( meterec->takes[take].port_has_lock[port] )
				fprintf(fd_conf, meterec->takes[take].port_has_track[port]?"L":"l" );
			else 
				fprintf(fd_conf, meterec->takes[take].port_has_track[port]?"X":"-" );
			
		fprintf(fd_conf, "\"; ");
		
		if (meterec->ports[port].record==REC)
			rec = "rec";
		else if (meterec->ports[port].record==DUB)
			rec = "dub";
		else if (meterec->ports[port].record==OVR)
			rec = "ovr";
		else
			rec = "---";
		
		fprintf(fd_conf, "record=\"%s\"; mute=%s; ", rec, meterec->ports[port].mute?"true":"false");
		
		fprintf(fd_conf,"connections=(");
		for (con=0; con< meterec->ports[port].portmap; con++) {
			if (con)
				fprintf(fd_conf, ",");
			
			fprintf(fd_conf,"\"%s\"", meterec->ports[port].connections[con]);
		}
		fprintf(fd_conf,"); ");
		
		fprintf(fd_conf,"name=\"%s\"; }", meterec->ports[port].name?meterec->ports[port].name:"");
		
	}
	
	fprintf(fd_conf, "\n);\n\n");
	
	
	fprintf(fd_conf, "indexes=\n{\n");
	for (index=0; index<MAX_INDEX; index++) {
		time.frm = meterec->seek.index[index] ;
		if ( time.frm != (unsigned int)(-1) ) {
			time_hms(&time);
			time_sprint(&time, time_str);
			fprintf(fd_conf,"  f%d=\"%s\";\n", index+1, time_str);
		}
	}
	fprintf(fd_conf, "};\n\n");
	  
	fclose(fd_conf);
	  
}

void load_conf(struct meterec_s *meterec) {
	
	char *file; 
	char buf[2];
	unsigned int n, take=1, port=0, con=0, track=0, index=0;
	config_t cfg, *cf;
	const config_setting_t *port_list, *port_group, *connection_list, connection ;
	unsigned int port_list_len, connection_list_len;
	const char *takes, *record, *name;
	int mute;
			
	buf[1] = 0;
	
	file = meterec->conf_file;
	
	fprintf(meterec->fd_log,"Loading '%s'\n", file);
	
	cf = &cfg;
	config_init(cf);

	if ( config_read_file(cf, file) == CONFIG_FALSE ) {
		fprintf(meterec->fd_log, "error in config file %s line %d - %s\n",
			config_error_file(cf),
			config_error_line(cf),
			config_error_text(cf));
		
		config_destroy(cf);
		
		exit_on_error("Cannot parse setup file.");
	}
	
	port_list = config_lookup(cf, "ports");
	port_list_len = config_setting_length(port_list);
	
	for (port=0; port<port_list_len; port++) {
		port_group = config_setting_get_elem(port_list, port);
		
		config_setting_lookup_bool(port_group, "mute", &mute);
		config_setting_lookup_string(port_group, "record", &record);
		config_setting_lookup_string(port_group, "name", &name);
		config_setting_lookup_string(port_group, "takes", &takes);
		
		fprintf(meterec->fd_log,"Port %d %s %s %s\n", port, mute?"muted":"unmuted", record, name);
		
		connection_list = config_setting_get_member(port_group, "connections");
		connection_list_len = config_setting_length(connection_list); 
		
		for (con=0; con<connection_list_len; con++) {
			
			meterec->ports[port].connections[con] = config_setting_get_string_elem(connection_list, con);
		}
		meterec->ports[port].portmap = con;
		
	}
	
	/*
	while ( fread(buf, sizeof(char), 1, fd_conf) ) {
		switch (*buf) {
			case '~' :
				meterec->ports[port].mute = 1;
			case '=' :
				take=1;
				break;
			
			case 'r' :
				meterec->ports[port].mute = 1;
			case 'R' :
				meterec->ports[port].record = REC;
				meterec->n_tracks++;
				take=1;
				break;
			
			case 'd' :
				meterec->ports[port].mute = 1;
			case 'D' :
				meterec->ports[port].record = DUB;
				meterec->n_tracks++;
				take=1;
				break;
			
			case 'o' :
				meterec->ports[port].mute = 1;
			case 'O' :
				meterec->ports[port].record = OVR;
				meterec->n_tracks++;
				take=1;
				break;
			
			case '>' :
				parse_time_index(meterec, fd_conf, index);
				index++;
				break;
			
			case 'l' :
				meterec->takes[take].port_has_lock[port] = 1 ;
				take++;
				break;
			case 'L' :
				meterec->takes[take].port_has_lock[port] = 1 ;
			case 'X' :
				track = meterec->takes[take].ntrack ;
				meterec->takes[take].track_port_map[track] = port ;
				meterec->takes[take].port_has_track[port] = 1 ;
				meterec->takes[take].ntrack++;
				meterec->ports[port].playback_take = take ;
			case '-' :
				take++;
				break;
				
			case '|' :
			
				// allocate memory for this port
				meterec->ports[port].read_disk_buffer = calloc(DISK_SIZE, sizeof(float));
				meterec->ports[port].write_disk_buffer = calloc(DISK_SIZE, sizeof(float));
				
				// create input ports
				create_input_port ( meterec->client, port );
				create_output_port ( meterec->client, port );
				
				// connect to other ports
				parse_port_con(meterec, fd_conf, port);
				
				port++;
				break;
			
			default :
				break;
		}
	}
	*/
	
	meterec->n_ports = port ;
	meterec->n_takes = take - 1;
	
}

