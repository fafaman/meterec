/*

  meterec.c 
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009 2010 Fabrice Lebas
  
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
#include <dirent.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <sndfile.h>
#include <jack/jack.h>
#include <getopt.h>
#include <curses.h>

#include "config.h"
#include "meterec.h"
#include "display.h"
#include "disk.h"
#include "conf.h"



WINDOW * mainwin;

int edit_mode=0, display_names=1;
unsigned int x_pos=0, y_pos=0;
int running = 1;

static unsigned long playhead = 0 ;

char *session = "meterec";
char *jackname = "meterec" ;
#if defined(HAVE_W64)
char *output_ext = "w64" ;
#else
char *output_ext = "wav" ;
#endif

pthread_t wr_dt, rd_dt, kb_dt ;

struct meterec_s * meterec ;

/******************************************************************************
** UTILs
*/

void cleanup_jack(void) {
	
	const char **all_ports;
	unsigned int i, port;
	
	if (meterec->client == NULL)
		return;
	
	for (port = 0; port < meterec->n_ports; port++) {
		
		if (meterec->ports[port].input != NULL ) {
			
			all_ports = jack_port_get_all_connections(meterec->client, meterec->ports[port].input);
			
			for (i=0; all_ports && all_ports[i]; i++) {
				fprintf(meterec->fd_log,"Disconnecting input port '%s' from '%s'.\n", jack_port_name(meterec->ports[port].input), all_ports[i] );
				jack_disconnect(meterec->client, all_ports[i], jack_port_name(meterec->ports[port].input));
			}
			
			free(all_ports);
		}
		if (meterec->ports[port].output != NULL ) {
			
			all_ports = jack_port_get_all_connections(meterec->client, meterec->ports[port].output);
			
			for (i=0; all_ports && all_ports[i]; i++) {
				fprintf(meterec->fd_log,"Disconnecting output port '%s' from '%s'.\n", jack_port_name(meterec->ports[port].output), all_ports[i] );
				jack_disconnect(meterec->client, all_ports[i], jack_port_name(meterec->ports[port].output));
			}
			
			free(all_ports);
		}
	}
	
	/* Leave the jack graph */
	jack_client_close(meterec->client);
	
	fprintf(meterec->fd_log, "Closed jack client connection.\n");
}

void cleanup_curse(void) {
	
	delwin(mainwin);
	endwin();
	refresh();
	fprintf(meterec->fd_log, "Stopped ncurses interface.\n");
	
}

static void halt(int sig) {

	running = 0;

	(void) signal(sig, SIG_DFL);
}

/* Close down JACK when exiting */
static void cleanup() {
	
	stop(meterec);
	
	running = 0;
	
	if (meterec->curses_sts)
		cleanup_curse();
		
	if (meterec->config_sts)
		save_conf(meterec);
		
	pthread_join(rd_dt, NULL);
	pthread_join(wr_dt, NULL);

	if (meterec->jack_sts)
		cleanup_jack();
	
	fclose(meterec->fd_log);
	
}

void exit_on_error(char * reason) {
	
	fprintf(meterec->fd_log, "Error: %s\n", reason);
	printf("Error: %s\n", reason);
	cleanup();
	exit(1);
}

void time_sprint(struct time_s * time, char * string) {
	sprintf(string, "%u:%02u:%02u.%03u",time->h, time->m, time->s, time->ms);
	
}

void time_hms(struct time_s * time) {
	
	unsigned int rate = time->rate;
	
	time->h = (unsigned int) ( time->frm / rate ) / 3600;
	time->m = (unsigned int) ((time->frm / rate ) / 60 ) % 60;
	time->s = (unsigned int) ( time->frm / rate ) % 60;
	rate /= 1000;
	time->ms =(unsigned int) ( time->frm / rate ) % 1000;
	
}

void time_frm(struct time_s * time) {
	time->frm =  (unsigned int) (
		time->h  * time->rate * 3600 +
		time->m  * time->rate * 60 +
		time->s  * time->rate +
		time->ms * time->rate / 1000
		);
}


/* Sleep for a fraction of a second */
static int fsleep( float secs ) {
	
#ifdef HAVE_USLEEP
	return usleep( secs * 1000000ul );
#else 
	return 0;
#endif
	
}

/* Read and reset the recent peak sample */
void read_peak(float bias) {
	
	unsigned int port;
	
	for (port = 0; port < meterec->n_ports; port++) {
		
		if (meterec->ports[port].peak_in > meterec->ports[port].max_in) {
			meterec->ports[port].max_in = meterec->ports[port].peak_in;
			meterec->ports[port].db_max_in = 20.0f * log10f( meterec->ports[port].max_in * bias ) ;
		}
		
		meterec->ports[port].db_in = 20.0f * log10f( meterec->ports[port].peak_in * bias ) ;
		meterec->ports[port].peak_in = 0.0f;
		
		meterec->ports[port].db_out = 20.0f * log10f( meterec->ports[port].peak_out * bias ) ;
		meterec->ports[port].peak_out = 0.0f;
		
	}
	
}

/******************************************************************************
** Takes and ports
*/

unsigned int take_to_playback(struct meterec_s *meterec, unsigned int port) {
	
	unsigned int take;
	
	for ( take = meterec->n_takes + 1; take > 0; take-- )
		if (meterec->takes[take].port_has_lock[port])
		break;
	
	if (!take)
		take = meterec->n_takes + 1;
	
	for ( ; take > 0; take-- )
		if (meterec->takes[take].port_has_track[port])
			break;
	
	return take;
	
}

void compute_takes_to_playback(struct meterec_s *meterec) {
	
	unsigned int port;
	
	for ( port = 0; port < meterec->n_ports; port++ ) 
		meterec->ports[port].playback_take = take_to_playback(meterec, port);
	
}

int changed_takes_to_playback(struct meterec_s *meterec) {
	
	unsigned int port;
	
	for ( port = 0; port < meterec->n_ports; port++ ) 
		if (meterec->ports[port].playback_take != take_to_playback(meterec, port))
			return 1;
	
	return 0;
	
}

void compute_tracks_to_record(struct meterec_s *meterec) {
	
	unsigned int port;
	
	meterec->n_tracks = 0;
	
	for ( port = 0; port < meterec->n_ports; port++ ) 
		if ( meterec->ports[port].record ) {
			
			meterec->takes[meterec->n_takes+1].port_has_track[port] = 1;
			meterec->takes[meterec->n_takes+1].track_port_map[meterec->n_tracks] = port ;
			
			meterec->n_tracks ++;
			
		}
	
	meterec->takes[meterec->n_takes+1].ntrack = meterec->n_tracks ;
	
}

/******************************************************************************
** INITs
*/

void init_ports(struct meterec_s *meterec) {
	
	unsigned int port, con;
	
	meterec->n_ports = 0;
	
	for (port = 0; port < MAX_PORTS; port++) {
		
		meterec->ports[port].input = NULL;
		meterec->ports[port].output = NULL;
		
		meterec->ports[port].portmap = 0;
		for (con = 0; con < MAX_CONS; con++)
			meterec->ports[port].connections[con] = NULL;
		
		meterec->ports[port].name = NULL;
		
		meterec->ports[port].write_disk_buffer = NULL;
		meterec->ports[port].read_disk_buffer = NULL;
		meterec->ports[port].monitor = OFF;
		meterec->ports[port].record = OFF;
		meterec->ports[port].mute = OFF;
		meterec->ports[port].thru = OFF;
		
		meterec->ports[port].peak_out = 0.0f;
		meterec->ports[port].db_out = 0.0f;
		
		meterec->ports[port].peak_in = 0.0f;
		meterec->ports[port].db_in = 0.0f;
		
		meterec->ports[port].max_in = 0.0f;
		meterec->ports[port].db_max_in = 0.0f;
		
		meterec->ports[port].dkpeak_in = 0;
		meterec->ports[port].dktime_in = 0;
		meterec->ports[port].dkmax_in = 0;
		
		meterec->ports[port].playback_take = 0;
		
	}
	
}

void init_takes(struct meterec_s *meterec) {
	
	unsigned int port, take, track ;
	
	meterec->n_takes = 0;
	
	for (take=0; take<MAX_TAKES; take++) {
		
		meterec->takes[take].take_file = NULL;
		meterec->takes[take].take_fd = NULL;
		meterec->takes[take].buf = NULL;
		meterec->takes[take].info.format = 0;
		
		meterec->takes[take].ntrack = 0;
		
		for (track=0; track<MAX_TRACKS; track++) {
			meterec->takes[take].track_port_map[track] = 0;
		}
		
		for (port=0; port<MAX_PORTS; port++) {
			meterec->takes[take].port_has_track[port] = 0;
			meterec->takes[take].port_has_lock[port] = 0;
		}
		
	}
	
}

void pre_option_init(struct meterec_s *meterec) {
	
	unsigned int index;
	
	meterec->n_tracks = 0;
	meterec->connect_ports = 1;
	
	meterec->monitor = NULL;
	
	meterec->record_sts = OFF;
	meterec->record_cmd = STOP;
	
	meterec->playback_sts = OFF;
	meterec->playback_cmd = START;
	
	meterec->keyboard_cmd = START;
	
	meterec->jack_sts = OFF;
	meterec->curses_sts = OFF;
	meterec->config_sts = OFF;
	
	meterec->jack_transport = 1;
		
	meterec->client = NULL;
	meterec->fd_log = NULL;
	
	meterec->write_disk_buffer_thread_pos = 0;
	meterec->write_disk_buffer_process_pos = 0;
	meterec->write_disk_buffer_overflow = 0;
	
	meterec->read_disk_buffer_thread_pos = 1; /* Hum... Would be better to rework thread loop... */
	meterec->read_disk_buffer_process_pos = 0;
	meterec->read_disk_buffer_overflow = 0;
	
	pthread_mutex_init(&meterec->seek.mutex, NULL);
	
	for (index=0; index<MAX_INDEX; index++)
		meterec->seek.index[index] = -1;
	
	meterec->seek.disk_playhead_target = -1;
	meterec->seek.jack_buffer_target = -1;
	meterec->seek.playhead_target = -1;
	
	meterec->seek.files_reopen = 0;
	meterec->seek.keyboard_lock = 0;
	
}

int find_take_name(char *session, unsigned int take, char **name) {
	
	struct dirent *entry;
	DIR *dp;
	char *current = ".";
	char *pattern;
	
	pattern = (char *) malloc( strlen(session) + strlen("_0000.") + 1 );
	sprintf(pattern,"%s_%04d.", session, take);
	
	dp = opendir(current);
	
	if (dp == NULL) {
		perror("opendir");
		return 0;
	}
	
	while((entry = readdir(dp)))
		if (strncmp(entry->d_name, pattern, strlen(pattern)) == 0) {
			
			*name = (char *) malloc( strlen(entry->d_name) + 1);
			strcpy(*name, entry->d_name);
			
			closedir(dp);
			free(pattern);
			
			return 1;
		}
	
	closedir(dp);
	free(pattern);
	
	return 0;
	
}

int file_exists(char *file) {
	
	FILE *fd_conf;
	
	if ( (fd_conf = fopen(file,"r")) == NULL )
		return 0;
	
	fclose(fd_conf);
	return 1;
	
}

void post_option_init(struct meterec_s *meterec, char *session) {
	
	meterec->session_file = (char *) malloc( strlen(session) + strlen("..sess") + 1 );
	sprintf(meterec->session_file,".%s.sess",session);
	
	meterec->setup_file = (char *) malloc( strlen(session) + strlen(".conf") + 1 );
	sprintf(meterec->setup_file,"%s.conf",session);
	
	meterec->conf_file = (char *) malloc( strlen(session) + strlen(".mrec") + 1 );
	sprintf(meterec->conf_file,"%s.mrec",session);
	
	meterec->log_file = (char *) malloc( strlen(session) + strlen(".log") + 1 );
	sprintf(meterec->log_file,"%s.log",session);
	
	meterec->output_ext = (char *) malloc( strlen(output_ext) + 1 );
	sprintf(meterec->output_ext,"%s",output_ext);
	
	if (strcmp(output_ext, "wav") == 0) 
		meterec->output_fmt = SF_FORMAT_WAV | SF_FORMAT_PCM_24;

#if defined(HAVE_W64)
	else if (strcmp(output_ext, "w64") == 0) 
		meterec->output_fmt = SF_FORMAT_W64 | SF_FORMAT_PCM_24;
#endif

#if defined(HAVE_VORBIS)
	else if (strcmp(output_ext, "ogg") == 0) 
		meterec->output_fmt = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
	
	else if (strcmp(output_ext, "oga") == 0) 
		meterec->output_fmt = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
	
	else if (strcmp(output_ext, "flac") == 0) 
		meterec->output_fmt = SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
#endif
	
	else {
		printf("Sorry, '%s' output record format is not supported.\n",output_ext);
		exit(1);
	}
	
}

void seek_existing_takes(struct meterec_s *meterec, char *session) {
	
	unsigned int take;
	
	/* this needs to be moved at config file reading time and file creation time */
	for (take=1; take<MAX_TAKES; take++) {
		
		if ( find_take_name(session, take, &meterec->takes[take].take_file) ) 
			fprintf(meterec->fd_log, "Found existing file '%s' for take %d\n", meterec->takes[take].take_file, take);
		else {
			meterec->takes[take].take_file = (char *) malloc( strlen(session) + strlen("_0000.????") + 1 );
			sprintf(meterec->takes[take].take_file, "%s_%04d.%s", session, take, meterec->output_ext);
		}
	}
	
}

/******************************************************************************
** JACK callback process
*/
static int update_jack_buffsize(jack_nframes_t nframes, void *arg) {
	
	struct meterec_s *meterec ;
	
	meterec = (struct meterec_s *)arg ;
	meterec->jack_buffsize = nframes;
	return 0;
	
}

static int process_jack_sync(jack_transport_state_t state, jack_position_t *pos, void *arg) {

	struct meterec_s *meterec ;
	
	meterec = (struct meterec_s *)arg ;
	
	if (pos) {}
	
	if (state == JackTransportStarting) 
		if (!meterec->playback_sts) 
			start_playback();
		else if (meterec->playback_sts == ONGOING) 
			return 1;
		
	if (state == JackTransportRolling) 
		return 1;
	
	
	if (state == JackTransportStopped) 
		return 1;
	
	return 0;
}

/* Callback called by JACK when audio is available. */
static int process_jack_data(jack_nframes_t nframes, void *arg) {

	jack_default_audio_sample_t *in, *out, *mon=NULL;
	jack_position_t pos;
	static jack_transport_state_t transport_state=JackTransportStopped, previous_transport_state;
	unsigned int i, port, write_pos, read_pos, remaining_write_disk_buffer, remaining_read_disk_buffer;
	unsigned int playback_ongoing, record_ongoing;
	float s;
	struct meterec_s *meterec ;
	
	meterec = (struct meterec_s *)arg ;
	
	if (meterec->jack_transport) {
		previous_transport_state = transport_state;
		transport_state = jack_transport_query(meterec->client, &pos);
	 
		/* send a stop command if the transport state as changed for stopped */
		if (previous_transport_state != transport_state)
			if (transport_state == JackTransportStopped) {
				meterec->playback_cmd = OFF;
				meterec->record_cmd = OFF;
			}

		/* compute local flags stable for this cycle */
		playback_ongoing = ((transport_state == JackTransportRolling) && (meterec->playback_sts == ONGOING));

	}
	else {
		/* compute local flags stable for this cycle */
		playback_ongoing = (meterec->playback_sts == ONGOING);

	}
	
	record_ongoing = (meterec->record_cmd != OFF);
	
	/* check if there is a new buffer position to go to*/
	if (meterec->seek.jack_buffer_target != (unsigned int)(-1)) {
		
		pthread_mutex_lock( &meterec->seek.mutex );
		
		meterec->read_disk_buffer_process_pos = meterec->seek.jack_buffer_target;
		
		/* if we seek because of a file re-open, compensate for what played since re-open request */
		if ( meterec->seek.files_reopen ) {
			meterec->read_disk_buffer_process_pos += (playhead - meterec->seek.playhead_target);
			meterec->read_disk_buffer_process_pos &= (DISK_SIZE - 1);
			meterec->seek.files_reopen = 0;
			meterec->seek.keyboard_lock = 0;
		} else {
			/* re-align playhead value if we moved due to a simple seek */
			playhead = meterec->seek.playhead_target;
		}
		
		meterec->seek.playhead_target = -1;
		meterec->seek.jack_buffer_target = -1;
		
		pthread_mutex_unlock( &meterec->seek.mutex );
		
	}
	
	/* get the monitor port buffer*/
	if (meterec->monitor != NULL) {
		mon = (jack_default_audio_sample_t *) jack_port_get_buffer(meterec->monitor, nframes);
		
		/* clean buffer */
		for (i = 0; i < nframes; i++)
			mon[i] = 0.0f;
		
	}
	
	/* get the audio samples, and find the peak sample */
	for (port = 0; port < meterec->n_ports; port++) {
		
		/* just in case the port isn't registered yet */
		if (meterec->ports[port].input == NULL) 
			continue;
		
		/* just in case the port isn't registered yet */
		if (meterec->ports[port].output == NULL) 
			continue;
		
		out = (jack_default_audio_sample_t *) jack_port_get_buffer(meterec->ports[port].output, nframes);
		in = (jack_default_audio_sample_t *) jack_port_get_buffer(meterec->ports[port].input, nframes);
		
		/* copy monitored ports to the monitor port*/
		if (meterec->monitor != NULL)
			if (meterec->ports[port].monitor)
				for (i = 0; i < nframes; i++)
					mon[i] += in[i];
		
		
		if (playback_ongoing) {
			
			read_pos = meterec->read_disk_buffer_process_pos;
			
			for (i = 0; i < nframes; i++) {
				
				if (meterec->ports[port].mute)
					out[i] = 0.0f;
				else
					out[i] = meterec->ports[port].read_disk_buffer[read_pos];
				
				/* update buffer pointer */
				read_pos = (read_pos + 1) & (DISK_SIZE - 1);
				
				/* compute peak of input (recordable) data*/
				s = fabs(in[i] * 1.0f) ;
				if (s > meterec->ports[port].peak_in) 
					meterec->ports[port].peak_in = s;
				
				/* compute peak of output (playback) data */
				s = fabs(out[i] * 1.0f) ;
				if (s > meterec->ports[port].peak_out) 
					meterec->ports[port].peak_out = s;
			}
			
			if (record_ongoing) {
			
			write_pos = meterec->write_disk_buffer_process_pos;
			
			for (i = 0; i < nframes; i++) {
				
				/* Fill write disk buffer */
				if (meterec->ports[port].record==OVR) 
					meterec->ports[port].write_disk_buffer[write_pos] = in[i] + out[i];
				else if (meterec->ports[port].record)
					meterec->ports[port].write_disk_buffer[write_pos] = in[i];
				
				/* update buffer pointer */
				write_pos = (write_pos + 1) & (DISK_SIZE - 1);
				
				}
			
			}
		}
		else {
			
			for (i = 0; i < nframes; i++) {
			
			out[i] = 0.0f ;
			
			/* compute peak */
			s = fabs(in[i] * 1.0f) ;
			if (s > meterec->ports[port].peak_in)
				meterec->ports[port].peak_in = s;
			
			}
		
		}
		
		if (meterec->ports[port].thru)
			out[i] += in[i];
	
	}
	
	if (playback_ongoing) {
		
		/* track buffer over/under flow -- needs rework */
		remaining_read_disk_buffer = DISK_SIZE - ((meterec->read_disk_buffer_thread_pos-meterec->read_disk_buffer_process_pos) & (DISK_SIZE-1));
		
		if (remaining_read_disk_buffer <= nframes)
			meterec->read_disk_buffer_overflow++;
		
		/* positon read pointer to end of ringbuffer */
		meterec->read_disk_buffer_process_pos = (meterec->read_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);
		
		/* update frame/time counter */
		playhead += nframes ;
		
		if (record_ongoing) {
			
			/* track buffer over/under flow */
			remaining_write_disk_buffer = DISK_SIZE - ((meterec->write_disk_buffer_process_pos-meterec->write_disk_buffer_thread_pos) & (DISK_SIZE-1));
			
			if (remaining_write_disk_buffer <= nframes)
				meterec->write_disk_buffer_overflow++;

			/* positon write pointer to end of ringbuffer*/
			meterec->write_disk_buffer_process_pos = (meterec->write_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);
		
		}
	
	}
	else {
		
		playhead = 0 ;
		
	}
	
	return 0;
	
}
/******************************************************************************
** PORTS
*/

void create_input_port(jack_client_t *client, unsigned int port) {
	
	char port_name[10] ;
	
	sprintf(port_name,"in_%d",port+1);
	
	fprintf(meterec->fd_log,"Creating input port '%s'.\n", port_name );
	
	if (!(meterec->ports[port].input = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
		fprintf(meterec->fd_log, "Cannot register input port '%s'.\n",port_name);
		exit_on_error("Cannot register input port");
	}
	
}

void create_output_port(jack_client_t *client, unsigned int port) {
	
	char port_name[10] ;
	
	sprintf(port_name,"out_%d",port+1);
	
	fprintf(meterec->fd_log,"Creating output port '%s'.\n", port_name );
	
	if (!(meterec->ports[port].output = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(meterec->fd_log, "Cannot register output port '%s'.\n",port_name);
		exit_on_error("Cannot register output port");
	}
	
}

void create_monitor_port(jack_client_t *client) {
	
	char port_name[] = "monitor" ;
	
	fprintf(meterec->fd_log,"Creating output port '%s'.\n", port_name );
	
	if (!(meterec->monitor = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(meterec->fd_log, "Cannot register output port '%s'.\n",port_name);
		exit_on_error("Cannot register output port");
	}
	
}

/* Connect the chosen port to ours */
void connect_any_port(jack_client_t *client, char *port_name, unsigned int port)
{
	jack_port_t *jack_port;
	int jack_flags;
	char *tmp = NULL;
	
	/* connect input port*/
	
	// Get the port we are connecting to
	jack_port = jack_port_by_name(client, port_name);
	
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
		meterec->ports[port].connections[meterec->ports[port].portmap] = (char *) malloc( strlen(port_name) + 1 );
		strcpy(meterec->ports[port].connections[meterec->ports[port].portmap], port_name);
		meterec->ports[port].portmap += 1;
	}
	
	if (!meterec->connect_ports)
		return;
	
	/* check port flags */
	jack_flags = jack_port_flags(jack_port);
	
	if ( jack_flags & JackPortIsInput ) {
		
		// Connect the port to our output port
		fprintf(meterec->fd_log,"Connecting '%s' to '%s'...\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));
		if (jack_connect(client, jack_port_name(meterec->ports[port].output), jack_port_name(jack_port))) {
			fprintf(meterec->fd_log, "Cannot connect port '%s' to '%s'\n", jack_port_name(meterec->ports[port].output), jack_port_name(jack_port));
			exit_on_error("Cannot connect ports");
		}
		
	}
	
	if ( jack_flags & JackPortIsOutput ) {
		
		// Connect the port to our input port
		fprintf(meterec->fd_log,"Connecting '%s' to '%s'...\n", jack_port_name(jack_port), jack_port_name(meterec->ports[port].input));
		if (jack_connect(client, jack_port_name(jack_port), jack_port_name(meterec->ports[port].input))) {
			fprintf(meterec->fd_log, "Cannot connect port '%s' to '%s'\n", jack_port_name(jack_port), jack_port_name(meterec->ports[port].input));
			exit_on_error("Cannot connect ports");
		}
	
	}
	
}


/******************************************************************************
** THREAD Utils
*/

void start_playback() {
	
	compute_takes_to_playback(meterec);
	meterec->playback_cmd = START ;
	pthread_create(&rd_dt, NULL, (void *)&reader_thread, (void *) meterec);

}

void start_record() {
	
	compute_tracks_to_record(meterec);
	if (meterec->n_tracks) {
		meterec->record_cmd = START;
		pthread_create(&wr_dt, NULL, (void *)&writer_thread, (void *) meterec);
	}

}

void stop(struct meterec_s *meterec) {
		
	fprintf(meterec->fd_log, "Stop requested.\n");
	
	if (meterec->jack_transport)
		jack_transport_stop(meterec->client);
	else {
		meterec->playback_cmd = STOP;
		meterec->record_cmd = STOP;
	}
}

void roll(struct meterec_s *meterec) {
		
	fprintf(meterec->fd_log, "Roll requested.\n");
	
	if (meterec->jack_transport)
		jack_transport_start(meterec->client);
	else 
		start_playback();
}

unsigned int seek(int seek_sec) {
	
	jack_nframes_t nframes, sample_rate;
	
	nframes = playhead;
	sample_rate = jack_get_sample_rate(meterec->client);
	
	fprintf(meterec->fd_log,"seek: at %d needs to seek %d (sr=%d)\n",nframes,seek_sec * sample_rate,sample_rate );
	
	if ( seek_sec < 0 )
		if ( nframes < abs( seek_sec ) * sample_rate )
			return 0;
	
	return (nframes + seek_sec * sample_rate); 
	
}


/******************************************************************************
** KEYBOARD
*/

int keyboard_thread(void *arg) {
	
	struct meterec_s *meterec ;
	unsigned int port, take;
	int key = 0;
	
	meterec = (struct meterec_s *)arg ;
	
	noecho();
	cbreak();
	nodelay(stdscr, FALSE);
	keypad(stdscr, TRUE);
	
	while (meterec->keyboard_cmd) {
	
		key = wgetch(stdscr);
		
		fprintf(meterec->fd_log, "Key pressed: %d '%c'\n",key,key);
		
		if (edit_mode) {
			
			switch (key) {
				
				/* 
				** Move cursor 
				*/
				case KEY_LEFT :
					if ( x_pos > 1 )
						x_pos--;
					break;
				
				case KEY_RIGHT :
					if ( x_pos < meterec->n_takes )
						x_pos++;
					break;
			}
			
			/* 
			** Change Locks 
			*/
			if (!meterec->seek.keyboard_lock) {
				
				switch (key) {
					case 'L' : /* clear all other locks for that port & process with toggle */
						for ( take=0 ; take < meterec->n_takes+1 ; take++) 
							meterec->takes[take].port_has_lock[y_pos] = 0 ;
					
					case 'l' : /* toggle lock at this position */
						meterec->takes[x_pos].port_has_lock[y_pos] = !meterec->takes[x_pos].port_has_lock[y_pos] ;
						
						if (changed_takes_to_playback(meterec) && (meterec->playback_sts != OFF)) {
							pthread_mutex_lock( &meterec->seek.mutex );
							meterec->seek.disk_playhead_target = playhead;
							meterec->seek.files_reopen = 1;
							meterec->seek.keyboard_lock = 1;
							pthread_mutex_unlock( &meterec->seek.mutex );
						}
						break;
					
					case 'A' : /* clear all other locks & process with toggle */
						for ( port=0 ; port < meterec->n_ports ; port++)
							for ( take=0 ; take < meterec->n_takes+1 ; take++)  
								meterec->takes[take].port_has_lock[port] = 0 ;
					
					case 'a' : /* toggle lock for all ports depending on this position */
						if ( meterec->takes[x_pos].port_has_lock[y_pos] ) 
							for ( port=0 ; port < meterec->n_ports ; port++) 
								meterec->takes[x_pos].port_has_lock[port] = 0;
						else 
							for ( port=0 ; port < meterec->n_ports ; port++) 
								meterec->takes[x_pos].port_has_lock[port] = 1;
						
						if (changed_takes_to_playback(meterec) && (meterec->playback_sts != OFF)) {
							pthread_mutex_lock( &meterec->seek.mutex );
							meterec->seek.disk_playhead_target = playhead;
							meterec->seek.files_reopen = 1;
							meterec->seek.keyboard_lock = 1;
							pthread_mutex_unlock( &meterec->seek.mutex );
						}
						break;
				}
				
			}
			
		} 
		else {
			
			switch (key) {
				/* reset absolute maximum markers */
				
				case 'n': 
					display_names = !display_names ;
					break;
					
				case 'v':
					for ( port=0 ; port < meterec->n_ports ; port++) {
						meterec->ports[port].dkmax_in = 0;
						meterec->ports[port].max_in = 0;
						meterec->ports[port].db_max_in = 20.0f * log10f(0) ;
					}
					break;
				
				case KEY_LEFT:
					if (!meterec->record_sts && meterec->playback_sts )
						meterec->seek.disk_playhead_target = seek(-5);
					break;
				
				case KEY_RIGHT:
					if (!meterec->record_sts && meterec->playback_sts )
						meterec->seek.disk_playhead_target = seek(5);
					break;
			}
			
		}
		
		/*
		** KEYs handled in all modes
		*/
		
		if (meterec->record_sts==OFF) {
			
			switch (key) {
				/* Change record mode */
				case 'R' : 
				case 'r' : 
					if ( meterec->ports[y_pos].record == REC )
						meterec->ports[y_pos].record = OFF;
					else
						meterec->ports[y_pos].record = REC;
					break;
				
				case 'D' : 
				case 'd' : 
					if ( meterec->ports[y_pos].record == DUB )
						meterec->ports[y_pos].record = OFF;
					else
						meterec->ports[y_pos].record = DUB;
					break;
				
				case 'O' : 
				case 'o' : 
					if ( meterec->ports[y_pos].record == OVR )
						meterec->ports[y_pos].record = OFF;
					else
						meterec->ports[y_pos].record = OVR;
					break;
			
			}
		
		}
		
		switch (key) {
			
			case 'T' : /* toggle pass thru on all ports */
				if ( meterec->ports[y_pos].thru ) 
					for ( port=0 ; port < meterec->n_ports ; port++) 
						meterec->ports[port].thru = 0;
				else 
					for ( port=0 ; port < meterec->n_ports ; port++) 
						meterec->ports[port].thru = 1;
				break;
			
			case 't' : /* toggle pass thru on this port */
				meterec->ports[y_pos].thru = !meterec->ports[y_pos].thru;
				break;

			case 'M' : /* toggle mute on all ports */
				if ( meterec->ports[y_pos].mute ) 
					for ( port=0 ; port < meterec->n_ports ; port++) 
						meterec->ports[port].mute = 0;
				else 
					for ( port=0 ; port < meterec->n_ports ; port++) 
						meterec->ports[port].mute = 1;
				break;
			
			case 'm' : /* toggle mute on this port */
				meterec->ports[y_pos].mute = !meterec->ports[y_pos].mute;
				break;
			
			case 'S' : /* unmute all ports */
				for ( port=0 ; port < meterec->n_ports ; port++) 
				meterec->ports[port].mute = 0;
				break;
			
			case 's' : /* mute all but this port */
				for ( port=0 ; port < meterec->n_ports ; port++) 
					meterec->ports[port].mute = 1;
				meterec->ports[y_pos].mute = 0;
				break;
			
			case KEY_UP :
				meterec->ports[y_pos].monitor = 0;
				if ( y_pos == 0 )
					y_pos = meterec->n_ports - 1;
				else
					y_pos--;
				meterec->ports[y_pos].monitor = 1;
				break;
			
			case KEY_DOWN :
				meterec->ports[y_pos].monitor = 0;
				if ( y_pos == meterec->n_ports - 1 )
					y_pos = 0;
				else 
					y_pos++;
				meterec->ports[y_pos].monitor = 1;
				break;
			
			case 9: /* TAB */
				edit_mode = !edit_mode ;
				break;
			
			case 10: /* RETURN */
				if (meterec->playback_sts == ONGOING)
					stop(meterec);
				else if (meterec->playback_sts == OFF) {
					start_record();
					roll(meterec);
				}
				break;
			
			case 127: /* BACKSPACE */
			case 263: /* BACKSPACE */
				if (meterec->record_sts == ONGOING) 
					meterec->record_cmd = RESTART;
				else if (meterec->playback_sts == OFF)
					start_record();
				break;
			
			case ' ':
				if (meterec->playback_sts == ONGOING && meterec->record_sts == OFF)
					stop(meterec);
				else if (meterec->playback_sts == OFF)
					roll(meterec);
				break;
			
			case 'Q':
			case 'q':
				meterec->keyboard_cmd = STOP;
				running = 0;
				break;
			
		}
		
		/* set index using SHIFT */
		if ( KEY_F(13) <= key && key <= KEY_F(24) ) 
		meterec->seek.index[key - KEY_F(13)] = playhead ;
		
		/* seek to index */
		if (!meterec->record_sts && meterec->playback_sts ) {
			
			if ( KEY_F(1) <= key && key <= KEY_F(12) ) {
				pthread_mutex_lock( &meterec->seek.mutex );
				meterec->seek.disk_playhead_target = meterec->seek.index[key - KEY_F(1)];
				sprintf(meterec->log_file,"key: seek %d",meterec->seek.disk_playhead_target );
				pthread_mutex_unlock( &meterec->seek.mutex );
			}
			
			if ( key == KEY_HOME ) {
				pthread_mutex_lock( &meterec->seek.mutex );
				meterec->seek.disk_playhead_target = 0;
				pthread_mutex_unlock( &meterec->seek.mutex );
			}
		}
		
	} // while
	
	return 0;
	
}

/******************************************************************************
** CORE
*/

/* Display how to use this program */
static int usage( const char * progname ) {
	fprintf(stderr, "version %s\n\n", VERSION);
	fprintf(stderr, "%s [-f freqency] [-r ref-level] [-w width] [-s session-name] [-j jack-name] [-o output-format] [-t][-p][-c]\n\n", progname);
	fprintf(stderr, "where  -f      is how often to update the meter per second [24]\n");
	fprintf(stderr, "       -r      is the reference signal level for 0dB on the meter [0]\n");
	fprintf(stderr, "       -w      is how wide to make the meter [auto]\n");
	fprintf(stderr, "       -s      is session name [%s]\n",session);
	fprintf(stderr, "       -j      is the jack client name [%s]\n",jackname);
	fprintf(stderr, "       -o      is the record output format (w64, wav, flac, ogg) [%s]\n",output_ext);
	fprintf(stderr, "       -t      record a new take at start\n");
	fprintf(stderr, "       -p      no playback at start\n");
	fprintf(stderr, "       -c      do not connect to jack ports listed in .mrec file\n");
	fprintf(stderr, "       -i      do not interact with jack transport\n");
	fprintf(stderr, "\n\n");
	fprintf(stderr, "Command keys:\n");
	fprintf(stderr, "       q       quit\n");
	fprintf(stderr, "       <SPACE> start playback; stop playback\n");
	fprintf(stderr, "       <ENTER> start record; stop all\n");
	fprintf(stderr, "       <BKSPS> create new take while record is ongoing\n");
	fprintf(stderr, "       v       reset maximum level vu-meter markers\n");
	fprintf(stderr, "       n       toggle port names display\n");
	fprintf(stderr, "       m       mute that port playback\n");
	fprintf(stderr, "       M       mute all ports playback\n");
	fprintf(stderr, "       s       mute all but that port playback (solo)\n");
	fprintf(stderr, "       S       unmute all ports playback\n");
	fprintf(stderr, "       r       toggle REC record mode for that port - record without listening playback\n");
	fprintf(stderr, "       d       toggle DUB record mode for that port - record listening playback\n");
	fprintf(stderr, "       o       toggle OVR record mode for that port - record listening and mixing playback\n");
	fprintf(stderr, "<SHIFT>F1-F12  set time index\n");
	fprintf(stderr, "       F1-F12  Jump to time index\n");
	fprintf(stderr, "       =>      seek forward 5sec\n");
	fprintf(stderr, "       <=      seek backward 5sec\n");
	fprintf(stderr, "       <HOME>  be kind, rewind\n");
	fprintf(stderr, "       <TAB>   edit mode\n");
	fprintf(stderr, "       l       toggle lock for that position\n");
	fprintf(stderr, "       L       clear all locks for that port, toggle lock for that position\n");
	fprintf(stderr, "       a       toggle lock for all ports for that take\n");
	fprintf(stderr, "       A       clear all locks, toggle lock for all ports for that take\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int console_width = 0; 
	jack_status_t status;
	float ref_lev = 0;
	int rate = 24;
	int opt;
	int decay_len;
	float bias = 1.0f;
	
	meterec = (struct meterec_s *) malloc( sizeof(struct meterec_s) ) ;
	
	init_ports(meterec);
	init_takes(meterec);
	
	pre_option_init(meterec);
	
	while ((opt = getopt(argc, argv, "r:w:f:s:j:o:ptchvi")) != -1) {
		switch (opt) {
			case 'r':
				ref_lev = atof(optarg);
				bias = powf(10.0f, ref_lev * -0.05f);
				break;
			
			case 'f':
				rate = atoi(optarg);
				break;
			
			case 'w':
				console_width = atoi(optarg);
				break;
				
			case 's':
				session = optarg ;
				break;
				
			case 'j':
				jackname = optarg ;
				break;
				
			case 'o':
				output_ext = optarg ;
				break;
				
			case 't':
				meterec->record_cmd = START;
				break;
				
			case 'p':
				meterec->playback_cmd = STOP;
				break;
				
			case 'c':
				meterec->connect_ports = 0;
				break;
				
			case 'i':
				meterec->jack_transport = OFF;
				break;
				
			case 'h':
			case 'v':
			default:
				/* Show usage/version information */
				usage( argv[0] );
				break;
		}
	}
	
	post_option_init(meterec, session);
	
	if ( (meterec->fd_log = fopen(meterec->log_file,"w")) == NULL ) {
		fprintf(stderr,"Error: could not open '%s' for writing\n", meterec->log_file);
		exit(1);
	}
	
	fprintf(meterec->fd_log,"---- Options ----\n");
	fprintf(meterec->fd_log,"Reference level: %.1fdB\n", ref_lev);
	fprintf(meterec->fd_log,"Updates per second: %d\n", rate);
	fprintf(meterec->fd_log,"Console Width: %d\n", console_width);
	fprintf(meterec->fd_log,"Session name: %s\n", session);
	fprintf(meterec->fd_log,"Jack client name: %s\n", jackname);
	fprintf(meterec->fd_log,"Output format: %s\n", output_ext);
	fprintf(meterec->fd_log,"%slayback at startup.\n",meterec->playback_cmd?"P":"No p");
	fprintf(meterec->fd_log,"%secording new take at startup.\n",meterec->record_cmd?"R":"Not r");
	fprintf(meterec->fd_log,"%snteract with jack transport.\n",meterec->jack_transport?"I":"Do not i");
	fprintf(meterec->fd_log,"---- Starting ----\n");
	
	/* Register with Jack */
	fprintf(meterec->fd_log, "Connecting to jackd...\n");
	if ((meterec->client = jack_client_open(jackname, JackNullOption, &status)) == 0) {
		fprintf(meterec->fd_log, "Failed to start '%s' jack client: %d\n", jackname, status);
		fprintf(stderr,"Failed to start '%s' jack client: %d - Is jackd running?\n", jackname, status);
		exit(1);
	}
	fprintf(meterec->fd_log,"Registered as '%s'.\n", jack_get_client_name( meterec->client ) );

	meterec->jack_sts = ONGOING;
	
	/* Register the signal process callback */
	jack_set_process_callback(meterec->client, process_jack_data, meterec);
	
	/* Register function to handle buffer size change */
	jack_set_buffer_size_callback(meterec->client, update_jack_buffsize, meterec);
	
	/* Register function to handle transport changes */
	if (meterec->jack_transport)
		jack_set_sync_callback(meterec->client, process_jack_sync, meterec);
	
	/* get initial buffer size */
	meterec->jack_buffsize = jack_get_buffer_size(meterec->client);
	
	if (jack_activate(meterec->client)) {
		fprintf(meterec->fd_log, "Cannot activate client.\n");
		exit_on_error("Cannot activate client");
	}
	
	if (file_exists(meterec->conf_file)) {
		load_conf(meterec);
	} else {
		load_setup(meterec);
		load_session(meterec);
		save_conf(meterec);
		fprintf(meterec->fd_log, "Converted old configuration to %s.\n", meterec->conf_file );
		exit_on_error("Converted old configuration");
	}
	
	meterec->config_sts = ONGOING;
	
	create_monitor_port(meterec->client);
	
	fprintf(meterec->fd_log, "Starting ncurses interface...\n");
	
	mainwin = initscr();
	
	if ( mainwin == NULL ) {
		fprintf(meterec->fd_log, "Error initialising ncurses.\n");
		exit(1);
	}
	
	meterec->curses_sts = ONGOING;
	
	start_color();
	
	// choose our color pairs
	init_pair(GREEN,  COLOR_GREEN,   COLOR_BLACK);
	init_pair(YELLOW, COLOR_YELLOW,  COLOR_BLACK);
	init_pair(BLUE,   COLOR_BLUE,    COLOR_BLACK);
	init_pair(RED,    COLOR_RED,     COLOR_BLACK);
	
	if (!console_width)
		console_width = getmaxx(mainwin);
	
	console_width --;
	
	/* Calculate the decay length (should be 1600ms) */
	decay_len = (int)(1.6f / (1.0f/rate));
	
	/* Init the scale */
	init_display_scale(console_width);
	
	pthread_create(&kb_dt, NULL, (void *)&keyboard_thread, (void *) meterec);
	
	seek_existing_takes(meterec, session);
	
	/* Start threads doing disk accesses */
	if (meterec->record_cmd==START) {
		
		start_record();
		roll(meterec);
		
	}
	else if (meterec->playback_cmd==START) {
		
		roll(meterec);
		
	}
	
	/* Register the cleanup function to be called when C-c */
	signal(SIGINT, halt);
	
	x_pos = meterec->n_takes;
	
	while (running) {
		
		read_peak(bias);
		
		clear();
		
		display_status(meterec, playhead);
		display_buffer(meterec, console_width);
		
		if (edit_mode)
			display_session(meterec, y_pos, x_pos);
		else
			display_meter(meterec, y_pos, display_names, console_width, decay_len);
		
		refresh();
		
		fsleep( 1.0f/rate );
		
	}
	
	cleanup();
	
	pthread_kill(kb_dt, SIGTERM); 
	pthread_join(kb_dt, NULL);

	return 0;
	
}

