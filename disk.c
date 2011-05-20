#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sndfile.h>
#include <jack/jack.h>

#include "config.h"
#include "meterec.h"
#include "disk.h"

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
    info.samplerate = jack_get_sample_rate(meterec->client);
    
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

int writer_thread(void *d)
{
    unsigned int i, port, opos, track, thread_delay;
    SNDFILE *out;
    float buf[BUF_SIZE * MAX_PORTS];
    struct meterec_s *meterec ;

    meterec = (struct meterec_s *)d ;

    meterec->record_sts = STARTING ;

    fprintf(meterec->fd_log, "Writer thread: Started.\n");

    thread_delay = set_thread_delay(meterec->client);

    /* Open the output file */
    out = write_disk_open_fd(meterec);
    
    if (!out)
        return 1;
    
    /* Start writing the RT ringbuffer to disk */
    meterec->record_sts = ONGOING ;
    opos = 0;
    while (meterec->record_sts) {
    
      for (i  = meterec->write_disk_buffer_thread_pos; 
           i != meterec->write_disk_buffer_process_pos && opos < BUF_SIZE;
           i  = (i + 1) & (DISK_SIZE - 1), opos++ ) {
        
        track = 0;
        for (port = 0; port < meterec->n_ports; port++) {
          if (meterec->ports[port].record) {
            buf[opos * meterec->n_tracks + track] = meterec->ports[port].write_disk_buffer[i]; 
            track++;
          }
        }
        
      }
      
      if (opos == BUF_SIZE) {
        sf_writef_float(out, buf, opos);
        opos = 0;
      }
      
      meterec->write_disk_buffer_thread_pos = i;
      
      if (meterec->record_cmd == RESTART ) {
        write_disk_close_fd(meterec, out);
        compute_tracks_to_record(meterec);
        out = write_disk_open_fd(meterec);
        /*this should be protected with a mutex or so...*/
        meterec->record_cmd = START;
      }
      
      /* run until empty buffer after a stop requets */
      if (meterec->record_sts == STOPING)
        if ( meterec->write_disk_buffer_thread_pos == meterec->write_disk_buffer_process_pos ) {
          sf_writef_float(out, buf, opos);
          break;
        }
    
      if (meterec->record_cmd == STOP)
        meterec->record_sts = STOPING ;
         
      usleep(thread_delay);
      
    }
    
    write_disk_close_fd(meterec, out);
    
    fprintf(meterec->fd_log,"Writer thread: done.\n");
    
    meterec->record_sts = OFF;
    
    return 0;
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

  /* open all files needed for this session */
  for (port=0; port<meterec->n_ports; port++) {

    take = meterec->ports[port].playback_take;

    /* do not open a file for a port that wants to playback take 0 */
    if (!take ) {

      fprintf(meterec->fd_log,"Reader thread: Port %d does not have a take associated\n", port+1);

      /* rather fill buffer with 0's */
      for (i=0; i<DISK_SIZE; i++) 
        meterec->ports[port].read_disk_buffer[i] = 0.0f ;

      continue;
    }

    /* do not open a file for a port that wants to be recoded in REC mode */
    if (meterec->ports[port].record==REC && meterec->record_cmd==START) {

      fprintf(meterec->fd_log,"Reader thread: Port %d beeing recorded in REC mode will not have a playback take associated\n", port+1);

      /* rather fill buffer with 0's */
      for (i=0; i<DISK_SIZE; i++) 
        meterec->ports[port].read_disk_buffer[i] = 0.0f ;

      continue;

    }

    fprintf(meterec->fd_log,"Reader thread: Port %d has take %d associated\n", port+1, take );

    /* only open a take file that is not defined yet  */  
    if (meterec->takes[take].take_fd == NULL) {
      meterec->takes[take].take_fd = sf_open(
      				meterec->takes[take].take_file, 
				SFM_READ, 
				&meterec->takes[take].info
				);

      /* check file is (was) opened properly */
      if (meterec->takes[take].take_fd == NULL) {
        meterec->playback_sts = OFF;
        fprintf(meterec->fd_log,"Reader thread: Cannot open file '%s' for reading\n", meterec->takes[take].take_file);
		exit_on_error("Reader thread: Cannot open file for reading");
      }

      fprintf(meterec->fd_log,"Reader thread: Opened '%s' for reading\n", meterec->takes[take].take_file);

      /* allocate buffer space for this take */
      fprintf(meterec->fd_log,"Reader thread: Allocating local buffer space %d*%d for take %d\n", 
      				meterec->takes[take].ntrack, 
				BUF_SIZE, 
				take);

      meterec->takes[take].buf = calloc(BUF_SIZE*meterec->takes[take].ntrack, sizeof(float));

    } 
    else {
      fprintf(meterec->fd_log,"Reader thread: File and buffer already setup.\n");
    }

  }

}

unsigned int fill_buffer(unsigned int *opos , struct meterec_s *meterec) {

    unsigned int i,  port, take, track, ntrack=0, fill;
    
    
    if (*opos == 0) {

      /* load the local buffer */
      for(take=1; take<meterec->n_takes+1; take++) {

        /* check if take is used */
        if (meterec->takes[take].take_fd == NULL)
          continue;

        /* get the number of tracks in this take */
        ntrack = meterec->takes[take].ntrack;

        fill = sf_read_float(meterec->takes[take].take_fd, meterec->takes[take].buf, (BUF_SIZE * ntrack) ); 

        /* complete buffer with 0's if reached end of file */
        for ( ; fill<(BUF_SIZE * ntrack); fill++) 
          meterec->takes[take].buf[fill] = 0.0f;
        
      } 

    }
    
    /* walk in the local buffer and copy it to each port buffers (demux)*/
    for (i  = meterec->read_disk_buffer_thread_pos; 
         i != meterec->read_disk_buffer_process_pos && *opos < BUF_SIZE;
         i  = (i + 1) & (DISK_SIZE - 1), (*opos)++ ) {
         
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
            meterec->ports[port].read_disk_buffer[i] = meterec->takes[take].buf[*opos * ntrack + track] ;
            
        }
        
      }
      
    }
      
    if (*opos == BUF_SIZE) 
      *opos = 0;

  return i;
}


int reader_thread(void *d)
{
    unsigned int i, take, opos, thread_delay, new_buffer_pos;
    struct meterec_s *meterec ;
    jack_nframes_t seek, new_playhead_target = -1;

    meterec = (struct meterec_s *)d ;
    
    meterec->playback_sts = STARTING ;
    
    fprintf(meterec->fd_log, "Reader thread: started.\n");

    thread_delay = set_thread_delay(meterec->client);

    /* empty buffer ( reposition thread position in order to refill where process will first read) */
    meterec->read_disk_buffer_thread_pos  = (meterec->read_disk_buffer_process_pos + 1);
    meterec->read_disk_buffer_thread_pos &= (DISK_SIZE - 1);

    /* open all files needed for this playback */
    read_disk_open_fd(meterec);

    fprintf(meterec->fd_log,"Reader thread: Start reading files.\n");
    
    /* prefill buffer at once */
    opos = 0;
    while (meterec->read_disk_buffer_thread_pos != meterec->read_disk_buffer_process_pos) {
        i = fill_buffer(&opos, meterec);
        meterec->read_disk_buffer_thread_pos = i;
    }

    /* Start reading disk to fill the RT ringbuffer */
    new_buffer_pos = -1;
    while ( meterec->playback_cmd==START )  {

    /* seek audio back and forth upon user request */
    seek = meterec->seek.disk_playhead_target;
    if (seek != (unsigned int)(-1) ) {

      /* make sure we fill buffer away from where jack read to avoid having to wait filling ringbuffer*/
      meterec->read_disk_buffer_thread_pos  = meterec->read_disk_buffer_process_pos - BUF_SIZE - 1;
      meterec->read_disk_buffer_thread_pos &= (DISK_SIZE - 1);
      
      if (meterec->seek.files_reopen) {
        fprintf(meterec->fd_log,"Reader thread: Re-opening all playback files\n");
        read_disk_close_fd(meterec);
        compute_takes_to_playback(meterec);
        read_disk_open_fd(meterec);
      }

      fprintf(meterec->fd_log,"Reader thread: Seek %d\n", seek);

      for(take=1; take<meterec->n_takes+1; take++) {
        /* check if track is used */
        if (meterec->takes[take].take_fd == NULL)
          continue;
        sf_seek(meterec->takes[take].take_fd, seek, SEEK_SET);
      }
      
      /* allow to copy fresh buffer */
      opos = 0;
      
      /* clear processed request */
      pthread_mutex_lock( &meterec->seek.mutex );
      meterec->seek.disk_playhead_target = -1;
      pthread_mutex_unlock( &meterec->seek.mutex );
      
      /* store position of new buffer start */
      new_buffer_pos  = meterec->read_disk_buffer_thread_pos;
      new_buffer_pos -= 1;
      new_buffer_pos &= (DISK_SIZE - 1);
      
      /* store playhead value matching above buffer position */
      new_playhead_target = seek;
    
    /* lets fill local buffer only if previously emptied*/
    } 

    i = fill_buffer(&opos, meterec);

    if ((new_buffer_pos!=(unsigned int)(-1)) && (meterec->read_disk_buffer_thread_pos != i)) {
      pthread_mutex_lock( &meterec->seek.mutex );
      meterec->seek.jack_buffer_target = new_buffer_pos;
      meterec->seek.playhead_target = new_playhead_target ;
      pthread_mutex_unlock( &meterec->seek.mutex );
      new_buffer_pos = -1;
      new_playhead_target = -1;
    }

    meterec->read_disk_buffer_thread_pos = i;

    if ( meterec->playback_sts==STARTING && (1-read_disk_buffer_level(meterec) > (4.0f/5)) )
      meterec->playback_sts=ONGOING;

    usleep(thread_delay);

  }

  /* close all fd's */
  read_disk_close_fd(meterec);

  fprintf(meterec->fd_log,"Reader thread: done.\n");

  meterec->playback_sts = OFF;

  return 0;
}

float read_disk_buffer_level(struct meterec_s *meterec) {
  float rdlevel;

  rdlevel = (meterec->read_disk_buffer_process_pos - meterec->read_disk_buffer_thread_pos) & (DISK_SIZE-1);
  return  (float)(rdlevel / DISK_SIZE);
}


unsigned int set_thread_delay(jack_client_t *client) {

    /* How long should we wait to read 10 times faster than data goes away */
    return 1000000ul * BUF_SIZE / jack_get_sample_rate(client) / 10; 

}

