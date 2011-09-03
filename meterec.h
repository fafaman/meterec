/*

  meterec.h 
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2010 2011 Fabrice Lebas
  
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



/* maximum number of connections per port - no known limit*/
#define MAX_CONS 24

/* maximum number of ports - no known limit, only extra memory used */
#define MAX_PORTS 24

/* maximum number of tracks - no known limit, only extra memory used */
#define MAX_TRACKS MAX_PORTS

/* maximum number of takes - no known limit, only extra memory used */
#define MAX_TAKES 100

/* size of disk wait buffer, must be power of two */
#define DISK_SIZE 131072

/* size of disk buffers */
#define BUF_SIZE 4096

/*number of seek indexes*/
#define MAX_INDEX 12

/* max when editing port names */
#define MAX_NAME_LEN 80

/* commands */
#define STOP 0
#define START 1
#define RESTART 2
#define PAUSE 3 

/* status */
#define OFF 0
#define STARTING 1
#define READY 2
#define ONGOING 3
#define STARVING 4
#define STOPING 5
#define PAUSED 6

/* type of recording */
#define REC 1
#define DUB 2
#define OVR 4
#define MAX_REC OVR*2

/* display colors */
#define DEFAULT 0
#define GREEN 1
#define YELLOW 2
#define RED 3
#define BLUE 4

/* view type */
#define VU 0
#define EDIT 1
#define PORT 2

/* port selection */
#define CON_IN -1
#define CON 0
#define CON_OUT 1

#define MAX_UINT ((unsigned int)(-1))

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

struct port_s
{

  jack_port_t *input;
  jack_port_t *output;
  
  unsigned int n_cons;
  const char **input_connected;
  const char **output_connected;
  char *connections[MAX_CONS];
  char *name;
  
  float *write_disk_buffer;
  float *read_disk_buffer;
  
  float peak_in;
  float max_in;
  float peak_out;

  float db_in;
  float db_max_in;
  float db_out;
  
  int dkmax_in;
  int dkpeak_in;
  int dktime_in;
  
  int record;
  int mute;
  int monitor;
  int thru;

  unsigned int playback_take;

};

struct seek_s 
{
};

struct event_s {
	
	unsigned int id;
	unsigned int type;
	unsigned int queue;
	jack_nframes_t old_playhead;
	jack_nframes_t new_playhead;
	unsigned int buffer_pos;
	struct event_s *next;
	struct event_s *prev;
};

struct loop_s
{
	unsigned int low;
	unsigned int high;
	unsigned int enable;
};

struct pos_s
{
	unsigned int port;
	unsigned int take;
	int inout;
	unsigned int con_in;
	unsigned int con_out;
};

struct jack_s
{
	unsigned int sample_rate;
	unsigned long playhead;
};

struct disk_s
{
	unsigned long playhead;
};


struct meterec_s
{
	FILE *fd_log ;
	
	char *session_file;
	char *setup_file;
	char *conf_file;
	char *log_file;
	
	char *jack_name;
	
	unsigned int record_sts;
	unsigned int record_cmd;   /* from gui or process to disk */
	
	unsigned int playback_sts;
	unsigned int playback_cmd; /* from gui or process to disk */
	
	unsigned int keyboard_cmd;
	
	unsigned int curses_sts;
	unsigned int config_sts;
	unsigned int jack_sts;
	
	unsigned int jack_transport;
	
	int connect_ports;
	
	const char **all_input_ports;
	const char **all_output_ports;
	
	unsigned int n_ports;
	struct port_s ports[MAX_PORTS];
	
	unsigned int n_takes;
	struct take_s takes[MAX_TAKES];
	
	unsigned int n_tracks;
	
	jack_client_t *client;
	jack_nframes_t jack_buffsize;
	
	jack_port_t *monitor;
	
	jack_nframes_t seek_index[MAX_INDEX];
	
	struct jack_s jack;
	
	struct disk_s disk;
	
	struct loop_s loop;
	
	struct pos_s pos;
	
	struct event_s *event;
	pthread_mutex_t event_mutex ;
	
	unsigned int output_fmt;
	char *output_ext;
	
	unsigned int write_disk_buffer_thread_pos;
	unsigned int write_disk_buffer_process_pos;
	unsigned int write_disk_buffer_overflow;
	
	unsigned int read_disk_buffer_thread_pos;
	unsigned int read_disk_buffer_process_pos;
	unsigned int read_disk_buffer_overflow;
	
};

void start_playback(void);
void stop(struct meterec_s *meterec);
void exit_on_error(char * reason);
void compute_takes_to_playback(struct meterec_s *meterec);
void compute_tracks_to_record(struct meterec_s *meterec);
