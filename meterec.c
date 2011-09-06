/*

  meterec.c 
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
#include <dirent.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <sndfile.h>
#include <jack/jack.h>
#include <jack/session.h>
#include <getopt.h>
#include <curses.h>

#include "config.h"
#include "position.h"
#include "meterec.h"
#include "display.h"
#include "disk.h"
#include "conf.h"
#include "ports.h"
#include "queue.h"
#include "session.h"



WINDOW * mainwin;

int view=VU, display_names=1;
int running = 1;

#if defined(HAVE_W64)
char *output_ext = "w64" ;
#else
char *output_ext = "wav" ;
#endif

pthread_t wr_dt=(pthread_t)NULL, rd_dt=(pthread_t)NULL, kb_dt=(pthread_t)NULL ;

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

void halt(int sig) {
	
	running = 0;
	
	if (sig)
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
		
	if (rd_dt)
		pthread_join(rd_dt, NULL);
		
	if (wr_dt)
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

void set_loop(struct meterec_s *meterec, unsigned int loophead) {
	
	if (meterec->loop.low == MAX_UINT) {
		meterec->loop.low = loophead;
		return;
	}
	else if (loophead > meterec->loop.low) {
		meterec->loop.high = loophead;
	}
	else if (loophead < meterec->loop.low) {
		meterec->loop.high = meterec->loop.low;
		meterec->loop.low = loophead;
	}
	
	meterec->loop.enable = 1;
	
	pthread_mutex_lock( &meterec->event_mutex );
	add_event(meterec, DISK, LOOP, meterec->loop.high, meterec->loop.low, MAX_UINT);
	pthread_mutex_unlock( &meterec->event_mutex );

}

void clr_loop(struct meterec_s *meterec) {
	
	meterec->loop.enable = 0;
	meterec->loop.low = MAX_UINT;
	meterec->loop.high = MAX_UINT;
	
	pthread_mutex_lock( &meterec->event_mutex );
	add_event(meterec, DISK, LOOP, MAX_UINT, MAX_UINT, MAX_UINT);
	pthread_mutex_unlock( &meterec->event_mutex );

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

		meterec->ports[port].input_connected = NULL;
		meterec->ports[port].output_connected = NULL;
		
		meterec->ports[port].n_cons = 0;
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

void free_ports(struct meterec_s *meterec) {
	
	unsigned int port, con;
	
	for (port = 0; port < MAX_PORTS; port++) {
		
		free(meterec->ports[port].input_connected);
		free(meterec->ports[port].output_connected);
		
		for (con = 0; con < meterec->ports[port].n_cons; con++)
			free(meterec->ports[port].connections[con]);
		
		free(meterec->ports[port].name);
		
		free(meterec->ports[port].write_disk_buffer);
		free(meterec->ports[port].read_disk_buffer);
		
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

void free_takes(struct meterec_s *meterec) {
	
	unsigned int take;
	
	for (take=0; take<MAX_TAKES; take++) {
		
		free(meterec->takes[take].take_file);
		free(meterec->takes[take].take_fd);
		free(meterec->takes[take].buf);
		
	}
	
}

void pre_option_init(struct meterec_s *meterec) {
	
	unsigned int index;
	
	meterec->n_tracks = 0;
	meterec->connect_ports = 1;
	
	meterec->jack_name = "meterec";
	meterec->session = "meterec";
	
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
	
	meterec->pos.port = 0;
	meterec->pos.take = 0;
	meterec->pos.inout = 0;
	meterec->pos.con_in = 0;
	meterec->pos.con_out = 0;
	
	meterec->jack.sample_rate = 0;
	meterec->jack.playhead = 0;
	
	meterec->disk.playhead = 0;
	
	meterec->all_input_ports = NULL;
	meterec->all_output_ports = NULL;
	
	meterec->client = NULL;
	meterec->fd_log = NULL;
	
	meterec->write_disk_buffer_thread_pos = 0;
	meterec->write_disk_buffer_process_pos = 0;
	meterec->write_disk_buffer_overflow = 0;
	
	meterec->read_disk_buffer_thread_pos = 1; /* Hum... Would be better to rework thread loop... */
	meterec->read_disk_buffer_process_pos = 0;
	meterec->read_disk_buffer_overflow = 0;
	
	for (index=0; index<MAX_INDEX; index++)
		meterec->seek_index[index] = MAX_UINT;
	
	meterec->loop.low = MAX_UINT;
	meterec->loop.high = MAX_UINT;
	meterec->loop.enable = 0;
	
	meterec->event = NULL;
	pthread_mutex_init(&meterec->event_mutex, NULL);
}

void free_options(struct meterec_s *meterec) {
	
	free(meterec->all_input_ports);
	free(meterec->all_output_ports);
	free(meterec->session_file);
	free(meterec->setup_file);
	free(meterec->conf_file);
	free(meterec->log_file);
	free(meterec->output_ext);
	
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

void post_option_init(struct meterec_s *meterec) {

	char *session;
	
	session = meterec->session;
	
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

void find_existing_takes(struct meterec_s *meterec) {
	
	unsigned int take;
	
	/* this needs to be moved at config file reading time and file creation time */
	for (take=1; take<MAX_TAKES; take++) {
		
		if ( find_take_name(meterec->session, take, &meterec->takes[take].take_file) ) 
			fprintf(meterec->fd_log, "Found existing file '%s' for take %d\n", meterec->takes[take].take_file, take);
		else {
			meterec->takes[take].take_file = (char *) malloc( strlen(meterec->session) + strlen("_0000.????") + 1 );
			sprintf(meterec->takes[take].take_file, "%s_%04d.%s", meterec->session, take, meterec->output_ext);
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
	
	if (state == JackTransportStarting) {
		if (!meterec->playback_sts) 
			start_playback();
		else if (meterec->playback_sts == ONGOING) 
			return 1;
	}
		
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
	struct event_s *event;
	
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
	
	event = find_first_event(meterec, JACK, ALL);
		
	if (event) {
		switch (event->type) {
			
			case LOCK:
				meterec->read_disk_buffer_process_pos = event->buffer_pos - 1;
				
				/* if we seek because of a file re-open, compensate for what played since re-open request */
				meterec->read_disk_buffer_process_pos += (meterec->jack.playhead  - event->new_playhead);
				meterec->read_disk_buffer_process_pos &= (DISK_SIZE - 1);
				
				/*
				event_print(meterec, LOG, event);
				fprintf(meterec->fd_log, "jack:                            playhead %d |max %d |nframes %d\n", meterec->jack.playhead, meterec->read_disk_buffer_process_pos+ nframes, nframes);
				*/
				
				pthread_mutex_lock(&meterec->event_mutex);
				rm_event(meterec, event);
				event = NULL;
				pthread_mutex_unlock(&meterec->event_mutex);
				break;
			case SEEK:
				meterec->read_disk_buffer_process_pos = event->buffer_pos;
				meterec->jack.playhead = event->new_playhead;
				pthread_mutex_lock(&meterec->event_mutex);
				rm_event(meterec, event);
				event = NULL;
				pthread_mutex_unlock(&meterec->event_mutex);
				break;
		}
	}
	
	
	/* get the monitor port buffer*/
	if (meterec->monitor != NULL) {
		mon = (jack_default_audio_sample_t *) jack_port_get_buffer(meterec->monitor, nframes);
		
		/* clean buffer because we will accumulate on it */
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
				s = fabs(meterec->ports[port].read_disk_buffer[read_pos] * 1.0f) ;
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
			for (i = 0; i < nframes; i++)
				out[i] += in[i];
	
	}
	
	if (playback_ongoing) {
		
		/* track buffer over/under flow -- needs rework */
		remaining_read_disk_buffer = DISK_SIZE - ((meterec->read_disk_buffer_thread_pos-meterec->read_disk_buffer_process_pos) & (DISK_SIZE-1));
		
		if (remaining_read_disk_buffer <= nframes)
			meterec->read_disk_buffer_overflow++;
		
		/* positon read pointer to end of ringbuffer */
		meterec->read_disk_buffer_process_pos = (meterec->read_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);
		
		/* set new playhead position */
		meterec->jack.playhead += nframes ;
		
		if (event) 
			if (event->type == LOOP)
				if (meterec->jack.playhead > event->new_playhead) {
					meterec->jack.playhead -= ( event->new_playhead - event->old_playhead );
					pthread_mutex_lock( &meterec->event_mutex );
					rm_event(meterec, event);
					event = NULL;
					pthread_mutex_unlock( &meterec->event_mutex );
				}
			
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
		
		meterec->jack.playhead = 0 ;
		
	}
	
	return 0;
	
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

void cancel_record() {
	
	meterec->record_cmd = STOP;
	
	pthread_join(wr_dt, NULL);
	
	meterec->n_takes --;
	
	if (meterec->config_sts)
		save_conf(meterec);

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

unsigned int seek(struct meterec_s *meterec, int seek_sec) {
	
	jack_nframes_t nframes, sample_rate;
	
	nframes = meterec->jack.playhead;
	sample_rate = meterec->jack.sample_rate;
	
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
	struct event_s *event ;
	unsigned int y_pos, x_pos, port, take;
	int key = 0;
	int freetext = 0;
	char *text = NULL;
	
	meterec = (struct meterec_s *)arg ;
	
	noecho();
	cbreak();
	nodelay(stdscr, FALSE);
	keypad(stdscr, TRUE);
	
	while (meterec->keyboard_cmd) {
	
		key = wgetch(stdscr);
		
		fprintf(meterec->fd_log, "Key pressed: %d '%c'\n",key,key);
		
		y_pos = meterec->pos.port;
		x_pos = meterec->pos.take;
		
		if (freetext) {
			
			if (key == 10) {
				freetext = 0;
			} 
			else if ((key == 127 || key == 263) && freetext < MAX_NAME_LEN) {
				text --;
				freetext ++;
				*text = '_';
				*(text+1) = '\0';
			}
			else {
				if (key < 32)
					continue;
				if (key > 126)
					continue;
				if (key == '"')
					continue;
				
				*text     = key;
				*(text+1) = '_';
				*(text+2) = '\0';
				text ++;
				freetext --;
			}
			
			if (freetext == 0)
				*text = '\0';
			
			continue;
		}
		
		switch (view) {
		case EDIT: 
			
			switch (key) {
				
				/* 
				** Move cursor 
				*/
				case KEY_LEFT :
					if ( meterec->pos.take > 1 )
						meterec->pos.take--;
					break;
				
				case KEY_RIGHT :
					if ( meterec->pos.take < meterec->n_takes )
						meterec->pos.take++;
					break;
			}
			
			/* 
			** Change Locks 
			*/
			event = find_first_event(meterec, ALL, LOCK);
			
			if (!event) {
				
				switch (key) {
					case 'L' : /* clear all other locks for that port & process with toggle */
						for ( take=0 ; take < meterec->n_takes+1 ; take++) 
							meterec->takes[take].port_has_lock[y_pos] = 0 ;
					
					case 'l' : /* toggle lock at this position */
						meterec->takes[x_pos].port_has_lock[y_pos] = !meterec->takes[x_pos].port_has_lock[y_pos] ;
						
						if (changed_takes_to_playback(meterec) && (meterec->playback_sts != OFF)) {
							pthread_mutex_lock( &meterec->event_mutex );
							add_event(meterec, DISK, LOCK, MAX_UINT, meterec->jack.playhead, MAX_UINT); 
							pthread_mutex_unlock( &meterec->event_mutex );
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
							pthread_mutex_lock( &meterec->event_mutex );
							add_event(meterec, DISK, LOCK, MAX_UINT, meterec->jack.playhead, MAX_UINT);
							pthread_mutex_unlock( &meterec->event_mutex );
						}
						break;
				}
				
			}
			
			break;
		case VU:
			event = find_first_event(meterec, ALL, SEEK);
			
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
					if (!meterec->record_sts && meterec->playback_sts && !event) {
						pthread_mutex_lock( &meterec->event_mutex );
						add_event(meterec, DISK, SEEK, MAX_UINT, seek(meterec,-5), MAX_UINT);
						pthread_mutex_unlock( &meterec->event_mutex );
					}
					break;
				
				case KEY_RIGHT:
					if (!meterec->record_sts && meterec->playback_sts && !event) {
						pthread_mutex_lock( &meterec->event_mutex );
						add_event(meterec, DISK, SEEK, MAX_UINT, seek(meterec,5), MAX_UINT);
						pthread_mutex_unlock( &meterec->event_mutex );
					}
					break;
			}
			break;
		
		case PORT:
		
			switch (key) {
				case KEY_LEFT:
					meterec->pos.inout --;
					if (meterec->pos.inout < CON_IN)
						meterec->pos.inout = CON_OUT;
					break;
				case KEY_RIGHT:
					meterec->pos.inout ++;
					if (meterec->pos.inout > CON_OUT)
						meterec->pos.inout = CON_IN;
					break;
				case 'c':
					if (meterec->pos.inout == CON_IN)
						register_connect_port(meterec, (char*)meterec->all_output_ports[meterec->pos.con_in], meterec->pos.port);
					else if (meterec->pos.inout == CON_OUT)
						register_connect_port(meterec, (char*)meterec->all_input_ports[meterec->pos.con_out], meterec->pos.port);
					break;
				case 'x':
					if (meterec->pos.inout == CON_IN)
						deregister_disconnect_port(meterec, (char*)meterec->all_output_ports[meterec->pos.con_in], meterec->pos.port);
					else if (meterec->pos.inout == CON_OUT)
						deregister_disconnect_port(meterec, (char*)meterec->all_input_ports[meterec->pos.con_out], meterec->pos.port);
					break;
			}
			break;
		}
		
		/*
		** KEYs handled in all modes
		*/
		
		if (meterec->record_sts==OFF) {
			
			switch (key) {
				/* Change record mode */
				case 'R' : 
					if ( meterec->ports[y_pos].record == REC )
						for ( port=0 ; port < meterec->n_ports ; port++)
							meterec->ports[port].record = OFF;
					else
						for ( port=0 ; port < meterec->n_ports ; port++)
							meterec->ports[port].record = REC;
					break;
				case 'r' : 
					if ( meterec->ports[y_pos].record == REC )
						meterec->ports[y_pos].record = OFF;
					else
						meterec->ports[y_pos].record = REC;
					break;
				
				case 'D' : 
					if ( meterec->ports[y_pos].record == DUB )
						for ( port=0 ; port < meterec->n_ports ; port++)
							meterec->ports[port].record = OFF;
					else
						for ( port=0 ; port < meterec->n_ports ; port++)
							meterec->ports[port].record = DUB;
					break;
				case 'd' : 
					if ( meterec->ports[y_pos].record == DUB )
						meterec->ports[y_pos].record = OFF;
					else
						meterec->ports[y_pos].record = DUB;
					break;
				
				case 'O' : 
					if ( meterec->ports[y_pos].record == OVR )
						for ( port=0 ; port < meterec->n_ports ; port++)
							meterec->ports[port].record = OFF;
					else
						for ( port=0 ; port < meterec->n_ports ; port++)
							meterec->ports[port].record = OVR;
					break;
				case 'o' : 
					if ( meterec->ports[y_pos].record == OVR )
						meterec->ports[y_pos].record = OFF;
					else
						meterec->ports[y_pos].record = OVR;
					break;
			
			}
		
		}
		
		switch (key) {
			
			case 'i' : /* rename the current port */
				text = port_rename(meterec, y_pos);
				freetext = MAX_NAME_LEN;
				break;
			
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
				if (view == PORT && meterec->pos.inout) {
					if (meterec->pos.inout == CON_IN)
						meterec->pos.con_in --;
					if (meterec->pos.inout == CON_OUT)
						meterec->pos.con_out --;
				} else {
					meterec->ports[meterec->pos.port].monitor = 0;
					if ( meterec->pos.port == 0 )
						meterec->pos.port = meterec->n_ports - 1;
					else
						meterec->pos.port--;
					meterec->ports[meterec->pos.port].monitor = 1;
				}
				break;
			
			case KEY_DOWN :
				if (view == PORT && meterec->pos.inout) {
					if (meterec->pos.inout == CON_IN)
						meterec->pos.con_in ++;
					if (meterec->pos.inout == CON_OUT)
						meterec->pos.con_out ++;
				} else {
					meterec->ports[meterec->pos.port].monitor = 0;
					if ( meterec->pos.port == meterec->n_ports - 1 )
						meterec->pos.port = 0;
					else 
						meterec->pos.port++;
					meterec->ports[meterec->pos.port].monitor = 1;
				}
				break;
			
			case 9: /* TAB */
				if (view==VU)
					view=EDIT;
				else if (view==EDIT) {
					view=PORT;
					retreive_connected_ports(meterec);
					retreive_existing_ports(meterec);
				}
				else if (view==PORT)
					view=VU;
				break;
			
			case 10: /* RETURN */
				if (meterec->playback_sts == ONGOING)
					stop(meterec);
				else {
					if (meterec->record_sts == OFF)
						start_record();
					if (meterec->playback_sts == OFF) 
						roll(meterec);
				}
				break;
			
			case 127: /* BACKSPACE */
			case 263: /* BACKSPACE */
				if (meterec->record_sts == ONGOING && meterec->playback_sts == ONGOING) 
					meterec->record_cmd = RESTART;
				else if (meterec->record_sts == OFF && meterec->playback_sts == OFF)
					start_record();
				else if (meterec->record_sts == ONGOING && meterec->playback_sts == OFF)
					cancel_record();
				break;
			
			case ' ':
				if (meterec->playback_sts == ONGOING && meterec->record_sts == OFF)
					stop(meterec);
				else if (meterec->playback_sts == OFF)
					roll(meterec);
				break;
			
			case '-': /* SUPR */
				clr_loop(meterec);
				break;
			
			case '+': 
				set_loop(meterec, meterec->jack.playhead);
				break;
			
			case 'Q':
			case 'q':
				meterec->keyboard_cmd = STOP;
				running = 0;
				break;
			
		}
		
		/* set index using SHIFT */
		if ( KEY_F(13) <= key && key <= KEY_F(24) ) 
			meterec->seek_index[key - KEY_F(13)] = meterec->jack.playhead ;
		
		/* set loop using CONTROL */
		if ( KEY_F(25) <= key && key <= KEY_F(36) ) {
			/* store index before setting loop if index is free */
			if (meterec->seek_index[key - KEY_F(25)] == MAX_UINT) 
				meterec->seek_index[key - KEY_F(25)] = meterec->jack.playhead ;
			
			set_loop(meterec, meterec->seek_index[key - KEY_F(25)]);
		}
		/* seek to index */
		if (!meterec->record_sts && meterec->playback_sts ) {
			
			if ( KEY_F(1) <= key && key <= KEY_F(12) ) {
				if (meterec->seek_index[key - KEY_F(1)] != MAX_UINT) {
					pthread_mutex_lock( &meterec->event_mutex );
					add_event(meterec, DISK, SEEK, MAX_UINT, meterec->seek_index[key - KEY_F(1)], MAX_UINT);
					pthread_mutex_unlock( &meterec->event_mutex );
				}
			}
			
			if ( key == KEY_HOME ) {
				pthread_mutex_lock( &meterec->event_mutex );
				add_event(meterec, DISK, SEEK, MAX_UINT, 0, MAX_UINT);
				pthread_mutex_unlock( &meterec->event_mutex );
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
	fprintf(stderr, "%s [-f freqency] [-r ref-level] [-w width] [-s session-name] [-j jack-name] [-o output-format] [-u uuid] [-t][-p][-c][-i]\n\n", progname);
	fprintf(stderr, "where  -f      is how often to update the meter per second [24]\n");
	fprintf(stderr, "       -r      is the reference signal level for 0dB on the meter [0]\n");
	fprintf(stderr, "       -w      is how wide to make the meter [auto]\n");
	fprintf(stderr, "       -s      is session name [%s]\n",meterec->session);
	fprintf(stderr, "       -j      is the jack client name [%s]\n",meterec->jack_name);
	fprintf(stderr, "       -o      is the record output format (w64, wav, flac, ogg) [%s]\n",output_ext);
	fprintf(stderr, "       -u      is the uuid value to be restored [none]\n");
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
	fprintf(stderr, "       i       insert name\n");
	fprintf(stderr, "       m       mute that port playback\n");
	fprintf(stderr, "       M       mute all ports playback\n");
	fprintf(stderr, "       s       mute all but that port playback (solo)\n");
	fprintf(stderr, "       S       unmute all ports playback\n");
	fprintf(stderr, "       r       toggle REC record mode for that port - record without listening playback\n");
	fprintf(stderr, "       R       toggle REC record mode for all ports\n");
	fprintf(stderr, "       d       toggle DUB record mode for that port - record listening playback\n");
	fprintf(stderr, "       D       toggle DUB record mode for all ports\n");
	fprintf(stderr, "       o       toggle OVR record mode for that port - record listening and mixing playback\n");
	fprintf(stderr, "       O       toggle OVR record mode for all ports\n");
	fprintf(stderr, "<SHIFT>F1-F12  set time index\n");
	fprintf(stderr, "       F1-F12  jump to time index\n");
	fprintf(stderr, " <CTRL>F1-F12  use time index as loop boundary\n");
	fprintf(stderr, "       +       use current time as loop boundary\n");
	fprintf(stderr, "       -       clear loop boundaries\n");
	fprintf(stderr, "       =>      seek forward 5 seconds\n");
	fprintf(stderr, "       <=      seek backward 5 seconds\n");
	fprintf(stderr, "       <HOME>  be kind, rewind\n");
	fprintf(stderr, "       <TAB>   edit mode\n");
	fprintf(stderr, "       l       lock selected track for playback\n");
	fprintf(stderr, "       L       lock only selected track for playback (clear all other locks on this port)\n");
	fprintf(stderr, "       a       lock selected take for playback\n");
	fprintf(stderr, "       A       lock only selected tack for playback  (clear all other locks)\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int console_width = 0; 
	int uuid = 0; 
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
	
	while ((opt = getopt(argc, argv, "r:w:f:s:j:o:u:ptchvi")) != -1) {
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
				meterec->session = optarg ;
				break;
				
			case 'j':
				meterec->jack_name = optarg ;
				break;
				
			case 'o':
				output_ext = optarg ;
				break;
				
			case 'u':
				uuid = atoi(optarg);
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
	
	post_option_init(meterec);
	
	if ( (meterec->fd_log = fopen(meterec->log_file,"w")) == NULL ) {
		fprintf(stderr,"Error: could not open '%s' for writing\n", meterec->log_file);
		exit(1);
	}
	
	fprintf(meterec->fd_log,"---- Options ----\n");
	fprintf(meterec->fd_log,"Reference level: %.1fdB\n", ref_lev);
	fprintf(meterec->fd_log,"Updates per second: %d\n", rate);
	fprintf(meterec->fd_log,"Console Width: %d\n", console_width);
	fprintf(meterec->fd_log,"Session name: %s\n", meterec->session);
	fprintf(meterec->fd_log,"Jack client name: %s\n", meterec->jack_name);
	fprintf(meterec->fd_log,"Output format: %s\n", output_ext);
	fprintf(meterec->fd_log,"%slayback at startup.\n",meterec->playback_cmd?"P":"No p");
	fprintf(meterec->fd_log,"%secording new take at startup.\n",meterec->record_cmd?"R":"Not r");
	fprintf(meterec->fd_log,"%snteract with jack transport.\n",meterec->jack_transport?"I":"Do not i");
	fprintf(meterec->fd_log,"---- Starting ----\n");
	
	/* Register with Jack */
	fprintf(meterec->fd_log, "Connecting to jackd...\n");
	
	if (uuid)
		meterec->client = jack_client_open(meterec->jack_name, JackNullOption, &status, uuid);
	else 
		meterec->client = jack_client_open(meterec->jack_name, JackNullOption, &status);
		
	if (meterec->client == 0) {
		fprintf(meterec->fd_log, "Failed to start '%s' jack client: %d\n", meterec->jack_name, status);
		fprintf(stderr,"Failed to start '%s' jack client: %d - Is jackd running?\n", meterec->jack_name, status);
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
	
	/* Register function to handle new ports */
	jack_set_port_registration_callback(meterec->client, process_port_register, meterec);
	
	/* Register session save callback */
	jack_set_session_callback(meterec->client, session_callback, meterec);
	
	/* get initial buffer size */
	meterec->jack_buffsize = jack_get_buffer_size(meterec->client);
	
	if (jack_activate(meterec->client)) {
		fprintf(meterec->fd_log, "Cannot activate client.\n");
		exit_on_error("Cannot activate client");
	}
	
	if (file_exists(meterec->conf_file)) {
		load_conf(meterec);
		if (meterec->connect_ports)
			connect_all_ports(meterec);
	} else {
		load_setup(meterec);
		load_session(meterec);
		save_conf(meterec);
		fprintf(meterec->fd_log, "Converted old configuration to %s.\n", meterec->conf_file );
		exit_on_error("Converted old configuration");
	}
	
	if (!meterec->jack.sample_rate)
		meterec->jack.sample_rate = jack_get_sample_rate(meterec->client);
	else if (meterec->jack.sample_rate != jack_get_sample_rate(meterec->client)) {
		fprintf(meterec->fd_log, "Session sample rate (%dHz) is not the same as jackd sample rate (%dHz).\n",
			meterec->jack.sample_rate,
			jack_get_sample_rate(meterec->client));
		fprintf(meterec->fd_log, "Restart jackd with %dHz saple rate or remove sample rate entry from '%s' configuration file.\n",
			meterec->jack.sample_rate,
			meterec->conf_file);
		exit_on_error("Session sample rate is not the same as jackd sample rate.");
	}
	
	meterec->config_sts = ONGOING;
	
	create_monitor_port(meterec);
	
	fprintf(meterec->fd_log, "Starting ncurses interface...\n");
	
	mainwin = initscr();
	
	if ( mainwin == NULL ) {
		fprintf(meterec->fd_log, "Error initialising ncurses.\n");
		exit(1);
	}
	
	meterec->curses_sts = ONGOING;
	
	curs_set(0);
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
	
	find_existing_takes(meterec);
	
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
	
	meterec->pos.take = meterec->n_takes;
	
	while (running) {
		
		read_peak(bias);
		
		clear();
		
		display_header(meterec, console_width);
		
		if (view==VU)
			display_meter(meterec, display_names, console_width, decay_len);
		else if (view==EDIT)	
			display_session(meterec);
		else if (view==PORT) {
			display_ports(meterec);
		}
		
		/*
		struct event_s *event;
		event = meterec->event ;
		printw("\n");
		while (event) {
			event_print(meterec, CURSES, event);
			event = event->next;
		}
		*/
		
		refresh();
		
		fsleep( 1.0f/rate );
		
	}
	
	cleanup();
	
	pthread_kill(kb_dt, SIGTERM); 
	pthread_join(kb_dt, NULL);
	
	free_scale();
	free_ports(meterec);
	free_takes(meterec);
	free_options(meterec);
	free(meterec);
	
	return 0;
	
}

