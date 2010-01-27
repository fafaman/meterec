/*

	meterec.c 
	Console based multi track digital peak meter and recorder for JACK
	Copyright (C) 2009 Fabrice Lebas
  
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

/* maximum number of ports - no known limit, only extra memory used */
#define MAX_PORTS 24

/* maximum number of tracks - no known limit, only extra memory used */
#define MAX_TRACKS MAX_PORTS

/* maximum number of takes - no known limit, only extra memory used */
#define MAX_TAKES 20

/* size of disk wait buffer, must be power of two */
#define DISK_SIZE 131072

/* size of disk write buffer */
#define BUF_SIZE 4096

/* command */
#define NO 0
#define START 1
#define RESTART 2
#define STOP 3

/* status */
#define OFF 0
#define STARTING 1
#define ONGOING 2
#define STARVING 3
#define STOPING 4

/* type of recording */
#define REC 1
#define DUB 2
#define OVR 4
#define MAX_REC OVR*2

/* colors */
#define DEFAULT 0
#define GREEN 1
#define YELLOW 2
#define RED 3

WINDOW * mainwin;

jack_client_t *client = NULL;

float bias = 1.0f;
int decay_len;

static unsigned long  total_nframes = 0 ;

unsigned int dump_initial_periods = 100;

char *scale ;
char *line ;
char *session = "meterec";
char *session_file;
char *setup_file;
char *jackname = "meterec" ;

unsigned int thread_delay = 10000; // in us

static unsigned int write_disk_buffer_thread_pos = 0;
static unsigned int write_disk_buffer_process_pos = 0;
static unsigned int write_disk_buffer_overflow = 0;

static unsigned int read_disk_buffer_thread_pos = 1; /* Hum... Would be better to rework thread loop... */
static unsigned int read_disk_buffer_process_pos = 0;
static unsigned int read_disk_buffer_overflow = 0;

unsigned int record_sts = OFF;
unsigned int record_cmd = NO;

unsigned int playback_sts = OFF;
unsigned int playback_cmd = START;


/*
note : 
- take 0 is before the session start, there will never be data in take 0
- track 0 is the first track, displayed as 1 
*/

/*
note : 
- a session contains several takes
- a take contains one or several tracks that where recorded at the same time.
  - in a take, the number of tracks will vary depending on the number of ports beeing recorded during that take.
  - in a take, the number of tracks beeing used for playback can be less than the total number of tracks in the take.
- current take contains tracks beeing recorded during this run.
- port refers to jack in/out port.
  - an in port can be recorded during the current take. it will then be a track of this take, potentially played back duing next(s) take(s).
  - an out port can playback previous take track, as long as this previous take contains a track mapped to that port.


=PP-----|01
=PP-----|02
=-P----P|03
=-P-----|04
=--PP---|05
=--PP---|06
=----P--|07
=-----PP|08recording
01234567

take 0 cannot contain any track. This is the state of the session when first started.
take 1 contains 2 tracks that are mapped on port 1 and 2
take 2 contains 4 tracks that are mapped on port 1, 2, 3 and 4
take 3 contains 2 tracks that are mapped on port 5 and 6
take 4 contains 2 tracks that are mapped on port 5 and 6
take 5 contains 1 track that is mapped on port 7
take 6 contains 1 track that is mapped on port 8
take 7 contains 2 tracks that are mapped on port 8 and 3

*/


struct take_s
{
  unsigned int ntrack; /* number of tracks in this take */
  
  unsigned int track_port_map[MAX_TRACKS]; /* track maps to a port : track_port_map[track] = port */
  unsigned int port_has_track[MAX_PORTS]; /* port has a track assigned : port_has_track[port] = 1/0 */ 
  unsigned int port_has_lock[MAX_PORTS]; /* port is marked locked for playback on this take : port_has_lock[port] = 1/0 */

  char *take_file;
  SNDFILE *take_fd;
  SF_INFO info;
	
	float *buf ;
 
};

struct take_s takes[MAX_TAKES]; /* table containing info of previous takes */

struct port_s
{

  jack_port_t *input;
  jack_port_t *output;
  
  char *portmap;
  
  float *write_disk_buffer;
  float *read_disk_buffer;
  
  float peak_in;
  float peak_out;

  float db_in;
  float db_out;
  
  int max_in;

  int dkpeak_in;
  int dktime_in;
  
  int record;

  unsigned int playback_take;
  unsigned int take_track_playback[MAX_TAKES];

};

struct port_s ports[MAX_PORTS];

unsigned int n_ports = 0; /* beurk */
unsigned int n_takes = 0; /* number of takes before current take */
unsigned int n_tracks = 0; /* number of tracks to be recorded during current take */


/******************************************************************************
** UTILs
*/

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
void read_peak(void)
{
	unsigned int port;
        
  for (port = 0; port < n_ports; port++) {

	  ports[port].db_in = 20.0f * log10f( ports[port].peak_in * bias ) ;
	  ports[port].peak_in = 0.0f;
    
	  ports[port].db_out = 20.0f * log10f( ports[port].peak_out * bias ) ;
	  ports[port].peak_out = 0.0f;
    
  }
  
}

/******************************************************************************
** INITs
*/

void init()
{
  unsigned int port, take, track ;
  
  for (port = 0; port < MAX_PORTS; port++) {
  
    ports[port].input = NULL;
    ports[port].output = NULL;
    
    ports[port].portmap = NULL;
    
    ports[port].write_disk_buffer = NULL;
    ports[port].read_disk_buffer = NULL;
    ports[port].record = 0;
    
    ports[port].peak_out = 0.0f;
    ports[port].db_out = 0.0f;
    
    ports[port].peak_in = 0.0f;
    ports[port].db_in = 0.0f;
    
    ports[port].dkpeak_in = 0;
    ports[port].dktime_in = 0;
    ports[port].max_in = 0;
    
    ports[port].playback_take = 0;
    
  }


  for (take=0; take<MAX_TAKES; take++) {

    takes[take].take_fd = NULL;
    takes[take].buf = NULL;
    takes[take].info.format = 0 ; //When opening a file for read, the format field should be set to zero before calling sf_open()

    takes[take].ntrack = 0;
    
    for (track=0; track<MAX_TRACKS; track++) {
      takes[take].track_port_map[track] = 0 ;
    }
    
    for (port=0; port<MAX_PORTS; port++) {
      takes[take].port_has_track[port] = 0 ;
      takes[take].port_has_lock[port] = 0 ;
    }

  }

}

void post_option_init(void) {

  unsigned int take ;
  
  session_file = (char *) malloc( strlen(session) + strlen("..sess") + 1 );
  sprintf(session_file,".%s.sess",session);

  setup_file = (char *) malloc( strlen(session) + strlen(".conf") + 1 );
  sprintf(setup_file,"%s.conf",session);
  
  for (take=0; take<MAX_TAKES; take++) {
    takes[take].take_file = (char *) malloc( strlen(session) + strlen("_0000.w64") + 1 );
    sprintf(takes[take].take_file,"%s_%04d.w64",session,take);
  }
   
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
** THREADs
*/

int writer_thread(void *d)
{
    unsigned int i, port, opos, track;
    SNDFILE *out;
    SF_INFO info;
    float buf[BUF_SIZE * MAX_PORTS];
    char *take_file ;

		record_sts = STARTING ;
    
    fprintf(stderr, "Writer thread: Started.\n");

    /* Open the output file */
    info.format = SF_FORMAT_W64 | SF_FORMAT_PCM_24 ; 
    info.channels = n_tracks;
    info.samplerate = jack_get_sample_rate(client);
    

    if (!sf_format_check(&info)) {
	    fprintf(stderr, PACKAGE ": Output file format error\n");
      record_sts = OFF;
      return 0;
    }
    
    take_file = (char *) malloc( strlen(session) + strlen("_0000.w64") + 1 );
    sprintf(take_file, "%s_%04d.w64", session,n_takes + 1);
    
    out = sf_open(take_file, SFM_WRITE, &info);
    if (!out) {
      perror("Writer thread: Cannot open file for writing");
      record_sts = OFF;
      return 0;
    }
    
    printf("Writer thread: Opened  %d track(s) file '%s' for writing.\n", n_tracks, take_file);
    
    free(take_file);

    /* Start writing the RT ringbuffer to disk */
    record_sts = ONGOING ;
    opos = 0;
    while (record_cmd == START) {
    
	    for (i  = write_disk_buffer_thread_pos; 
           i != write_disk_buffer_process_pos && opos < BUF_SIZE;
	         i  = (i + 1) & (DISK_SIZE - 1), opos++ ) {
 	      
        track = 0;
        for (port = 0; port < n_ports; port++) {
          if (ports[port].record) {
            buf[opos * n_tracks + track] = ports[port].write_disk_buffer[i]; 
            track++;
          }
	      }
        
	    }
      
	    sf_writef_float(out, buf, opos);

	    write_disk_buffer_thread_pos = i;
	    opos = 0;
      
	    usleep(thread_delay);
      
    }
    
    
    sf_close(out);

    printf("Writer thread: done.\n");

    record_sts = OFF;

    return 0;
}

int reader_thread(void *d)
{
    unsigned int i, ntrack=0, track, port, take, opos, fill;
    
    playback_sts = STARTING ;
    
		fprintf(stderr, "Reader thread: started.\n");

    /* open all files needed for this session */
    for (port=0; port<n_ports; port++) {
 
      
      take = ports[port].playback_take;
      
      /* do not open a file for a port that wants to playback take 0 */
      if (!take ) {
      
        fprintf(stderr,"Reader thread: Port %d does not have a take associated\n", port+1);

        /* rather fill buffer with 0's */
        for (i=0; i<DISK_SIZE; i++) 
          ports[port].read_disk_buffer[i] = 0.0f ;
          
        continue;
      }
      
      /* do not open a file for a port that wants to be recoded in REC mode */
      if (ports[port].record==REC && record_cmd==START) {
      
        fprintf(stderr,"Reader thread: Port %d beeing recorded in REC mode will not have a take associated\n", port+1);

        /* rather fill buffer with 0's */
        for (i=0; i<DISK_SIZE; i++) 
          ports[port].read_disk_buffer[i] = 0.0f ;
          
        continue;
        
      }
      
      fprintf(stderr,"Reader thread: Port %d has take %d associated\n", port+1, take );
      
      /* only open a take file that is not defined yet  */  
      if (takes[take].take_fd == NULL) {
        takes[take].take_fd = sf_open(takes[take].take_file, SFM_READ, &takes[take].info);
      
        /* check file is (was) opened properly */
        if (takes[take].take_fd == NULL) {
          perror("Reader thread: Cannot open file for reading");
          exit(1);
        }
				
        fprintf(stderr,"Reader thread: Opened '%s' for reading\n", takes[take].take_file);
        
        //TODO check the number of channels vs number of tracks

        /* allocate buffer space for this take */
        fprintf(stderr,"Reader thread: Allocating local buffer space %d*%d for take %d\n", takes[take].ntrack, BUF_SIZE, take);
        takes[take].buf = calloc(BUF_SIZE*takes[take].ntrack, sizeof(float));

		  } 
      else {
        fprintf(stderr,"Reader thread: File and buffer already setup.\n");
      }
      
    }
    
    /* Start reading disk to fill the RT ringbuffer */
    opos = 0;
    while ( playback_cmd==START )  {
	
    /* load the local buffer */
    for(take=1; take<n_takes+1; take++) {
      
      /* check if track is used */
      if (takes[take].take_fd == NULL)
        continue;
				
      /* get the number of tracks in this take */
      ntrack = takes[take].ntrack;
    
      /* lets fill local buffer only if previously emptied*/
      if (opos == 0) {
      
        fill = sf_read_float(takes[take].take_fd, takes[take].buf, (BUF_SIZE * ntrack) ); 
    
        /* complete buffer with 0's if reached end of file */
        for ( ; fill<(BUF_SIZE * ntrack); fill++) 
          takes[take].buf[fill] = 0.0f;
      } 

    }
    
    /* walk in the local buffer and copy it to each port buffers (demux)*/
	  for (i  = read_disk_buffer_thread_pos; 
         i != read_disk_buffer_process_pos && opos < BUF_SIZE;
	       i  = (i + 1) & (DISK_SIZE - 1), opos++ ) {
         
      for(take=1; take<n_takes+1; take++) {

				
				/* check if take is used */
        if (takes[take].take_fd == NULL)
          continue;
					
				ntrack = takes[take].ntrack;

        /* for each track belonging to this take */
        for (track=0; track<ntrack; track++) {

          /* find what port is mapped to this track */
          port = takes[take].track_port_map[track] ;

          /* check if this port needs data from this take */
          if (ports[port].playback_take == take)
            /* Only fill buffer if in playback, dub or overdub */
            if (ports[port].record != REC || !record_cmd)
            ports[port].read_disk_buffer[i] = takes[take].buf[opos * ntrack + track] ;
						
        }
        
      }
      
    }
      
	  read_disk_buffer_thread_pos = i;
					  
		if ( playback_sts==STARTING && (read_disk_buffer_thread_pos > (DISK_SIZE*4/5)))
		  playback_sts=ONGOING;

    
    if (opos == BUF_SIZE) 
      opos = 0;
			
    usleep(thread_delay);
      
    }
    
    /* close all fd's */
    for (take=1; take<n_takes+1; take++) 
      if (takes[take].take_fd)
        sf_close(takes[take].take_fd);

    fprintf(stderr,"Reader thread: done.\n");

    playback_sts = OFF;

    return 0;
}


/******************************************************************************
** JACK callback process
*/

/* Callback called by JACK when audio is available. */
static int process_jack_data(jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in;
	jack_default_audio_sample_t *out;
	unsigned int i, port, write_pos, read_pos, remaining_write_disk_buffer, remaining_read_disk_buffer;
  float s;
    
	/* get the audio samples, and find the peak sample */
  for (port = 0; port < n_ports; port++) {

	  /* just in case the port isn't registered yet */
	  if (ports[port].input == NULL) 
      continue;

	  /* just in case the port isn't registered yet */
	  if (ports[port].output == NULL) 
      continue;

    out = (jack_default_audio_sample_t *) jack_port_get_buffer(ports[port].output, nframes);
    in = (jack_default_audio_sample_t *) jack_port_get_buffer(ports[port].input, nframes);

    if ( (record_sts==ONGOING || record_cmd==NO) && playback_sts==ONGOING) {
  
      write_pos = write_disk_buffer_process_pos;
      read_pos = read_disk_buffer_process_pos;
      for (i = 0; i < nframes; i++) {

        // Empty read disk buffer
        out[i] = ports[port].read_disk_buffer[read_pos];
        read_pos = (read_pos + 1) & (DISK_SIZE - 1);

        // Fill write disk buffer
        if (record_sts==ONGOING && ports[port].record) {
          if (ports[port].record==OVR) 
            ports[port].write_disk_buffer[write_pos] = in[i] + out[i];
          else 
            ports[port].write_disk_buffer[write_pos] = in[i];
            
          write_pos = (write_pos + 1) & (DISK_SIZE - 1);
        }

        /* compute peak */
		    s = fabs(in[i] * 1.0f) ;
		    if (s > ports[port].peak_in) {
			    ports[port].peak_in = s;
		    }
        
		    s = fabs(out[i] * 1.0f) ;
		    if (s > ports[port].peak_out) {
			    ports[port].peak_out = s;
		    }
        
	    }
			
    }
    else {
      
      // fill output with silence while disk threads are not ready
      for (i = 0; i < nframes; i++) 
        out[i] = 0.0f ;
        
    }
    
  }


  if (record_sts==ONGOING && n_tracks) {
    
    remaining_write_disk_buffer = DISK_SIZE - ((write_disk_buffer_process_pos-write_disk_buffer_thread_pos) & (DISK_SIZE-1));
    
    if (remaining_write_disk_buffer <= nframes)
      write_disk_buffer_overflow++;

  }
  
  
  // needs rework
  remaining_read_disk_buffer = DISK_SIZE - ((read_disk_buffer_thread_pos-read_disk_buffer_process_pos) & (DISK_SIZE-1));
    
  if (remaining_read_disk_buffer <= nframes)
    read_disk_buffer_overflow++;

 
 
  if ((record_sts==ONGOING || record_cmd==NO) && playback_sts==ONGOING) {

		// positon write pointer to end of ringbuffer
		write_disk_buffer_process_pos = (write_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);

		// positon read pointer to end of ringbuffer
		read_disk_buffer_process_pos = (read_disk_buffer_process_pos + nframes) & (DISK_SIZE - 1);

		total_nframes +=  nframes ;
    
  }
 
   
	return 0;
}
/******************************************************************************
** PORTS
*/

void create_input_port(unsigned int port) {
  
  char port_name[10] ;
  
	sprintf(port_name,"in_%d",port+1);

  fprintf(stderr,"Creating input port '%s'.\n", port_name );

  if (!(ports[port].input = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
		fprintf(stderr, "Cannot register input port '%s'.\n",port_name);
		exit(1);
	}
  
}

void create_output_port(unsigned int port) {
  
  char port_name[10] ;

	sprintf(port_name,"out_%d",port+1);

  fprintf(stderr,"Creating output port '%s'.\n", port_name );

  if (!(ports[port].output = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(stderr, "Cannot register output port '%s'.\n",port_name);
		exit(1);
	}
  
}

/* Connect the chosen port to ours */
static void connect_any_port(jack_client_t *client, char *port_name, unsigned int port)
{
	jack_port_t *jack_port;
  int jack_flags;

  /* connect input port*/
  
	// Get the port we are connecting to
	jack_port = jack_port_by_name(client, port_name);
  
  // Check if port exists
	if (jack_port == NULL) {
		fprintf(stderr, "Can't find port '%s'\n", port_name);
		exit(1);
	}

  /* check port flags */
  jack_flags = jack_port_flags(jack_port);
  
  if ( jack_flags & JackPortIsInput ) {
  
	  // Connect the port to our output port
	  fprintf(stderr,"Connecting '%s' to '%s'...\n", jack_port_name(ports[port].output), jack_port_name(jack_port));
	  if (jack_connect(client, jack_port_name(ports[port].output), jack_port_name(jack_port))) {
		  fprintf(stderr, "Cannot connect port '%s' to '%s'\n", jack_port_name(ports[port].output), jack_port_name(jack_port));
		  exit(1);
	  }

  }
  
  if ( jack_flags & JackPortIsOutput ) {
  
	  // Connect the port to our input port
	  fprintf(stderr,"Connecting '%s' to '%s'...\n", jack_port_name(jack_port), jack_port_name(ports[port].input));
	  if (jack_connect(client, jack_port_name(jack_port), jack_port_name(ports[port].input))) {
		  fprintf(stderr, "Cannot connect port '%s' to '%s'\n", jack_port_name(jack_port), jack_port_name(ports[port].input));
		  exit(1);
	  }

  }
  
}

/* Close down JACK when exiting */
static void cleanup(int sig)
{
	const char **all_ports;
	unsigned int i, port;

  if (record_sts)
    record_cmd = STOP ;
  
  if (playback_sts)
    playback_cmd = STOP ;
  

  delwin(mainwin);

  endwin();

  refresh();

  fprintf(stderr, "Stopped ncurses interface.\n");


  for (port = 0; port < n_ports; port++) {
  
	  if (ports[port].input != NULL ) {

		  all_ports = jack_port_get_all_connections(client, ports[port].input);

		  for (i=0; all_ports && all_ports[i]; i++) {
      	fprintf(stderr,"Disconnecting input port '%s' from '%s'.\n", jack_port_name(ports[port].input), all_ports[i] );
			  jack_disconnect(client, all_ports[i], jack_port_name(ports[port].input));
		  }
	  }
    
	  if (ports[port].output != NULL ) {

		  all_ports = jack_port_get_all_connections(client, ports[port].output);

		  for (i=0; all_ports && all_ports[i]; i++) {
      	fprintf(stderr,"Disconnecting output port '%s' from '%s'.\n", jack_port_name(ports[port].output), all_ports[i] );
			  jack_disconnect(client, all_ports[i], jack_port_name(ports[port].output));
		  }
	  }
        
  }

	/* Leave the jack graph */
	jack_client_close(client);
  

  fprintf(stderr, "Waiting end of reading.");
  while(playback_cmd && playback_sts!=OFF) {
    fprintf(stderr, ".");
    fsleep( 0.25f );
  }
  fprintf(stderr, " Done.\n");


  fprintf(stderr, "Waiting end of recording.");
  while(record_cmd && record_sts!=OFF) {
    fprintf(stderr, ".");
    fsleep( 0.25f );
  }
  fprintf(stderr, " Done.\n");


  (void) signal(SIGINT, SIG_DFL);
  
  exit(0);
  
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
  
  fscanf(fd_conf,"%s%[^\r\n]%*[\r\n ]",label, line);
  
  ports[port].portmap = (char *) malloc( strlen(line)+1 );
  strcpy(ports[port].portmap, line);
  
//  fprintf(stderr,"Port %d needs to connect to\n", port+1);

  i = 0;
  while ( sscanf(line+i,"%s%n",port_name,&u ) ) {

//    fprintf(stderr,"  - '%s'(%s)\n", port_name, line+i);

    connect_any_port(client, port_name, port);

    i+=u;
    
    while(line[i] == ' ')
     i++;
    
    if (line[i] == '\0')
      break;
        
  }
  
}

void load_setup(char *file)
{

  FILE *fd_conf;
  char buf[2];
  unsigned int take=1, port=0;
  
  buf[1] = 0;
  
  if ( (fd_conf = fopen(file,"r")) == NULL ) {
    fprintf(stderr,"ERROR: could not open '%s' for reading\n", file);
    exit(1);
  }
  
  fprintf(stderr,"Loading '%s'\n", file);
  
  while ( fread(buf, sizeof(char), 1, fd_conf) ) {
    
    if (*buf == 'L' || *buf == 'l') {
      fprintf(stderr,"Playback LOCK on Port %d take %d\n", port+1,take);
			takes[take].port_has_lock[port] = 1 ;
    }
    
    
    /* new port / new take */
    
    if (*buf == '|') {
		
      // allocate memory for this port
	    ports[port].read_disk_buffer = calloc(DISK_SIZE, sizeof(float));
	    ports[port].write_disk_buffer = calloc(DISK_SIZE, sizeof(float));
			
      // create input ports
      create_input_port ( port );
      create_output_port ( port );
			
      // connect to other ports
      parse_port_con(fd_conf, port);
			
      port++;
    }
    
    if (*buf == '=') {
      take=1;
    }
    else if (*buf == 'R' || *buf == 'r') {
      ports[port].record = REC;
      n_tracks++;
      take=1;
    }
    else if (*buf == 'D' || *buf == 'd') {
      ports[port].record = DUB;
      n_tracks++;
      take=1;
    }
    else if (*buf == 'O' || *buf == 'o') {
      ports[port].record = OVR;
      n_tracks++;
      take=1;
    } else {
      take++;
    }
        
  }

  fclose(fd_conf);
  
	n_ports = port ;
  
}

void load_session(char * file)
{

  FILE *fd_conf;
  char buf[2];
  unsigned int take=1, port=0, track=0, i;
  
  buf[1] = 0;
  
  if ( (fd_conf = fopen(file,"r")) == NULL ) {
    fprintf(stderr,"ERROR: could not open '%s' for reading\n", file);
    exit(1);
  }

  fprintf(stderr,"Loading '%s'\n", file);

  while ( fread(buf, sizeof(char), 1, fd_conf) ) {
    
    if (*buf == 'X') {
      track = takes[take].ntrack ;
      takes[take].track_port_map[track] = port ;
      takes[take].port_has_track[port] = 1 ;
      takes[take].ntrack++;
      
      ports[port].playback_take = take ;
      ports[port].take_track_playback[take] = track ;
      
    }

    /* new port / new take */
    if (*buf == '|') {
		  
			
			/* look for a lock along the takes for this port */
		  i = take+1;
		  while (i--)
			  if (takes[i].port_has_lock[port])
				  break;
			
			i++;
			
			/* look for latest take at or before that lock */
			while (i--) {
		    if (takes[i].port_has_track[port]) {
				  ports[port].playback_take = i;
				  break;
				}
				ports[port].playback_take = 0;
		  }
		
      port++;
      n_takes = take - 1;
    }
		
    if (*buf == '=') 
      take=1;
    else 
			take++;	
    
            
  }
    
  fclose(fd_conf);
  
  if (port>n_ports) {
    fprintf(stderr,"ERROR: '%s' contains more ports (%d) than defined in .conf file (%d)\n", file,port,n_ports);
    exit(1);
  }
	
}


void session_tail (FILE * fd_conf) 
{
  unsigned int ntakes, take, step=1, i;
  char *spaces;
  
  ntakes = n_takes+1;

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
    fprintf(stderr,"ERROR: could not open '%s' for writing\n", file);
    exit(1);
  }
  
  for (port=0; port<n_ports; port++) {
  
    fprintf(fd_conf,"=");
      
    for (take=1; take<n_takes+1; take++) {
    
      if ( takes[take].port_has_track[port] ) 
        fprintf(fd_conf,"X");
      else 
        fprintf(fd_conf,"-");
        
    }
    
    if (ports[port].record)
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
  unsigned int take, port;

  if ( (fd_conf = fopen(file,"w")) == NULL ) {
    fprintf(stderr,"ERROR: could not open '%s' for writing\n", file);
    exit(1);
  }
  
  for (port=0; port<n_ports; port++) {
  
    if (ports[port].record==REC)
      fprintf(fd_conf,"R");
    else if (ports[port].record==DUB)
      fprintf(fd_conf,"D");
    else if (ports[port].record==OVR)
      fprintf(fd_conf,"O");
    else
      fprintf(fd_conf,"=");
      
    for (take=1; take<n_takes+1; take++) {
      
      if ( takes[take].port_has_lock[port] )
        fprintf(fd_conf,"L");
      else 
        if ( takes[take].port_has_track[port] ) 
          fprintf(fd_conf,"X");
      else 
        fprintf(fd_conf,"-");
        
    }
    
    if (ports[port].record)
      fprintf(fd_conf,"X");
    else 
      fprintf(fd_conf,"-");

    fprintf(fd_conf,"|%02d%s\n",port+1,ports[port].portmap);
  }
    
  session_tail(fd_conf);
  
  fclose(fd_conf);
  
}



/******************************************************************************
** DISPLAYs
*/

void display_session(int y_pos, int x_pos) 
{
  unsigned int take, port;

  /* y - port */
  /* x - take */


  printw("  Port %2d ", y_pos+1);
  if (ports[y_pos].record==REC)
     printw("[REC]");
  else if (ports[y_pos].record==OVR)
     printw("[OVR]");
  else if (ports[y_pos].record==DUB)
     printw("[DUB]");
  else 
     printw("[   ]");
     
  if ( ports[y_pos].playback_take && ( ports[y_pos].record == OVR || ports[y_pos].record == DUB )) 
    printw(" PLAYING Take %2d", ports[y_pos].playback_take);

   printw("\n");
  
  
  printw("  Take %2d ",x_pos);
  printw("%s", (ports[y_pos].playback_take == x_pos)?"[PLAYING]":"[       ]" );
  printw("%s", takes[x_pos].port_has_track[y_pos]?"[CONTENT]":"[       ]" );
  printw("%s\n", takes[x_pos].port_has_lock[y_pos]?"[LOCKED]":"[      ]" );

  for (port=0; port<n_ports; port++) {
  
    if (ports[port].record) 
      color_set(YELLOW, NULL);
    else 
      color_set(DEFAULT, NULL);
    
    if (y_pos == port) 
       attron(A_REVERSE);
    else 
       attroff(A_REVERSE);
  
    printw("%02d ",port+1,ports[port].playback_take);

    if ( ports[port].record == REC )
      printw("R");
    else if ( ports[port].record == DUB )
      printw("D");
    else if ( ports[port].record == OVR )
      printw("O");
    else 
      printw("=");
      
    for (take=1; take<n_takes+1; take++) {
    
      if ((y_pos == port) || (x_pos == take))
         attron(A_REVERSE);
      else 
         attroff(A_REVERSE);

      if ((y_pos == port) && (x_pos == take))
         attroff(A_REVERSE);

      if ( takes[take].port_has_lock[port] )
        printw("L");
      else if ( ports[port].playback_take == take ) 
        printw("P");
      else if ( takes[take].port_has_track[port] ) 
        printw("X");
      else 
        printw("-");
              
    }

    printw("\n");
  }
  
  attroff(A_REVERSE);
  color_set(DEFAULT, NULL);
      
}

void display_status(void) {
  
  float load;
  static float  max_load=0.0f;
  jack_nframes_t rate;
  unsigned int h, m, s, ds ;
  
  rate = jack_get_sample_rate(client);
  load = jack_cpu_load(client);
  
  h = (unsigned int) (total_nframes / rate ) / 3600;
  m = (unsigned int) ((total_nframes / rate ) / 60 ) % 60;
  s = (unsigned int) ( total_nframes / rate ) % 60;
  ds =(unsigned int) ((100*total_nframes) / rate ) % 100;
  
  if (load>max_load) 
    max_load = load;
  
  
  
  printw("%dHz %d:%02d:%02d.%02d %4.1f%% (%3.1f%%)", rate, h, m, s, ds, load , max_load);
  
  if (record_sts==ONGOING) {

    color_set(RED, NULL);

    printw(" REC");
  
    if (write_disk_buffer_overflow)
      printw(" *-*-* %d write disk buffer overflow(s) *-*-*",write_disk_buffer_overflow);

    color_set(DEFAULT, NULL);
    
  }

  printw("\n");
  
}

void display_buffer(int width) {

  int wrlevel, wrsize, rdlevel, rdsize, i;
  static int peak_wrsize=0, peak_rdsize=0;
  static char *pedale = "|";
  
  rdlevel = (read_disk_buffer_process_pos - read_disk_buffer_thread_pos) & (DISK_SIZE-1);
  rdsize = (width * rdlevel) / DISK_SIZE;
  
  if (rdsize > peak_rdsize) 
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

  if (record_sts==ONGOING) {
		if (n_tracks) {

      wrlevel = (write_disk_buffer_process_pos - write_disk_buffer_thread_pos) & (DISK_SIZE-1);
      wrsize = (width * wrlevel) / DISK_SIZE;

      if (wrsize > peak_wrsize) 
        peak_wrsize = wrsize;
  
    	printw("WR%s", pedale);

    	for (i=0; i<peak_wrsize; i++) {
      	if (i < wrsize-1)
        	printw("+");
      	else if (i == peak_wrsize-1)
        	printw(":");
      	else 
        	printw(" ");
    	}
      printw("\n");

  	}
		else 
    	printw("WR- IDLE !!! No ports selected for recording in %s !!! \n", setup_file);
  }
		
	if      (pedale=="/")
  	pedale = "-";
	else if (pedale=="-")
  	pedale = "\\";
	else if (pedale=="\\")
  	pedale = "|";
	else if (pedale=="|")
  	pedale = "/";
   

}

void display_meter( int width )
{
	int size_out, size_in, i;
  unsigned int port ;
  
  printw("%s\n", scale);
	printw("%s\n", line);
  
  for ( port=0 ; port < n_ports ; port++) {
    
    if ( ports[port].record ) 
      if (record_sts == ONGOING)
        color_set(RED, NULL);
      else 
        color_set(YELLOW, NULL);
    else 
      color_set(GREEN, NULL);
    
    printw("%02d",port+1);
    
    size_in = iec_scale( ports[port].db_in, width );
    size_out = iec_scale( ports[port].db_out, width );
    
    if (size_in > ports[port].max_in)
      ports[port].max_in = size_in;
      
	  if (size_in > ports[port].dkpeak_in) {
		  ports[port].dkpeak_in = size_in;
		  ports[port].dktime_in = 0;
	  } else if (ports[port].dktime_in++ > decay_len) {
		  ports[port].dkpeak_in = size_in;
	  }

    for (i=0; i<ports[port].max_in || i<size_out; i++) {

      if (i < size_in-1) {
        printw("#");
      }
      else if ( i==ports[port].dkpeak_in-1 ) {
        printw("I");
      }
      else if ( i==ports[port].max_in-1 ) {
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
  
  color_set(DEFAULT, NULL);
	printw("%s\n", line);
  printw("%s\n", scale);

}


/******************************************************************************
** EDITION
*/


/******************************************************************************
** CORE
*/

/* Display how to use this program */
static int usage( const char * progname )
{
	fprintf(stderr, "meterec version %s\n\n", VERSION);
	fprintf(stderr, "Usage %s [-f freqency] [-r ref-level] [-w width] [-s sessionname] [-j jackname] [-t]\n\n", progname);
	fprintf(stderr, "where  -f      is how often to update the meter per second [8]\n");
	fprintf(stderr, "       -r      is the reference signal level for 0dB on the meter\n");
	fprintf(stderr, "       -w      is how wide to make the meter [79]\n");
	fprintf(stderr, "       -s      is session name [%s]\n",session);
	fprintf(stderr, "       -j      is the jack client name [%s]\n",jackname);
	fprintf(stderr, "       -t      record a new take\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int console_width = 79; //GetTermSize(&rows, &cols); 
	jack_status_t status;
	int running = 1;
	float ref_lev;
	int rate = 8;
	int opt;
	int key = 0;
	int edit_mode = 0;
  int x_pos =0, y_pos = 0;
  pthread_t wr_dt, rd_dt ;
  
	init();
  
	while ((opt = getopt(argc, argv, "w:f:s:j:thv")) != -1) {
		switch (opt) {
			case 'r':
				ref_lev = atof(optarg);
				fprintf(stderr,"Reference level: %.1fdB\n", ref_lev);
				bias = powf(10.0f, ref_lev * -0.05f);
				break;
			case 'f':
				rate = atoi(optarg);
				fprintf(stderr,"Updates per second: %d\n", rate);
				break;
			case 'w':
				console_width = atoi(optarg);
				fprintf(stderr,"Console Width: %d\n", console_width);
				break;
      case 's':
      	session = optarg ;
				fprintf(stderr,"Session name: %s\n", session);
        break;
      case 'j':
      	jackname = optarg ;
				fprintf(stderr,"Jack client name: %s\n", jackname);
        break;
      case 't':
        record_cmd = START;
				fprintf(stderr,"Recording new take.\n");
        break;
			case 'h':
			case 'v':
			default:
				/* Show usage/version information */
				usage( argv[0] );
				break;
		}
	}

  // init vars that rely on a changable option
  post_option_init();

	// Calculate the decay length (should be 1600ms)
	decay_len = (int)(1.6f / (1.0f/rate));
	
	// Init the scale
  init_display_scale(console_width - 2);

	// Register with Jack
	if ((client = jack_client_open(jackname, JackNullOption, &status)) == 0) {
		fprintf(stderr, "Failed to start '%s' jack client: %d\n", jackname, status);
		exit(1);
	}
	fprintf(stderr,"Registered as '%s'.\n", jack_get_client_name( client ) );

	// Register the signal process callback
	jack_set_process_callback(client, process_jack_data, 0);

	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate client.\n");
		exit(1);
	}

  /* How long should we wait to read 10 times faster than data goes away */
  thread_delay = 1000000ul * BUF_SIZE / jack_get_sample_rate(client) / 10; 
    
  load_setup(setup_file);

  load_session(session_file);
  
  // start the thread emptying disk buffer to file
  if (record_cmd==START) {

    if (n_tracks) {
			fprintf(stderr,"Saving session of n_ports=%d, n_takes=%d+1, n_tracks=%d to '%s'.\n", n_ports, n_takes, n_tracks, session_file );
    	save_session(session_file);

    	fprintf(stderr,"Saving setup of n_ports=%d, n_takes=%d+1, n_tracks=%d to '%s'.\n", n_ports, n_takes, n_tracks, setup_file );
    	save_setup(setup_file);

    	fprintf(stderr,"Starting writer thread\n");
    	pthread_create(&wr_dt, NULL, (void *)&writer_thread, NULL);

    	while(record_sts!=ONGOING) 
      	fsleep( 0.1f );
			
		} else {
		  fprintf(stderr,"ERROR: Cannot do a new take without port selected for recording (R/D/O) in first column of %s\n",setup_file);
			record_cmd=STOP;
			playback_cmd=STOP;
			cleanup(0); 
		}
  } 

	fprintf(stderr,"Starting reader thread\n");
  pthread_create(&rd_dt, NULL, (void *)&reader_thread, NULL);
    
  while(playback_sts!=ONGOING) 
    fsleep( 0.1f );

	// Register the cleanup function to be called when C-c 
	signal(SIGINT, cleanup);

  fprintf(stderr, "Starting ncurses interface...\n");

  mainwin = initscr();
  
  if ( mainwin == NULL ) {
    fprintf(stderr, "Error initialising ncurses.\n");
	  exit(1);
  }

  start_color();
  
  // choose our color pairs
	init_pair(GREEN,  COLOR_GREEN,   COLOR_BLACK);
	init_pair(YELLOW, COLOR_YELLOW,  COLOR_BLACK);
	init_pair(RED,    COLOR_RED,     COLOR_BLACK);

  noecho();  
  keypad(stdscr, TRUE);
  timeout(0);  

	while (running) {
  
    read_peak();
  
    clear();

    display_status();

    display_buffer(console_width - 2);
		
		if (edit_mode) {
		
		  if ( key == KEY_UP ) {
		    y_pos--;
		  }
		  if ( key == KEY_DOWN ) {
		    y_pos++;
		  }
		  if ( key == KEY_LEFT ) {
		    x_pos--;
		  }
		  if ( key == KEY_RIGHT ) {
		    x_pos++;
		  }


      if ( key == 'l' ) {
  		  takes[x_pos].port_has_lock[y_pos] = !takes[x_pos].port_has_lock[y_pos] ;
      }
      if ( key == 'r' ) {
  		  if ( ports[y_pos].record == REC )
          ports[y_pos].record = NO;
        else
          ports[y_pos].record = REC;
      }
      if ( key == 'd' ) {
  		  if ( ports[y_pos].record == DUB )
          ports[y_pos].record = NO;
        else
          ports[y_pos].record = DUB;
      }
      if ( key == 'o' ) {
   		  if ( ports[y_pos].record == OVR )
          ports[y_pos].record = NO;
        else
          ports[y_pos].record = OVR;
      }


      display_session(y_pos, x_pos);
		
		} else {

      display_meter(console_width - 2);

    }

	  key = wgetch(stdscr);

		if ( key == 9 ) {
		
		  edit_mode = !edit_mode ;
			
		}
		
		if ( key == 'q') {
		
			cleanup(0); 
		
		}

    refresh();
	  
       
		fsleep( 1.0f/rate );
    
	}
  
	return 0;
}

