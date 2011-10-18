/*

  meterec 
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009-2011 Fabrice Lebas
  
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
#include <string.h>
#include <sndfile.h>
#include <jack/jack.h>
#include "meterec.h"
#include "ports.h"

void process_port_register(jack_port_id_t port_id, int new, void *arg) {

	struct meterec_s *meterec ;
	unsigned int port, con;
	const char *port_name;
	jack_port_t *jack_port;
	
	if (!new) 
		return;
	
	meterec = (struct meterec_s *)arg ;
	
	if (!meterec->connect_ports)
		return;
	
	/* check if this is a port we would have liked to connect to */
	jack_port = jack_port_by_id(meterec->client, port_id);
	
	port_name = jack_port_name(jack_port);
	
	
	for (port=0; port<meterec->n_ports; port++)
		for (con=0; con<meterec->ports[port].n_cons; con++)
			if ( strcmp(port_name, meterec->ports[port].connections[con]) == 0 )
				connect_any_port(meterec, (char*) port_name, port);
	
	
}

void retreive_connected_ports(struct meterec_s *meterec) {
	
	unsigned int port;
	
	for (port=0; port<meterec->n_ports; port++) {
		free(meterec->ports[port].input_connected);
		free(meterec->ports[port].output_connected);
		
		meterec->ports[port].input_connected = jack_port_get_connections( meterec->ports[port].input );
		meterec->ports[port].output_connected = jack_port_get_connections( meterec->ports[port].output );
	}

};

void retreive_existing_ports(struct meterec_s *meterec) {
	
	if (meterec->all_input_ports)
		free(meterec->all_input_ports);
	if (meterec->all_output_ports)
		free(meterec->all_output_ports);
	
	meterec->all_input_ports = jack_get_ports(meterec->client, NULL, NULL, JackPortIsInput);
//	meterec->all_input_ports = jack_get_ports(meterec->client,"meterec", NULL, JackPortIsInput);
//	meterec->all_input_ports = jack_get_ports(meterec->client,"^[^m][^e][^t][^e][^r][^e][^c]:", NULL, JackPortIsInput);
	meterec->all_output_ports = jack_get_ports(meterec->client, NULL, NULL, JackPortIsOutput);
	
};

void filter_existing_ports(const char **port_list, const char *port_name_pattern ) {
	
	const char **tmp, **port;
	char *copy, *pattern;
	unsigned int len;
	
	if (!port_name_pattern) 
		return;
	
	if (!port_name_pattern[0]) 
		return;
	
	len = strlen(port_name_pattern);
	pattern = (char *) malloc(len + 2);
	strcpy(pattern, port_name_pattern);
	pattern[len]   = ':';
	pattern[len+1] = '\0';
	
	port = port_list;
	
	if (!port)
		return;
	
	while (*port) {
		
		len = strlen(*port);
		
		copy = (char *) malloc(len + 1);
		strcpy(copy, *port);
		
		if (strlen(pattern) < len) {
			copy[strlen(pattern)] = '\0';
		}
		
		if ( strcmp(copy, pattern) == 0) {
			tmp = port;
			while ( *port ) {
				*port = *(port+1);
				port++;
			}
			port = tmp - 1;
		}
		free(copy);
		
		port++;
	}
	
}

void create_input_port(struct meterec_s *meterec, unsigned int port) {
	
	char port_name[10] ;
	
	sprintf(port_name,"in_%d",port+1);
	
	fprintf(meterec->fd_log,"Creating input port '%s'.\n", port_name );
	
	if (!(meterec->ports[port].input = jack_port_register(meterec->client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
		fprintf(meterec->fd_log, "Cannot register input port '%s'.\n",port_name);
		exit_on_error("Cannot register input port");
	}
	
}

void create_output_port(struct meterec_s *meterec, unsigned int port) {
	
	char port_name[10] ;
	
	sprintf(port_name,"out_%d",port+1);
	
	fprintf(meterec->fd_log,"Creating output port '%s'.\n", port_name );
	
	if (!(meterec->ports[port].output = jack_port_register(meterec->client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(meterec->fd_log, "Cannot register output port '%s'.\n",port_name);
		exit_on_error("Cannot register output port");
	}
	
}

void create_monitor_port(struct meterec_s *meterec) {
	
	const char port_name[] = "monitor" ;
	
	fprintf(meterec->fd_log,"Creating output port '%s'.\n", port_name );
	
	if (!(meterec->monitor = jack_port_register(meterec->client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(meterec->fd_log, "Cannot register output port '%s'.\n",port_name);
		exit_on_error("Cannot register output port");
	}
	
}

void register_port_old(struct meterec_s *meterec, char *port_name, unsigned int port) {

	char *tmp = NULL;
	jack_port_t *jack_port;
	
	// Get the port we are connecting to
	jack_port = jack_port_by_name(meterec->client, port_name);
	
	// Check if port exists
	if (jack_port == NULL) {
		fprintf(meterec->fd_log, "Can't find port '%s' assuming this is part of port name.\n", port_name);
		
		if ( meterec->ports[port].name ) {
			tmp = (char *) malloc( strlen(meterec->ports[port].name) + 1 );
			strcpy(tmp, meterec->ports[port].name);
			free(meterec->ports[port].name);
			meterec->ports[port].name = (char *) malloc( strlen(port_name) + 1 + strlen(tmp) + 1);
			sprintf(meterec->ports[port].name, "%s %s",tmp, port_name);
			free(tmp);
		} else {
			meterec->ports[port].name = (char *) malloc( strlen(port_name) + 1 );
			strcpy(meterec->ports[port].name, port_name);
		}
		
		return;
		
	}
	else {
		meterec->ports[port].connections[meterec->ports[port].n_cons] = (char *) malloc( strlen(port_name) + 1 );
		strcpy(meterec->ports[port].connections[meterec->ports[port].n_cons], port_name);
		meterec->ports[port].n_cons += 1;
	}
}

void register_port(struct meterec_s *meterec, char *port_name, unsigned int port) {

	jack_port_t *jack_port;
	unsigned int con = meterec->ports[port].n_cons;
	
	// Get the port we are connecting to
	jack_port = jack_port_by_name(meterec->client, port_name);
	
	// Check if port exists
	if (jack_port == NULL) 
		fprintf(meterec->fd_log, "Can't find port '%s' will connect later if port becomes available.\n", port_name);
		
	meterec->ports[port].connections[con] = (char *) malloc( strlen(port_name) + 1 );
	strcpy(meterec->ports[port].connections[con], port_name);
	
	meterec->ports[port].n_cons += 1;
	
}

void deregister_port(struct meterec_s *meterec, char *port_name, unsigned int port) {

	unsigned int con, n_cons;
	
	n_cons = meterec->ports[port].n_cons;
	
	for (con=0; con<n_cons; con++) 
		if ( strcmp(meterec->ports[port].connections[con], port_name) == 0 ) 
			break;
	
	if (con < n_cons) {
		free(meterec->ports[port].connections[con]);
		n_cons--;
		
		for ( ; con<n_cons; con++) 
			meterec->ports[port].connections[con] = meterec->ports[port].connections[con+1];
		
		meterec->ports[port].connections[n_cons+1] = NULL;
		
		meterec->ports[port].n_cons = n_cons;
	}
	
}

void connect_all_ports(struct meterec_s *meterec) {

	unsigned int port, con;
	
	for (port=0; port<meterec->n_ports; port++)
		for (con=0; con<meterec->ports[port].n_cons; con++)
			connect_any_port(meterec, meterec->ports[port].connections[con], port);

}

/* Connect the chosen port to ours */
void connect_any_port(struct meterec_s *meterec, char *port_name, unsigned int port)
{
	jack_port_t *jack_port;
	int jack_flags;
	
	/* connect input port*/
	jack_port = jack_port_by_name(meterec->client, port_name);
	
	if (jack_port == NULL) 
		return;
		
	/* check port flags */
	jack_flags = jack_port_flags(jack_port);
	
	
	if ( jack_flags & JackPortIsInput ) {
		
		// Connect the port to our output port
		fprintf(meterec->fd_log,"Connecting '%s' to '%s'...\n", jack_port_name(meterec->ports[port].output), port_name);
		
		if (jack_port_connected_to(meterec->ports[port].output, port_name)) {
			fprintf(meterec->fd_log, "Ports '%s' and '%s' already connected\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));
			return;
		}
		
		if (jack_connect(meterec->client, jack_port_name(meterec->ports[port].output), port_name)) {
			fprintf(meterec->fd_log, "Cannot connect port '%s' to '%s'\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));
			exit_on_error("Cannot connect ports");
		}
		
	}
	
	if ( jack_flags & JackPortIsOutput ) {
		
		// Connect the port to our input port
		fprintf(meterec->fd_log,"Connecting '%s' to '%s'...\n", port_name, jack_port_name(meterec->ports[port].input));
		
		if (jack_port_connected_to(meterec->ports[port].input, port_name)) {
			fprintf(meterec->fd_log, "Ports '%s' and '%s' already connected\n", jack_port_name(meterec->ports[port].input), jack_port_name(jack_port));
			return;
		}
		
		if (jack_connect(meterec->client, port_name, jack_port_name(meterec->ports[port].input))) {
			fprintf(meterec->fd_log, "Cannot connect port '%s' to '%s'\n", jack_port_name(jack_port), jack_port_name(meterec->ports[port].input));
			exit_on_error("Cannot connect ports");
		}
	
	}
	
}


void disconnect_any_port(struct meterec_s *meterec, char *port_name, unsigned int port)
{
	jack_port_t *jack_port;
	int jack_flags;
	
	/* connect input port*/
	jack_port = jack_port_by_name(meterec->client, port_name);
	
	if (jack_port == NULL) 
		return;
		
	/* check port flags */
	jack_flags = jack_port_flags(jack_port);
	
	if ( jack_flags & JackPortIsInput ) {
		
		// Disconnect the port from our output port
		fprintf(meterec->fd_log,"Disconnecting '%s' from '%s'...\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));

		if (!jack_port_connected_to(meterec->ports[port].output, port_name)) {
			fprintf(meterec->fd_log, "Ports '%s' and '%s' already disconnected\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));
			return;
		}
		
		if (jack_disconnect(meterec->client, jack_port_name(meterec->ports[port].output), jack_port_name(jack_port))) {
			fprintf(meterec->fd_log, "Cannot disconnect ports '%s' and '%s'\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));
			exit_on_error("Cannot disconnect ports");
		}
		
	}
	
	if ( jack_flags & JackPortIsOutput ) {
		
		// Connect the port to our input port
		fprintf(meterec->fd_log,"Disconnecting '%s' from '%s'...\n", jack_port_name(jack_port), jack_port_name(meterec->ports[port].input));

		if (!jack_port_connected_to(meterec->ports[port].input, port_name)) {
			fprintf(meterec->fd_log, "Ports '%s' and '%s' already disconnected\n", jack_port_name(meterec->ports[port].input), jack_port_name(jack_port));
			return;
		}
		
		if (jack_disconnect(meterec->client, jack_port_name(jack_port), jack_port_name(meterec->ports[port].input))) {
			fprintf(meterec->fd_log, "Cannot disconnect ports '%s' and '%s'\n", jack_port_name(jack_port), jack_port_name(meterec->ports[port].input));
			exit_on_error("Cannot disconnect ports");
		}
	
	}
	
}

void register_connect_port(struct meterec_s *meterec, char *port_name, unsigned int port)
{
	register_port(meterec, port_name, port);
	connect_any_port(meterec, port_name, port);
}

void deregister_disconnect_port(struct meterec_s *meterec, char *port_name, unsigned int port)
{
	deregister_port(meterec, port_name, port);
	disconnect_any_port(meterec, port_name, port);

}

char* port_rename(struct meterec_s *meterec, unsigned int port) 
{
	char *new;
	
	new = (char *) malloc( MAX_NAME_LEN + 1 );
	
	if (meterec->ports[port].name) 
		free(meterec->ports[port].name);
	
	meterec->ports[port].name = new;
	
	*new     = '_';
	*(new+1) = '\0';
	
	return new;
}


