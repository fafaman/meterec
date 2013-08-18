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
#include <string.h>
#include <unistd.h>

#include <curses.h>
#include <sndfile.h>
#include <jack/jack.h>

#include "position.h"
#include "config.h"
#include "meterec.h"
#include "disk.h"
#include "conf.h"
#include "queue.h"

#define RD_BUFF_LEN ((meterec->read_disk_buffer_process_pos - meterec->read_disk_buffer_thread_pos) & (DBUF_SIZE-1))


/******************************************************************************
** THREADs
*/

void write_disk_close_fd(struct meterec_s *meterec, SNDFILE *out) {
	
	sf_write_sync(out);
	sf_close(out);
	
	meterec->n_takes ++;
	
}

SNDFILE* write_disk_open_fd(struct meterec_s *meterec) {
	
	SF_INFO info;
	SNDFILE *out;
	char *take_file ;
	
	info.format = meterec->output_fmt;
	info.channels = meterec->n_tracks;
	info.samplerate = meterec->jack.sample_rate;
	
	take_file = meterec->takes[meterec->n_takes + 1].take_file;
	
	if (!sf_format_check(&info)) {
		fprintf(meterec->fd_log, "Writer thread: Cannot open take file '%s' for writing (%d, %d, %d)\n",take_file,info.format, info.channels, info.samplerate);
		meterec->record_sts = OFF;
		exit_on_error("Writer thread: Output file format error\n" );
		return (SNDFILE*)NULL;
	}
	
	out = sf_open(take_file, SFM_WRITE, &info);
	
	if (!out) {
		fprintf(meterec->fd_log,"Writer thread: Cannot open '%s' file for writing",take_file);
		meterec->record_sts = OFF;
		return (SNDFILE*)NULL;
	}
	
	fprintf(meterec->fd_log,"Writer thread: Opened %d track(s) file '%s' for writing.\n", meterec->n_tracks, take_file);
	
	return out;
}

void *writer_thread(void *d) {
	unsigned int i, port, zbuff_pos, track, thread_delay;
	SNDFILE *out;
	float buf[ZBUF_SIZE * MAX_PORTS];
	struct meterec_s *meterec ;
	
	meterec = (struct meterec_s *)d ;
	
	meterec->record_sts = STARTING ;
	
	fprintf(meterec->fd_log, "Writer thread: Started.\n");
	
	thread_delay = set_thread_delay(meterec);
	
	/* Open the output file */
	out = write_disk_open_fd(meterec);
	
	if (!out)
		return (void*)1;
	
	/* Start writing the RT ringbuffer to disk */
	meterec->record_sts = ONGOING ;
	zbuff_pos = 0;
	while (meterec->record_sts) {
		
		for (i  = meterec->write_disk_buffer_thread_pos; 
			i != meterec->write_disk_buffer_process_pos && zbuff_pos < ZBUF_SIZE;
			i  = (i + 1) & (DBUF_SIZE - 1), zbuff_pos++ ) {
			
			track = 0;
			for (port = 0; port < meterec->n_ports; port++) {
				if (meterec->ports[port].record) {
					buf[zbuff_pos * meterec->n_tracks + track] = meterec->ports[port].write_disk_buffer[i]; 
					track++;
				}
			}
		}
		
		if (zbuff_pos == ZBUF_SIZE) {
			sf_writef_float(out, buf, zbuff_pos);
			zbuff_pos = 0;
		}
		
		meterec->write_disk_buffer_thread_pos = i;
		
		if (meterec->record_cmd == RESTART ) {
			
			write_disk_close_fd(meterec, out);
			
			if (meterec->config_sts)
				save_conf(meterec);
			
			compute_tracks_to_record(meterec);
			
			out = write_disk_open_fd(meterec);
			
			/*this should be protected with a mutex or so...*/
			meterec->record_cmd = START;
			
		}
		
		/* run until empty buffer after a stop requets */
		if (meterec->record_sts == STOPING)
			if ( meterec->write_disk_buffer_thread_pos == meterec->write_disk_buffer_process_pos ) {
				sf_writef_float(out, buf, zbuff_pos);
				break;
			}
		
		if (meterec->record_cmd == STOP)
			meterec->record_sts = STOPING ;
		
		usleep(thread_delay);
		
	}
	
	write_disk_close_fd(meterec, out);
	
	if (meterec->config_sts)
		save_conf(meterec);
	
	fprintf(meterec->fd_log,"Writer thread: done.\n");
	
	meterec->record_sts = OFF;
	
	return (void*)0;
}

void read_disk_close_fd(struct meterec_s *meterec) {
	
	unsigned int take;
	
	/* close all fd's */
	for (take=1; take<meterec->n_takes+1; take++) 
		if (meterec->takes[take].take_fd) {
			sf_close(meterec->takes[take].take_fd);
			free(meterec->takes[take].buf);
			meterec->takes[take].buf = NULL;
			meterec->takes[take].take_fd = NULL;
		}
}

void read_disk_open_fd(struct meterec_s *meterec) {
	
	unsigned int take, port, i;
	struct time_s tlenght;
	
	/* open all files needed for this session */
	for (port=0; port<meterec->n_ports; port++) {
		
		take = meterec->ports[port].playback_take;
		
		/* do not open a file for a port that wants to playback take 0 */
		if (!take ) {
			
			fprintf(meterec->fd_log,"Reader thread: Port %d does not have a take associated\n", port+1);
			
			/* rather fill buffer with 0's */
			for (i=0; i<DBUF_SIZE; i++) 
				meterec->ports[port].read_disk_buffer[i] = 0.0f ;
			
			continue;
		}
		
		/* do not open a file for a port that wants to be recoded in REC mode */
		if (meterec->ports[port].record==REC && meterec->record_cmd==START) {
			
			fprintf(meterec->fd_log,"Reader thread: Port %d beeing recorded in REC mode will not have a playback take associated\n", port+1);
			
			/* rather fill buffer with 0's */
			for (i=0; i<DBUF_SIZE; i++) 
				meterec->ports[port].read_disk_buffer[i] = 0.0f ;
			
			continue;
		}
		
		fprintf(meterec->fd_log,"Reader thread: Port %d has take %d associated\n", port+1, take );
		
		/* only open a take file that is not defined yet  */  
		if (meterec->takes[take].take_fd == NULL) {
			
			meterec->takes[take].take_fd = sf_open( 
				meterec->takes[take].take_file, 
				SFM_READ, 
				&meterec->takes[take].info);
			
			/* check file is (was) opened properly */
			if (meterec->takes[take].take_fd == NULL) {
				meterec->playback_sts = OFF;
				fprintf(meterec->fd_log,"Reader thread: Cannot open file '%s' for reading\n", meterec->takes[take].take_file);
				exit_on_error("Reader thread: Cannot open file for reading");
			}
			
			fprintf(meterec->fd_log,"Reader thread: Opened '%s' for reading\n", meterec->takes[take].take_file);
			
			/* save take lenght information */
			time_init_frm(&tlenght, 
				meterec->takes[take].info.samplerate, 
				meterec->takes[take].info.frames + meterec->takes[take].offset);
			time_sprint(&tlenght, meterec->takes[take].lenght);
			
			/* allocate buffer space for this take */
			fprintf(meterec->fd_log,"Reader thread: Allocating local buffer space %d*%d for take %d\n", 
				meterec->takes[take].ntrack, 
				ZBUF_SIZE, 
				take);
			
			meterec->takes[take].buf = calloc(ZBUF_SIZE*meterec->takes[take].ntrack, sizeof(float));
			
		} 
		else {
			fprintf(meterec->fd_log,"Reader thread: File and buffer already setup.\n");
		}
		
	}
	
}

unsigned int fill_buffer(struct meterec_s *meterec, unsigned int *zbuff_pos ) {
	
	/* real read from disk thru libsndfile to fill 'buffer zero' then copy from
	   'buffer zero' to 'disk buffer' that is the connection with jack process */
	
	unsigned int rdbuff_pos, port, take, track, ntrack=0, fill;
	
	/* Leave right away if the process does not need any data (disk buffer full) */
	if (meterec->read_disk_buffer_thread_pos == meterec->read_disk_buffer_process_pos)
		return meterec->read_disk_buffer_thread_pos;
	
	/* lets fill local buffer only if previously emptied */
	if (*zbuff_pos == 0) {
	
	#ifdef DEBUG_BUFF
	fprintf(meterec->fd_log, "fill_buffer: Filling zero buffer -------------------------------\n");
	#endif
		/* load the local buffer */
		for(take=1; take<meterec->n_takes+1; take++) {
			
			/* check if take is used */
			if (meterec->takes[take].take_fd == NULL)
				continue;
			
			/* get the number of tracks in this take */
			ntrack = meterec->takes[take].ntrack;
			
			fill = sf_read_float(meterec->takes[take].take_fd, meterec->takes[take].buf, (ZBUF_SIZE * ntrack) ); 
			
			/* complete buffer with 0's if reached end of file */
			for ( ; fill<(ZBUF_SIZE * ntrack); fill++) 
				meterec->takes[take].buf[fill] = 0.0f;
			
		}
	
	}
	
	
	
	/* walk in the zero buffer and copy it to each port buffers (demux)*/
	for (rdbuff_pos  = meterec->read_disk_buffer_thread_pos; 
		rdbuff_pos != meterec->read_disk_buffer_process_pos && *zbuff_pos < ZBUF_SIZE;
		rdbuff_pos  = (rdbuff_pos + 1) & (DBUF_SIZE - 1), (*zbuff_pos)++, meterec->disk.playhead++ ) {
		
		for(take=1; take<meterec->n_takes+1; take++) {
			
			
			/* check if take is used */
			if (meterec->takes[take].take_fd == NULL)
				continue;
			
			ntrack = meterec->takes[take].ntrack;
			
			/* for each track belonging to this take */
			for (track=0; track<ntrack; track++) {
				
				/* find what port is mapped to this track */
				port = meterec->takes[take].track_port_map[track] ;
				
				/* check if this port needs data from this take */
				if (meterec->ports[port].playback_take == take)
					/* Only fill buffer if in playback, dub or overdub */
					if (meterec->ports[port].record != REC || !meterec->record_sts)
						meterec->ports[port].read_disk_buffer[rdbuff_pos] = meterec->takes[take].buf[*zbuff_pos * ntrack + track] ;
				
			}
		
		}
	
	}
	
	if (*zbuff_pos == ZBUF_SIZE) 
		*zbuff_pos = 0;
	
	#ifdef DEBUG_BUFF
	fprintf(meterec->fd_log, "fill_buffer: zbuff_pos %4X/%4X | rdbuff_pos %5X/%5X | thread_pos %5X/%5X | process_pos %5X/%5X\n", 
		*zbuff_pos, ZBUF_SIZE, 
		rdbuff_pos, DBUF_SIZE, 
		meterec->read_disk_buffer_thread_pos, DBUF_SIZE,
		meterec->read_disk_buffer_process_pos, DBUF_SIZE);
	#endif
	
	return rdbuff_pos;
	
}

void read_disk_seek(struct meterec_s *meterec, unsigned int seek) {
	
	unsigned int take, abs_seek;
	sf_count_t reached;
	
	for(take=1; take<meterec->n_takes+1; take++) {
	
		/* check if track is used */
		if (meterec->takes[take].take_fd == NULL)
			continue;
		
		#ifdef DEBUG_SEEK
		fprintf(meterec->fd_log, "read_disk_seek: seek take %d at position %d 0x%X (%.3f).\n", take, seek, seek, (float)seek/meterec->jack.sample_rate);
		#endif
		
		abs_seek = seek + meterec->takes[take].offset;
		
		reached = sf_seek(meterec->takes[take].take_fd, abs_seek, SEEK_SET);
		
		if (abs_seek - reached) {
			#ifdef DEBUG_SEEK
			fprintf(meterec->fd_log, "read_disk_seek: failed (abs_seek=%d reached=%d)", abs_seek, reached);
			#endif
			sf_seek(meterec->takes[take].take_fd, 0, SEEK_END);
		}
	}

}


void *reader_thread(void *d)
{
	unsigned int rdbuff_pos, zbuff_pos, thread_delay, may_loop;
	struct event_s *event, *event_kill;
	struct meterec_s *meterec ;
	
	meterec = (struct meterec_s *)d ;
	
	meterec->playback_sts = STARTING ;
	
	fprintf(meterec->fd_log, "Reader thread: started.\n");
	
	thread_delay = set_thread_delay(meterec);
	
	/* empty buffer (reposition thread position in order to refill where process will first read) */
	meterec->read_disk_buffer_thread_pos  = (meterec->read_disk_buffer_process_pos + 1);
	meterec->read_disk_buffer_thread_pos &= (DBUF_SIZE - 1);
	
	/* open all files needed for this playback */
	read_disk_open_fd(meterec);
	
	fprintf(meterec->fd_log,"Reader thread: Start reading files.\n");
	
	/* prefill buffer at once */
	zbuff_pos = 0;
	while (meterec->read_disk_buffer_thread_pos != meterec->read_disk_buffer_process_pos) {
		rdbuff_pos = fill_buffer(meterec, &zbuff_pos);
		meterec->read_disk_buffer_thread_pos = rdbuff_pos;
	}
	
	meterec->playback_sts=ONGOING;
	
	/* Start reading disk to fill the RT ringbuffer */
	while ( meterec->playback_cmd==START ) {
		
		#ifdef DEBUG_QUEUES
		event_queue_print(meterec, LOG); 
		#endif
		
		/* find ALL event types in DISK queue */
		event = find_last_event(meterec, DISK, ALL);
		
		if (event) {
			
			switch (event->type) {
			
			case LOCK:
				
				read_disk_close_fd(meterec);
				compute_takes_to_playback(meterec);
				read_disk_open_fd(meterec);
				
				/* no break here */
				
			case SEEK:
				
				/* make sure we fill buffer away from where jack read to avoid having to wait filling ringbuffer */
				meterec->read_disk_buffer_thread_pos  = meterec->read_disk_buffer_process_pos - (DBUF_SIZE/2);
				meterec->read_disk_buffer_thread_pos &= (DBUF_SIZE - 1);
				
				read_disk_seek(meterec, event->new_playhead);
				
				zbuff_pos = 0;
				
				event->buffer_pos  = meterec->read_disk_buffer_thread_pos ;
				event->buffer_pos &= (DBUF_SIZE - 1);
				
				meterec->disk.playhead = event->new_playhead;
				
				event->queue = PEND;
				
				pthread_mutex_lock(&meterec->event_mutex);
				find_rm_events(meterec, JACK, ALL);
				find_rm_events(meterec, DISK, ALL);
				pthread_mutex_unlock(&meterec->event_mutex);
				
				break;
				
				
			case LOOP:
				
				/* Is next loop event already in the buffer */
				if (meterec->loop.enable) {
					
					if (meterec->disk.playhead > meterec->loop.high &&
						meterec->jack.playhead < meterec->loop.high) {
						
						/*we need to empty buffer up to the loop point */
						meterec->read_disk_buffer_thread_pos -= meterec->loop.high;
						meterec->read_disk_buffer_thread_pos += meterec->disk.playhead;
						meterec->read_disk_buffer_thread_pos &= (DBUF_SIZE - 1);
						
						read_disk_seek(meterec, meterec->loop.low);
						zbuff_pos = 0;
						
						meterec->disk.playhead = meterec->loop.low;
					}
					
					pthread_mutex_lock(&meterec->event_mutex);
					rm_event(meterec, event);
					event = NULL;
					pthread_mutex_unlock(&meterec->event_mutex);
				
				} else {
					/* we are trying to cancel all ongoing loops */
					event_kill = find_first_event(meterec, JACK, LOOP);
					
					if (event_kill) {
						/* rewind to the oldest event loop */
						meterec->read_disk_buffer_thread_pos = event_kill->buffer_pos ;
					}
					
					pthread_mutex_lock(&meterec->event_mutex);
					find_rm_events(meterec, ALL, LOOP);
					event = NULL;
					pthread_mutex_unlock(&meterec->event_mutex);
				}
				
				break;
			
			}
			
		}
		
		
		may_loop = 0;
		if (meterec->loop.enable) 
			if (meterec->disk.playhead < meterec->loop.high)
				may_loop = 1;
		
		rdbuff_pos = fill_buffer(meterec, &zbuff_pos);
		
		if (may_loop)
			if (meterec->disk.playhead >= meterec->loop.high) {
				
				read_disk_seek(meterec, meterec->loop.low);
				zbuff_pos = 0;
				
				rdbuff_pos -= (meterec->disk.playhead - meterec->loop.high);
				rdbuff_pos &= (DBUF_SIZE - 1);
				
				meterec->disk.playhead = meterec->loop.low;
				
				pthread_mutex_lock(&meterec->event_mutex);
				add_event(meterec, JACK, LOOP, meterec->loop.low, meterec->loop.high, rdbuff_pos);
				pthread_mutex_unlock(&meterec->event_mutex);
			}
		
		#ifdef DEBUG_QUEUES
		event_queue_print(meterec, LOG); 
		#endif
		
		event = find_first_event(meterec, PEND, ALL);
		
		if (event) 
			switch (event->type) {
				case SEEK:
					
					/* do not pass the event to jack process if we did not have data
					   to fill the buffer */
					if (meterec->read_disk_buffer_thread_pos != rdbuff_pos)
						event->queue = JACK;
					break;
					
				case LOCK:
					/* be less reactive but more secure when changing take locks
					   and only tell jack process to jump once we have a full 
					   'buffer zero' adavance */
					if (meterec->disk.playhead > meterec->jack.playhead + ZBUF_SIZE)
						event->queue = JACK;
					break;
						
			}
		
		meterec->read_disk_buffer_thread_pos = rdbuff_pos;
		
		#ifdef DEBUG_QUEUES
		event_queue_print(meterec, LOG); 
		#endif
		
		usleep(thread_delay);
		
	}
	
	/* close all fd's */
	read_disk_close_fd(meterec);
	
	fprintf(meterec->fd_log,"Reader thread: done.\n");
	
	meterec->playback_sts = OFF;
	
	return 0;
	
}

float read_disk_buffer_level(struct meterec_s *meterec) {
	float level;
	level = (meterec->read_disk_buffer_process_pos - meterec->read_disk_buffer_thread_pos) & (DBUF_SIZE-1);
	return  (float)(level / DBUF_SIZE);
}

float write_disk_buffer_level(struct meterec_s *meterec) {
	float level;
	level = (meterec->write_disk_buffer_process_pos - meterec->write_disk_buffer_thread_pos) & (DBUF_SIZE-1);
	return  (float)(level / DBUF_SIZE);
}

unsigned int set_thread_delay(struct meterec_s *meterec) {
	
	/* How long should we wait to read 10 times faster than data goes away */
	return 1000000ul * ZBUF_SIZE / meterec->jack.sample_rate / 10; 
	
}

