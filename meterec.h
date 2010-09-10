/*

  meterec.h 
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2010 Fabrice Lebas
  
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

/* commands */
#define STOP 0
#define START 1
#define RESTART 2

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

/* display colors */
#define DEFAULT 0
#define GREEN 1
#define YELLOW 2
#define RED 3
#define BLUE 4


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

};

struct meterec_s
{
  FILE *fd_log ;

  char *session_file;
  char *setup_file;
  char *log_file;

  unsigned int record_sts;
  unsigned int record_cmd;

  unsigned int playback_sts;
  unsigned int playback_cmd;

  unsigned int n_ports;
  struct port_s ports[MAX_PORTS];
  
  unsigned int n_takes;
  struct take_s takes[MAX_TAKES];

  unsigned int n_tracks;

  jack_client_t *client;
  jack_nframes_t jack_buffsize;
  jack_nframes_t seek ;

  unsigned int write_disk_buffer_thread_pos;
  unsigned int write_disk_buffer_process_pos;
  unsigned int write_disk_buffer_overflow;

  unsigned int read_disk_buffer_thread_pos; /* Hum... Would be better to rework thread loop... */
  unsigned int read_disk_buffer_process_pos;
  unsigned int read_disk_buffer_overflow;

};

void exit_on_error(char * reason);
