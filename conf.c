/*

  meterec
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009-2013 Fabrice Lebas
  
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

#include <curses.h>
#include <sndfile.h>
#include <jack/jack.h>
#include <libconfig.h>
 
#include "position.h"
#include "meterec.h"
#include "ports.h"


/*
** Legacy configuration files scheme ( .session.sess / session.conf )
*/

void parse_port_con(struct meterec_s *meterec, FILE *fd_conf, unsigned int port) {

	char line[1000];
	char label[100];
	char port_name[100];
	unsigned int i, u;
	
	i = fscanf(fd_conf,"%s%[^\r\n]%*[\r\n ]",label, line);
	i = 0;
	while ( sscanf(line+i,"%s%u",port_name,&u ) ) {
		
		register_port_old(meterec, port_name, port);
		connect_any_port(meterec, port_name, port);
		
		i+=u;
		
		while(line[i] == ' ')
			i++;
		
		if (line[i] == '\0')
			break;
	
	}
	
}

void parse_time_index(struct meterec_s *meterec, FILE *fd_conf, unsigned int index) {
	
	struct time_s time;
	unsigned int u;
	
	u = fscanf(fd_conf, "%u:%u:%u.%u%*s", &time.h, &time.m, &time.s, &time.ms);
	
	if (u==4) { 
		time.rate = jack_get_sample_rate(meterec->client);
		time_frm(&time);
		
		meterec->seek_index[index] = time.frm;
	}
	else {
		/* consume this line */
		u = fscanf(fd_conf, "%*s");
	}
	
}

void load_setup(struct meterec_s *meterec) {
	
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
		
			/* allocate memory for this port */
			meterec->ports[port].read_disk_buffer = calloc(DBUF_SIZE, sizeof(float));
			meterec->ports[port].write_disk_buffer = calloc(DBUF_SIZE, sizeof(float));
			
			/* create input ports */
			create_input_port(meterec, port);
			create_output_port(meterec, port);
			
			/* connect to other ports */
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

void load_session(struct meterec_s *meterec) {
	
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

/*
** New configuration file scheme ( session.mrec )
*/


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
		
		fprintf(fd_conf, "record=\"%s\"; ", rec);
		
		fprintf(fd_conf, "mute=%s ", meterec->ports[port].mute?"true; ":"false;");
		
		fprintf(fd_conf, "thru=%s ", meterec->ports[port].thru?"true; ":"false;");
		
		fprintf(fd_conf,"connections=(");
		for (con=0; con< meterec->ports[port].n_cons; con++) {
			if (con)
				fprintf(fd_conf, ",");
			
			fprintf(fd_conf,"\"%s\"", meterec->ports[port].connections[con]);
		}
		fprintf(fd_conf,"); ");
		
		fprintf(fd_conf,"name=\"%s\"; }", meterec->ports[port].name?meterec->ports[port].name:"");
		
	}
	fprintf(fd_conf, "\n);\n\n");
	
	fprintf(fd_conf, "takes=\n(\n");
	for (take=1; take<meterec->n_takes+1; take++) {
		fprintf(fd_conf, "  {");
		fprintf(fd_conf, " offset=%-10d;",meterec->takes[take].offset);
		fprintf(fd_conf, " name=\"%s\";",meterec->takes[take].name?meterec->takes[take].name:"");
		fprintf(fd_conf, " }");
		if (take < meterec->n_takes)
			fprintf(fd_conf, ",\n");
	}
	fprintf(fd_conf, "\n);\n\n");
	
	
	fprintf(fd_conf, "indexes=\n{\n");
	for (index=0; index<MAX_INDEX; index++) {
		time.frm = meterec->seek_index[index] ;
		if ( time.frm != (unsigned int)(-1) ) {
			time_hms(&time);
			time_sprint(&time, time_str);
			fprintf(fd_conf,"  f%d=\"%s\";\n", index+1, time_str);
		}
	}
	fprintf(fd_conf, "};\n\n");
	
	if (meterec->jack.sample_rate) {
		fprintf(fd_conf, "jack=\n{\n");
		fprintf(fd_conf, "  sample_rate=%d;\n", meterec->jack.sample_rate);
		fprintf(fd_conf, "};\n\n");
	}
	
	fprintf(fd_conf, "version=1;\n\n");	
	
	fclose(fd_conf);
	
	fprintf(meterec->fd_log, "Saved configuration to '%s'.\n", file);
}

int parse_record(const char *record) {
	
	if (strcmp(record, "---") == 0) 
		return OFF;
	if (strcmp(record, "rec") == 0) 
		return REC;
	if (strcmp(record, "dub") == 0) 
		return DUB;
	if (strcmp(record, "ovr") == 0) 
		return OVR;
	
	return OFF;
	
}

void parse_time(struct meterec_s *meterec, unsigned int index, const char *time_str) {
	
	struct time_s time;
	
	if (sscanf(time_str, "%u:%u:%u.%u%*s", &time.h, &time.m, &time.s, &time.ms) != 4)
		return;
	
	time.rate = jack_get_sample_rate(meterec->client);
	time_frm(&time);
	
	meterec->seek_index[index] = time.frm;
	
}


unsigned int parse_takes(struct meterec_s *meterec, unsigned int port, const char *takes) {

	unsigned int take = 1, track;
	
	while ( *takes ) {
		switch (*takes) {
			case '-' :
				break;
			case 'l' :
				meterec->takes[take].port_has_lock[port] = 1 ;
				break;
			case 'L' :
				meterec->takes[take].port_has_lock[port] = 1 ;
			case 'X' :
				track = meterec->takes[take].ntrack ;
				meterec->takes[take].track_port_map[track] = port ;
				meterec->takes[take].port_has_track[port] = 1 ;
				meterec->takes[take].ntrack++;
				meterec->ports[port].playback_take = take ;
			default :
				break;
		}
		
		takes ++;
		take ++;
	}
	return take;
}


void load_conf(struct meterec_s *meterec) {
	
	unsigned int port=0, con=0, index=0, take=0;
	config_t cfg, *cf;
	const config_setting_t *take_list, *take_group, *port_list, *port_group, *connection_list, *index_group, *jack_group ;
	unsigned int take_list_len, port_list_len, connection_list_len;
	const char *takes, *record, *name, *port_name, *time;
	int mute=OFF, thru=OFF;
	long sample_rate, take_offset;
	char fn[4];
				
	fprintf(meterec->fd_log,"Loading '%s'\n", meterec->conf_file);
	
	cf = &cfg;
	config_init(cf);

	if (!config_read_file(cf, meterec->conf_file)) {
		fprintf(meterec->fd_log, "Error in '%s' %d - %s\n",
			meterec->conf_file,
			config_error_line(cf),
			config_error_text(cf));
		
		config_destroy(cf);
		
		exit_on_error("Cannot parse configuration file.");
	}
	
	index_group = config_lookup(cf, "indexes");
	
	if (index_group) {
		for (index=0; index<12; index++) {
			sprintf(fn, "f%d", index+1);
			if (config_setting_lookup_string(index_group, fn, &time))
				parse_time(meterec, index, time);
		}
	}
	
	jack_group = config_lookup(cf, "jack");
	
	if (jack_group) 
		if (config_setting_lookup_int(jack_group, "sample_rate", &sample_rate))
			meterec->jack.sample_rate = (unsigned int)sample_rate;
	
	take_list = config_lookup(cf, "takes");
	if (take_list) {
		take_list_len = config_setting_length(take_list);
		
		for (take=0; take<take_list_len; take++) {
			take_group = config_setting_get_elem(take_list, take);
			
			if (take_group) {
				
				if (config_setting_lookup_string(take_group, "name", &name)) {
					meterec->takes[take+1].name = (char *) malloc( strlen(name) + 1 ); 
					strcpy(meterec->takes[take+1].name, name); 
				}
				
				if (config_setting_lookup_int(take_group, "offset", &take_offset)) {
					meterec->takes[take+1].offset = (unsigned int)take_offset;
				}
			}
			
		}
	}
	
	port_list = config_lookup(cf, "ports");
	if (port_list) {
		port_list_len = config_setting_length(port_list);
		
		for (port=0; port<port_list_len; port++) {
			port_group = config_setting_get_elem(port_list, port);
			
			if (port_group) {
				
				/* allocate memory for this port */
				meterec->ports[port].read_disk_buffer = calloc(DBUF_SIZE, sizeof(float));
				meterec->ports[port].write_disk_buffer = calloc(DBUF_SIZE, sizeof(float));
				
				/* create input ports */
				create_input_port(meterec, port);
				create_output_port(meterec, port);
						
				if (config_setting_lookup_string(port_group, "takes", &takes))
					meterec->n_takes = parse_takes(meterec, port, takes);
				
				if (config_setting_lookup_bool(port_group, "mute", &mute))
					meterec->ports[port].mute = mute;
				
				if (config_setting_lookup_bool(port_group, "thru", &thru))
					meterec->ports[port].thru = thru;
				
				if (config_setting_lookup_string(port_group, "record", &record))
					meterec->ports[port].record = parse_record(record);
				
				if (config_setting_lookup_string(port_group, "name", &name)) {
					meterec->ports[port].name = (char *) malloc( strlen(name) + 1 ); 
					strcpy(meterec->ports[port].name, name); 
				}
				
				connection_list = config_setting_get_member(port_group, "connections");
				if (connection_list) {
					connection_list_len = config_setting_length(connection_list); 
					
					for (con=0; con<connection_list_len; con++) {
						port_name = config_setting_get_string_elem(connection_list, con);
						
						if (port_name) {
							meterec->ports[port].connections[con] = (char *) malloc( strlen(port_name) + 1 );
							strcpy(meterec->ports[port].connections[con], port_name);
							
							/* store connection info */
							register_port(meterec, (char *)port_name, port);
						}
					}
					meterec->ports[port].n_cons = con;
				}
			}
		}
	
	}
	
	config_destroy(cf);
		
	meterec->n_ports = port ;
	meterec->n_takes -= 1;
	
}

