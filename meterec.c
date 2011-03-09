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


void stop(void);

WINDOW * mainwin;

int edit_mode = 0, display_names = 1;
int x_pos = 0, y_pos = 0;
  int running = 1;

static unsigned long  playhead = 0 ;

char *scale ;
char *line ;
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
  
  for (port = 0; port < meterec->n_ports; port++) {
  
    if (meterec->ports[port].input != NULL ) {

      all_ports = jack_port_get_all_connections(meterec->client, meterec->ports[port].input);

      for (i=0; all_ports && all_ports[i]; i++) {
        fprintf(meterec->fd_log,"Disconnecting input port '%s' from '%s'.\n", jack_port_name(meterec->ports[port].input), all_ports[i] );
        jack_disconnect(meterec->client, all_ports[i], jack_port_name(meterec->ports[port].input));
      }
    }
    
    if (meterec->ports[port].output != NULL ) {

      all_ports = jack_port_get_all_connections(meterec->client, meterec->ports[port].output);

      for (i=0; all_ports && all_ports[i]; i++) {
        fprintf(meterec->fd_log,"Disconnecting output port '%s' from '%s'.\n", jack_port_name(meterec->ports[port].output), all_ports[i] );
        jack_disconnect(meterec->client, all_ports[i], jack_port_name(meterec->ports[port].output));
      }
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

/* Close down JACK when exiting */
static void cleanup(int sig)
{

  stop();
  
  cleanup_jack();
  
  cleanup_curse();
    
  fclose(meterec->fd_log);

  (void) signal(SIGINT, SIG_DFL);
  
  exit(0);
  
}

void exit_on_error(char * reason) {

  stop();
  
  fprintf(meterec->fd_log, "Error: %s\n", reason);

  cleanup_jack();
  
  cleanup_curse();

  fclose(meterec->fd_log);
    
  printf("Error: %s\n", reason);
  
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
  time->frm = 
		  (unsigned int) (
		  time->h  * time->rate * 3600 +
		  time->m  * time->rate * 60 +
		  time->s  * time->rate +
		  time->ms * time->rate / 1000
		  ) ;
}

/*
  db: the signal stength in db
  width: the size of the meter
*/
static int iec_scale(float db, int size) {
  float def = 0.0f; /* Meter deflection %age */
  
  if (db < -70.0f) {
    def = 0.0f;
  } else if (db < -60.0f) {
    def = (db + 70.0f) * 0.25f;
  } else if (db < -50.0f) {
    def = (db + 60.0f) * 0.5f + 2.5f;
  } else if (db < -40.0f) {
    def = (db + 50.0f) * 0.75f + 7.5f;
  } else if (db < -30.0f) {
    def = (db + 40.0f) * 1.5f + 15.0f;
  } else if (db < -20.0f) {
    def = (db + 30.0f) * 2.0f + 30.0f;
  } else if (db < 0.0f) {
    def = (db + 20.0f) * 2.5f + 50.0f;
  } else {
    def = 100.0f;
  }
  
  return (int)( (def / 100.0f) * ((float) size) );
}


/* Sleep for a fraction of a second */
static int fsleep( float secs )
{

#ifdef HAVE_USLEEP
  return usleep( secs * 1000000ul );
#else 
  return 0;
#endif

}

/* Read and reset the recent peak sample */
void read_peak(float bias)
{
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

void compute_takes_to_playback(struct meterec_s *meterec) {

  unsigned int take, port;

  for ( port = 0; port < meterec->n_ports; port++ ) {
    
    for ( take = meterec->n_takes + 1; take > 0; take-- )
      if (meterec->takes[take].port_has_lock[port])
        break;
    
    if (!take)
      take = meterec->n_takes + 1;
      
    for ( ; take > 0; take-- )
      if (meterec->takes[take].port_has_track[port])
        break;
        
    meterec->ports[port].playback_take = take;
    
  }
}

unsigned int changed_takes_to_playback(struct meterec_s *meterec) {

  unsigned int take, port;

  for ( port = 0; port < meterec->n_ports; port++ ) {
    
    for ( take = meterec->n_takes + 1; take > 0; take-- )
      if (meterec->takes[take].port_has_lock[port])
        break;
    
    if (!take)
      take = meterec->n_takes + 1;
      
    for ( ; take > 0; take-- )
      if (meterec->takes[take].port_has_track[port])
        break;
        
    if (meterec->ports[port].playback_take != take)
      return 1;

  }
  
  return 0;
}

void compute_tracks_to_record() {
  
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

void init_ports(struct meterec_s *meterec)
{
  unsigned int port;
  
  meterec->n_ports = 0;

  for (port = 0; port < MAX_PORTS; port++) {
  
    meterec->ports[port].input = NULL;
    meterec->ports[port].output = NULL;
    
    meterec->ports[port].portmap = NULL;
    meterec->ports[port].name = NULL;
    
    meterec->ports[port].write_disk_buffer = NULL;
    meterec->ports[port].read_disk_buffer = NULL;
    meterec->ports[port].record = OFF;
    
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

    meterec->takes[take].take_fd = NULL;
    meterec->takes[take].buf = NULL;
    meterec->takes[take].info.format = 0 ; //When opening a file for read, the format field should be set to zero before calling sf_open()

    meterec->takes[take].ntrack = 0;
    
    for (track=0; track<MAX_TRACKS; track++) {
      meterec->takes[take].track_port_map[track] = 0 ;
    }
    
    for (port=0; port<MAX_PORTS; port++) {
      meterec->takes[take].port_has_track[port] = 0 ;
      meterec->takes[take].port_has_lock[port] = 0 ;
    }

  }

}

void pre_option_init(struct meterec_s *meterec) {

  meterec->n_tracks = 0;
  
  meterec->record_sts = OFF;
  meterec->record_cmd = STOP;

  meterec->playback_sts = OFF;
  meterec->playback_cmd = START;
  
  meterec->client = NULL;
  meterec->fd_log = NULL;

  meterec->write_disk_buffer_thread_pos = 0;
  meterec->write_disk_buffer_process_pos = 0;
  meterec->write_disk_buffer_overflow = 0;

  meterec->read_disk_buffer_thread_pos = 1; /* Hum... Would be better to rework thread loop... */
  meterec->read_disk_buffer_process_pos = 0;
  meterec->read_disk_buffer_overflow = 0;
}

int find_take_name(char *session, unsigned int take, char * name) {

  struct dirent *entry;
  DIR *dp;
  char *current = ".";
  
  char *pattern;
  pattern = (char *) malloc( strlen(session) + strlen("_0000.") + 1 );
  sprintf(pattern,"%s_%04d.",session,take);

  dp = opendir(current);
 
  if (dp == NULL) {
    perror("opendir");
    return -1;
  }
 
  while((entry = readdir(dp)))
    if (strncmp(entry->d_name, pattern, strlen(pattern)) == 0) {
      closedir(dp);
      free(pattern);
      free(name);
      name = (char *) malloc( strlen(entry->d_name) );
      strcpy(name, entry->d_name);
      return 1;
    }
    
  closedir(dp);
  free(pattern);
  return 0;

}

void post_option_init(struct meterec_s *meterec, char *session) {

  unsigned int take, index ;

  meterec->session_file = (char *) malloc( strlen(session) + strlen("..sess") + 1 );
  sprintf(meterec->session_file,".%s.sess",session);

  meterec->setup_file = (char *) malloc( strlen(session) + strlen(".conf") + 1 );
  sprintf(meterec->setup_file,"%s.conf",session);
  
  meterec->log_file = (char *) malloc( strlen(session) + strlen(".log") + 1 );
  sprintf(meterec->log_file,"%s.log",session);
  
  meterec->output_ext = (char *) malloc( strlen(output_ext) + 1 );
  sprintf(meterec->output_ext,"%s",output_ext);

  if (strcmp(output_ext, "wav") == 0) {
    meterec->output_fmt = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
  }
#if defined(HAVE_W64)
    else if (strcmp(output_ext, "w64") == 0) {
    meterec->output_fmt = SF_FORMAT_W64 | SF_FORMAT_PCM_24;
  }
#endif
#if defined(HAVE_VORBIS)
  else if (strcmp(output_ext, "ogg") == 0) {
    meterec->output_fmt = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
  }
  else if (strcmp(output_ext, "oga") == 0) {
    meterec->output_fmt = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
  }
  else if (strcmp(output_ext, "flac") == 0) {
    meterec->output_fmt = SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
  }
#endif
  else {
    printf("Sorry, '%s' output record format is not supported.\n",output_ext);
    exit(1);
  }

  /* this needs to be moved at config file reading time and file creation time */
  for (take=1; take<MAX_TAKES; take++) {
    meterec->takes[take].take_file = (char *) malloc( strlen(session) + strlen("_0000.????") + 1 );
    if ( find_take_name(session, take, meterec->takes[take].take_file) ) 
      printf("Found existing file '%s' for take %d\n",meterec->takes[take].take_file, take);
    else 
      sprintf(meterec->takes[take].take_file,"%s_%04d.%s",session,take, meterec->output_ext);
  }
  
  pthread_mutex_init(&meterec->seek.mutex, NULL);
    
  for (index=0; index<MAX_INDEX; index++)
     meterec->seek.index[index] = -1;

  meterec->seek.disk_playhead_target = -1;
  meterec->seek.jack_buffer_target = -1;
  meterec->seek.playhead_target = -1;

  meterec->seek.files_reopen = 0;
  meterec->seek.keyboard_lock = 0;
  
}

void init_display_scale( int width )
{

  int i=0;
  const int marks[12] = { 0, -3, -5, -10, -15, -20, -25, -30, -35, -40, -50, -60 };
  
  char * scale0 ;
  char * line0 ;
  
  scale0 = (char *) malloc( width+1+2 );
  line0  = (char *) malloc( width+1+2 );

  scale = (char *) malloc( width+1+2 );
  line  = (char *) malloc( width+1+2 );

  // Initialise the scale
  for(i=0; i<width; i++) { scale0[i] = ' '; line0[i]='_'; }
  scale0[width] = 0;
  line0[width] = 0;


  // 'draw' on each of the db marks
  for(i=0; i < 12; i++) {
    char mark[5];
    int pos = iec_scale( marks[i], width )-1;
    int spos, slen;

    // Create string of the db value
    snprintf(mark, 4, "%d", marks[i]);

    // Position the label string
    slen = strlen(mark);
    spos = pos-(slen/2);
    if (spos<0) spos=0;
    if (spos+strlen(mark)>width) spos=width-slen;
    memcpy( scale0+spos, mark, slen );

    // Position little marker
    line0[pos] = '|';
  }

  sprintf(scale,"  %s",scale0);
  sprintf(line,"  %s",line0);

  free(scale0);
  free(line0);
  
}




/******************************************************************************
** JACK callback process
*/
static int update_jack_buffsize(jack_nframes_t nframes, void *arg)
{
  meterec->jack_buffsize = nframes;
  
  return 0;
}

/* Callback called by JACK when audio is available. */
static int process_jack_data(jack_nframes_t nframes, void *arg)
{
  jack_default_audio_sample_t *in;
  jack_default_audio_sample_t *out;
  unsigned int i, port, write_pos, read_pos, remaining_write_disk_buffer, remaining_read_disk_buffer;
  unsigned int playback_sts_local, record_sts_local;
  float s;
  
  /* make sure statuses do not change during callback */
  playback_sts_local = meterec->playback_sts;
  record_sts_local = meterec->record_sts;
  
  /* check if there is a new buffer position to go to*/
  if (meterec->seek.jack_buffer_target != -1) {

    pthread_mutex_lock( &meterec->seek.mutex );

    meterec->read_disk_buffer_process_pos = meterec->seek.jack_buffer_target;

    /* if we seek because of a file re-open, compensate for what played since re-open request */
    if ( meterec->seek.files_reopen ) {
      meterec->read_disk_buffer_process_pos += (playhead - meterec->seek.playhead_target);
      meterec->read_disk_buffer_process_pos &= (DISK_SIZE - 1);
      meterec->seek.files_reopen = 0;
      meterec->seek.keyboard_lock = 0;
    } 
    else {
      /* re-align playhead value if we moved due to a simple seek */
      playhead = meterec->seek.playhead_target;
    }

    meterec->seek.playhead_target = -1;
    meterec->seek.jack_buffer_target = -1;
    
    pthread_mutex_unlock( &meterec->seek.mutex );
    
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


    if (playback_sts_local==ONGOING) {
    
      read_pos = meterec->read_disk_buffer_process_pos;

      for (i = 0; i < nframes; i++) {
      
        if (meterec->ports[port].mute) {

		  /* be silent */
		  out[i] = 0;

		} else {
		  /* Empty read disk buffer */
          out[i] = meterec->ports[port].read_disk_buffer[read_pos];

          /* update buffer pointer */
          read_pos = (read_pos + 1) & (DISK_SIZE - 1);
		}
		
        /* compute peak of input (recordable) data*/
        s = fabs(in[i] * 1.0f) ;
        if (s > meterec->ports[port].peak_in) {
          meterec->ports[port].peak_in = s;
        }
        
        /* compute peak of output (playback) data */
        s = fabs(out[i] * 1.0f) ;
        if (s > meterec->ports[port].peak_out) {
          meterec->ports[port].peak_out = s;
        }
        
      }
      
      if (record_sts_local==ONGOING) {
      
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
      
    } else {
    
      for (i = 0; i < nframes; i++) {
      
        out[i] = 0.0f ;
      
        /* compute peak */
        s = fabs(in[i] * 1.0f) ;
        if (s > meterec->ports[port].peak_in) {
          meterec->ports[port].peak_in = s;
        }
        
      } 
       
    }
        
  }


 
  if (playback_sts_local==ONGOING) {
  
    /* track buffer over/under flow -- needs rework */
    remaining_read_disk_buffer = DISK_SIZE - ((meterec->read_disk_buffer_thread_pos-meterec->read_disk_buffer_process_pos) & (DISK_SIZE-1));

    if (remaining_read_disk_buffer <= nframes)
      meterec->read_disk_buffer_overflow++;

    /* positon read pointer to end of ringbuffer */
    meterec->read_disk_buffer_process_pos = (meterec->read_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);
  
    /* update frame/time counter */
    playhead += nframes ;
    
    if (record_sts_local==ONGOING) {

      /* track buffer over/under flow */
      remaining_write_disk_buffer = DISK_SIZE - ((meterec->write_disk_buffer_process_pos-meterec->write_disk_buffer_thread_pos) & (DISK_SIZE-1));

      if (remaining_write_disk_buffer <= nframes)
        meterec->write_disk_buffer_overflow++;

      /* positon write pointer to end of ringbuffer*/
      meterec->write_disk_buffer_process_pos = (meterec->write_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);

    }
  
  } else {
  
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

/* Connect the chosen port to ours */
static void connect_any_port(jack_client_t *client, char *port_name, unsigned int port)
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
** Session
*/

void parse_port_con(FILE *fd_conf, unsigned int port)
{

  char line[1000];
  char label[100];
  char port_name[100];
  unsigned int i, u;
  
  i = fscanf(fd_conf,"%s%[^\r\n]%*[\r\n ]",label, line);
  
  meterec->ports[port].portmap = (char *) malloc( strlen(line)+1 );
  strcpy(meterec->ports[port].portmap, line);
  
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

void parse_time_index(FILE *fd_conf, unsigned int index)
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

void load_setup(char *file)
{

  FILE *fd_conf;
  char buf[2];
  unsigned int take=1, port=0, index=0;
  
  buf[1] = 0;
  
  if ( (fd_conf = fopen(file,"r")) == NULL ) {
    fprintf(meterec->fd_log,"could not open '%s' for reading\n", file);
    exit_on_error("Cannot open setup file for reading.");
  }
  
  fprintf(meterec->fd_log,"Loading '%s'\n", file);
  
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
      parse_port_con(fd_conf, port);
      
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
        parse_time_index(fd_conf, index);
        index++;
	    break;
		
	  default :
	    take++;
	}
        
  }

  fclose(fd_conf);
  
  meterec->n_ports = port ;
  
}

void load_session(char * file)
{

  FILE *fd_conf;
  char buf[2];
  unsigned int take=1, port=0, track=0;
  
  buf[1] = 0;
  
  if ( (fd_conf = fopen(file,"r")) == NULL ) {
    fprintf(meterec->fd_log,"could not open '%s' for reading\n", file);
     exit_on_error("Cannot open session file for reading.");
 }

  fprintf(meterec->fd_log,"Loading '%s'\n", file);

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
    fprintf(meterec->fd_log,"'%s' contains more ports (%d) than defined in .conf file (%d)\n", file,port,meterec->n_ports);
    exit_on_error("Session and setup not consistent");
  }
  
}


void session_tail(FILE * fd_conf) 
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

void save_session(char * file)
{
  FILE *fd_conf;
  unsigned int take, port;
  
  if ( (fd_conf = fopen(file,"w")) == NULL ) {
    fprintf(meterec->fd_log,"could not open '%s' for writing\n", file);
    exit_on_error("Cannot open session file for writing.");
  }
  
  for (port=0; port<meterec->n_ports; port++) {
  
    fprintf(fd_conf,"=");
      
    for (take=1; take<meterec->n_takes+1; take++) {
    
      if ( meterec->takes[take].port_has_track[port] ) 
        fprintf(fd_conf,"X");
      else 
        fprintf(fd_conf,"-");
        
    }
    
    if (meterec->ports[port].record)
      fprintf(fd_conf,"X");
    else 
      fprintf(fd_conf,"-");
    
    fprintf(fd_conf,"|%02d\n",port+1);
  }
  
  session_tail(fd_conf);
  
  fclose(fd_conf);

}

void save_setup(char *file)
{

  FILE *fd_conf;
  unsigned int take, port, index;
  struct time_s time;
  char time_str[14] ;
  
  time.rate = jack_get_sample_rate(meterec->client);

  if ( (fd_conf = fopen(file,"w")) == NULL ) {
    fprintf(meterec->fd_log,"could not open '%s' for writing\n", file);
    exit_on_error("Cannot open setup file for writing.");
  }
  
  for (port=0; port<meterec->n_ports; port++) {
  
    if (meterec->ports[port].record==REC)
      if (meterec->ports[port].mute)
        fprintf(fd_conf,"r");
	  else 
        fprintf(fd_conf,"R");
    else if (meterec->ports[port].record==DUB)
      if (meterec->ports[port].mute)
        fprintf(fd_conf,"d");
	  else 
        fprintf(fd_conf,"D");
    else if (meterec->ports[port].record==OVR)
      if (meterec->ports[port].mute)
        fprintf(fd_conf,"o");
	  else 
        fprintf(fd_conf,"O");
    else
      if (meterec->ports[port].mute)
        fprintf(fd_conf,"~");
	  else 
        fprintf(fd_conf,"=");
      
    for (take=1; take<meterec->n_takes+1; take++) {
      
      if ( meterec->takes[take].port_has_lock[port] )
        fprintf(fd_conf,"L");
      else 
        if ( meterec->takes[take].port_has_track[port] ) 
          fprintf(fd_conf,"X");
        else 
          fprintf(fd_conf,"-");
        
    }
    
//    if (meterec->ports[port].record)
//      fprintf(fd_conf,"X");
//    else 
//      fprintf(fd_conf,"-");

    fprintf(fd_conf,"|%02d%s\n",port+1,meterec->ports[port].portmap);
  }
    
  session_tail(fd_conf);
  
  for (index=0; index<MAX_INDEX; index++) {
	  time.frm = meterec->seek.index[index] ;
	  if ( time.frm == -1 ) {
	      fprintf(fd_conf,">           >%02d\n", index+1);
	  } 
	  else {
		  time_hms(&time);
          time_sprint(&time, time_str);
	      fprintf(fd_conf,">%s>%02d\n", time_str, index+1);
	  }
  }
  
  fclose(fd_conf);
  
}

/******************************************************************************
** THREAD Utils
*/

void start_playback() {

  compute_takes_to_playback(meterec);

  save_setup(meterec->setup_file);

  meterec->playback_cmd = START ;
  
  pthread_create(&rd_dt, NULL, (void *)&reader_thread, (void *) meterec);

}

void start_record() {

  compute_tracks_to_record();
  
  if (meterec->n_tracks) {
  
    save_session(meterec->session_file);

    meterec->record_cmd = START;

    pthread_create(&wr_dt, NULL, (void *)&writer_thread, (void *) meterec);

    while(meterec->record_sts!=ONGOING) 
      fsleep( 0.1f );

  }
  
}

void stop() {

  if (meterec->record_sts) {
    meterec->record_cmd = STOP ;
  
    fprintf(meterec->fd_log, "Waiting end of recording.\n");
    while(meterec->record_cmd || meterec->record_sts) {
      fsleep( 0.05f );
    }
    pthread_join(wr_dt, NULL);

    /* get ready for the next take */
    meterec->n_takes ++;
  }
  
  if (meterec->playback_sts) {
    meterec->playback_cmd = STOP ;

    fprintf(meterec->fd_log, "Waiting end of reading.\n");
    while(meterec->playback_cmd || meterec->playback_sts) {
      fsleep( 0.05f );
    }
    pthread_join(rd_dt, NULL);

  }
  
  save_setup(meterec->setup_file);

}

unsigned int seek(int seek_sec) {

  jack_nframes_t nframes;
  jack_nframes_t sample_rate;
  
  nframes = playhead;
  sample_rate = jack_get_sample_rate(meterec->client);
  
  fprintf(meterec->fd_log,"seek: at %d needs to seek %d (sr=%d)\n",nframes,seek_sec * sample_rate,sample_rate );
  
  if ( seek_sec < 0 )
    if ( nframes < abs( seek_sec ) * sample_rate )
      return 0;
  
  return (nframes + seek_sec * sample_rate); 
   
}


/******************************************************************************
** DISPLAYs
*/

void display_status(void) {
  
  float load;
  static float  max_load=0.0f;
  jack_nframes_t rate;
  struct time_s time;
  
  rate = jack_get_sample_rate(meterec->client);
  load = jack_cpu_load(meterec->client);
  
  time.frm = playhead ;
  time.rate = rate ;
  time_hms(&time);
  
  if (load>max_load) 
    max_load = load;
  
  printw("%dHz %d:%02d:%02d.%03d %4.1f%% (%3.1f%%) ", rate, time.h, time.m, time.s, time.ms, load , max_load);
  
  printw("[> ");
  
  if (meterec->playback_sts==OFF) 
    printw("%-8s","OFF");
  if (meterec->playback_sts==STARTING) 
    printw("%-8s","STARTING");
  if (meterec->playback_sts==ONGOING) 
    printw("%-8s","ONGOING");
  if (meterec->playback_sts==PAUSED) 
    printw("%-8s","PAUSED");

  printw("]");


  if (meterec->record_sts) 
    color_set(RED, NULL);
  
  printw("[O ");

  if (meterec->record_sts==OFF) 
    printw("%-8s","OFF");
  if (meterec->record_sts==STARTING) 
    printw("%-8s","STARTING");
  if (meterec->record_sts==ONGOING) 
    printw("%-8s","ONGOING");

  printw("]");
  
  if (meterec->record_sts==ONGOING) {
    attron(A_BOLD);
    printw(" Take %d",meterec->n_takes+1);
    attroff(A_BOLD); 
  }
    
  
  if (meterec->write_disk_buffer_overflow)
    printw(" OVERFLOWS(%d)",meterec->write_disk_buffer_overflow);

  color_set(DEFAULT, NULL);

  printw("\n");
  
}

void display_buffer(int width) {

  int wrlevel, wrsize, rdsize, i;
  static int peak_wrsize=0, peak_rdsize=0;
  static char *pedale = "|";
  
  rdsize = width * read_disk_buffer_level(meterec);
  
  if (rdsize > peak_rdsize && meterec->playback_sts == ONGOING) 
    peak_rdsize = rdsize;
  
  printw("  ");
  for (i=0; i<width-3; i++) {
    if (i == peak_rdsize-1)
      printw(":");
    else if (i > rdsize-1)
      printw("+");
    else 
      printw(" ");
  }
  printw("%sRD\n", pedale);


  if (meterec->record_sts==ONGOING && meterec->playback_sts == ONGOING) {
    printw("WR%s", pedale);
  } else {
    printw("WR-");
  }

  wrlevel = (meterec->write_disk_buffer_process_pos - meterec->write_disk_buffer_thread_pos) & (DISK_SIZE-1);
  wrsize = (width * wrlevel) / DISK_SIZE;

  if (wrsize > peak_wrsize) 
    peak_wrsize = wrsize;

  for (i=0; i<peak_wrsize; i++) {
    if (i < wrsize-1)
      printw("+");
    else if (i == peak_wrsize-1)
      printw(":");
    else 
      printw(" ");
  }
  printw("\n");

    
  if (meterec->playback_sts==ONGOING) {
    if      (*pedale=='/')
      pedale = "-";
    else if (*pedale=='-')
      pedale = "\\";
    else if (*pedale=='\\')
      pedale = "|";
    else if (*pedale=='|')
      pedale = "/";
 }

}

void display_meter( int width, int decay_len )
{
  int size_out, size_in, i;
  unsigned int port ;
  
  printw("%s\n", scale);
  printw("%s\n", line);
  
  for ( port=0 ; port < meterec->n_ports ; port++) {
        
    if ( meterec->ports[port].record ) 
      if (meterec->record_sts == ONGOING)
        color_set(RED, NULL);
      else 
        color_set(YELLOW, NULL);
    else 
      color_set(GREEN, NULL);
	
    if (y_pos == port) 
       attron(A_REVERSE);
    else 
       attroff(A_REVERSE);
	
    
    printw("%02d",port+1);
    display_port_recmode(&meterec->ports[port]);

    size_in = iec_scale( meterec->ports[port].db_in, width );
    size_out = iec_scale( meterec->ports[port].db_out, width );
    
    if (size_in > meterec->ports[port].dkmax_in)
      meterec->ports[port].dkmax_in = size_in;
      
    if (size_in > meterec->ports[port].dkpeak_in) {
      meterec->ports[port].dkpeak_in = size_in;
      meterec->ports[port].dktime_in = 0;
    } else if (meterec->ports[port].dktime_in++ > decay_len) {
      meterec->ports[port].dkpeak_in = size_in;
    }
	
    for ( i=0; i<width; i++) {

      if (display_names)
	    if (i == width/5) 
	      if (meterec->ports[port].name) {
		    printw("%s",meterec->ports[port].name);
		    i += strlen(meterec->ports[port].name);
	      }
	  
	  if (i < size_in-1) {
        printw("#");
      }
      else if ( i==meterec->ports[port].dkpeak_in-1 ) {
        printw("I");
      }
      else if ( i==meterec->ports[port].dkmax_in-1 ) {
        printw(":");
      }
      else if ( i < size_out-1 ) {
        printw("-");
      }
      else {
        printw(" ");
      }
      
    }
	
    printw("\n");

  }
  
  attroff(A_REVERSE);
  color_set(DEFAULT, NULL);
  printw("%s\n", line);
  printw("%s\n", scale);
  
  printw("  Port %2d ", y_pos+1);
  display_port_info( &meterec->ports[y_pos] );
   

}


/******************************************************************************
** KEYBOARD
*/

int keyboard_thread(void *d)
{

  unsigned int port, take;
  int key = 0;

  noecho();
  cbreak();
  nodelay(stdscr, FALSE);
  keypad(stdscr, TRUE);
  
  while (1) {

    key = wgetch(stdscr);

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

          if (changed_takes_to_playback(meterec) 
              && (meterec->playback_sts != OFF)) {
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

          if (changed_takes_to_playback(meterec)
              && (meterec->playback_sts != OFF)) {
        	pthread_mutex_lock( &meterec->seek.mutex );
        	meterec->seek.disk_playhead_target = playhead;
        	meterec->seek.files_reopen = 1;
        	meterec->seek.keyboard_lock = 1;
        	pthread_mutex_unlock( &meterec->seek.mutex );
          }
          break;
    	}

      }

    } else {

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
        if ( y_pos > 0 )
          y_pos--;
        break;
        
      case KEY_DOWN :
        if ( y_pos < meterec->n_ports - 1 )
          y_pos++;
        break;
      
      case 9: /* TAB */
        edit_mode = !edit_mode ;
        break;
    
      case 10: /* RETURN */
        if (meterec->playback_sts == ONGOING)
          stop();
        else if (meterec->playback_sts == OFF) {
          start_record();    
          start_playback();
        }
        break;
        
      case ' ':
        if (meterec->playback_sts == ONGOING && meterec->record_sts == OFF)
          stop();
        else if (meterec->playback_sts == OFF)
          start_playback();
        break;
	
	  /* exit */
      case 'Q':
      case 'q':
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
static int usage( const char * progname )
{
  fprintf(stderr, "version %s\n\n", VERSION);
  fprintf(stderr, "%s [-f freqency] [-r ref-level] [-w width] [-s session-name] [-j jack-name] [-o output-format] [-t]\n\n", progname);
  fprintf(stderr, "where  -f      is how often to update the meter per second [24]\n");
  fprintf(stderr, "       -r      is the reference signal level for 0dB on the meter [0]\n");
  fprintf(stderr, "       -w      is how wide to make the meter [auto]\n");
  fprintf(stderr, "       -s      is session name [%s]\n",session);
  fprintf(stderr, "       -j      is the jack client name [%s]\n",jackname);
  fprintf(stderr, "       -o      is the record output format (w64, wav) [%s]\n",output_ext);
  fprintf(stderr, "       -t      record a new take at start\n");
  fprintf(stderr, "\n\n");
  fprintf(stderr, "Command keys:\n");
  fprintf(stderr, "       <SPACE> start playback; stop\n");
  fprintf(stderr, "       <ENTER> start record; stop\n");
  fprintf(stderr, "       v       reset maximum level vu-meter markers\n");
  fprintf(stderr, "       q       quit\n");
  fprintf(stderr, "       m       mute that port playback\n");
  fprintf(stderr, "       M       mute all ports playback\n");
  fprintf(stderr, "       s       mute all but that port playback (solo)\n");
  fprintf(stderr, "       r       toggle REC record mode for that port - record without listening playback\n");
  fprintf(stderr, "       d       toggle DUB record mode for that port - record listening playback\n");
  fprintf(stderr, "       o       toggle OVR record mode for that port - record listening and mixing playback\n");
  fprintf(stderr, "       <TAB>   edit mode\n");
  fprintf(stderr, "       l       toggle lock for that position\n");
  fprintf(stderr, "       L       clear all locks for that port, toggle lock for that position\n");
  fprintf(stderr, "       a       toggle lock for all ports for that take\n");
  fprintf(stderr, "       A       clear all locks, toggle lock for all ports for that take\n");
  fprintf(stderr, "<SHIFT>F1-F12  set time index\n");
  fprintf(stderr, "       F1-F12  Jump to time index\n");
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

    while ((opt = getopt(argc, argv, "w:f:s:j:o:thv")) != -1) {
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
      case 'h':
      case 'v':
      default:
        /* Show usage/version information */
        usage( argv[0] );
        break;
    }
  }

  /* init vars that rely on a changable option */
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
  fprintf(meterec->fd_log,"%secording new take at startup.\n",meterec->record_cmd?"R":"Not r");

  fprintf(meterec->fd_log,"---- Starting ----\n");
  
  fprintf(meterec->fd_log, "Starting ncurses interface...\n");

  mainwin = initscr();
  
  if ( mainwin == NULL ) {
    fprintf(meterec->fd_log, "Error initialising ncurses.\n");
    exit(1);
  }

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
  init_display_scale(console_width - 3);

  /* Register with Jack */
  if ((meterec->client = jack_client_open(jackname, JackNullOption, &status)) == 0) {
    fprintf(meterec->fd_log, "Failed to start '%s' jack client: %d\n", jackname, status);
    exit_on_error("Failed to start jack client. Is jackd running?");
  }
  fprintf(meterec->fd_log,"Registered as '%s'.\n", jack_get_client_name( meterec->client ) );

  /* Register the signal process callback */
  jack_set_process_callback(meterec->client, process_jack_data, 0);

  /* Register function to handle buffer size change */
  jack_set_buffer_size_callback(meterec->client, update_jack_buffsize, 0);
  
  /* get initial buffer size */
  meterec->jack_buffsize = jack_get_buffer_size(meterec->client);
  
  if (jack_activate(meterec->client)) {
    fprintf(meterec->fd_log, "Cannot activate client.\n");
    exit_on_error("Cannot activate client");
  }

  load_setup(meterec->setup_file);

  load_session(meterec->session_file);
  
  pthread_create(&kb_dt, NULL, (void *)&keyboard_thread, (void *) meterec);

  /* Start threads doing disk accesses */
  if (meterec->record_cmd==START) {
  
    start_record();    
    start_playback();
  
  }
  else if (meterec->playback_cmd==START) {
  
    start_playback();
  
  }
  
  /* Register the cleanup function to be called when C-c */
  signal(SIGINT, cleanup);

  
  x_pos = meterec->n_takes;

  while (running) {
  
    read_peak(bias);
  
    clear();

    display_status();

    display_buffer(console_width - 3);
    
    if (edit_mode)
      display_session(meterec, y_pos, x_pos);
    else
      display_meter(console_width - 3 , decay_len);
    
    refresh();
    
    fsleep( 1.0f/rate );
    
  }
  
  cleanup(0);
  pthread_join(kb_dt, NULL);
  
  return 0;
}

