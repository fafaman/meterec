/*

  meterec
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
#include <string.h>

#include <sndfile.h>
#include <jack/jack.h>
#include <curses.h>
#include "meterec.h"
#include "disk.h"

char *scale ;
char *line ;

void display_port_info(struct port_s *port_p) {

  if (port_p->record==REC)
     printw("[REC]");
  else if (port_p->record==OVR)
     printw("[OVR]");
  else if (port_p->record==DUB)
     printw("[DUB]");
  else 
     printw("[   ]");
     
  if (port_p->mute)
     printw("[MUTED]");
  else 
     printw("[     ]");

   if (port_p->thru)
     printw("[THRU]");
  else 
     printw("[    ]");

   if ( port_p->playback_take ) 
    printw(" PLAYING take %2d", port_p->playback_take);
  else 
    printw(" PLAYING no take");
  
  printw(" | %5.1fdB", port_p->db_in);
  printw(" (%5.1fdB)", port_p->db_max_in);

  if (port_p->name)
    printw(" | %s", port_p->name);

}

void display_port_recmode(struct port_s *port_p) {
  if ( port_p->record == REC )
    if ( port_p->mute )
      printw("r");
    else
      printw("R");
  else if ( port_p->record == DUB )
    if ( port_p->mute )
      printw("d");
    else
      printw("D");
  else if ( port_p->record == OVR )
    if ( port_p->mute )
      printw("o");
    else
      printw("O");
  else 
    if ( port_p->mute )
      printw("~");
    else
      printw("=");
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

void display_meter(struct meterec_s *meterec, int display_names, int width, int decay_len)
{
  int size_out, size_in, i;
  unsigned int port ;
  
  width -= 3;
  
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
	
    if (meterec->pos.port == port) 
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
        if (i>width-3)
	  printw("X");
	else
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
  
  printw("  Port %2d ", meterec->pos.port+1);
  display_port_info( &meterec->ports[meterec->pos.port] );
   

}

void init_display_scale( unsigned int width )
{

  unsigned int i=0;
  const int marks[12] = { 0, -3, -5, -10, -15, -20, -25, -30, -35, -40, -50, -60 };
  
  char * scale0 ;
  char * line0 ;
  
  width -= 3;
  
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


void display_buffer(struct meterec_s *meterec, int width) {

  int wrlevel, wrsize, rdsize, i;
  static int peak_wrsize=0, peak_rdsize=0;
  static char *pedale = "|";
  
  width -= 3;
  
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


  if (meterec->record_sts==ONGOING) {
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

void display_status(struct meterec_s *meterec, unsigned int playhead) {
  
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
  if (meterec->playback_sts==STOPING) 
    printw("%-8s","STOPING");

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
  if (meterec->record_sts==STOPING) 
    printw("%-8s","STOPING");

  printw("]");
  
  if (meterec->record_sts) {
    attron(A_BOLD);
    printw(" Take %d",meterec->n_takes+1);
    attroff(A_BOLD); 
  }
    
  
  if (meterec->write_disk_buffer_overflow)
    printw(" OVERFLOWS(%d)",meterec->write_disk_buffer_overflow);

  color_set(DEFAULT, NULL);

  printw("\n");
  
}

static void color_port(struct meterec_s *meterec, unsigned int port) {

	if (meterec->ports[port].record) 
		if (meterec->record_sts == ONGOING)
			color_set(RED, NULL);
		else 
			color_set(YELLOW, NULL);
	else 
		color_set(GREEN, NULL);
}

void display_session(struct meterec_s *meterec) 
{
  unsigned int take, port;
  unsigned int y_pos, x_pos;
  
  y_pos = meterec->pos.port;
  x_pos = meterec->pos.take;

  
  printw("  Take %2d ",x_pos);
  printw("%s",  meterec->takes[x_pos].port_has_track[y_pos]?"[CONTENT]":"[       ]" );
  printw("%s",  meterec->takes[x_pos].port_has_lock[y_pos]?"[LOCKED]":"[      ]" );
  printw("%s", (meterec->ports[y_pos].playback_take == x_pos)?"[PLAYING]":"[       ]" );
  
  printw("\n\n");

  for (port=0; port<meterec->n_ports; port++) {
  
    color_port(meterec, port);
    
    if (y_pos == port) 
       attron(A_REVERSE);
    else 
       attroff(A_REVERSE);
  
    printw("%02d",port+1);

    display_port_recmode(&meterec->ports[port]);

    for (take=1; take<meterec->n_takes+1; take++) {
    
      if ((y_pos == port) || (x_pos == take))
         attron(A_REVERSE);
      else 
         attroff(A_REVERSE);

      if ((y_pos == port) && (x_pos == take))
         attroff(A_REVERSE);

      if ( meterec->ports[port].playback_take == take )
        attron(A_BOLD);
        
      if ( meterec->takes[take].port_has_lock[port] )
        printw(meterec->takes[take].port_has_track[port]?"L":"l");
      else if ( meterec->ports[port].playback_take == take ) 
        printw("P");
      else if ( meterec->takes[take].port_has_track[port] ) 
        printw("X");
      else 
        printw("-");
        
      attroff(A_BOLD);
              
    }
	
    printw("\n");
  }
  
  attroff(A_REVERSE);
  color_set(DEFAULT, NULL);

  printw("\n\n");
  printw("  Port %2d ", y_pos+1);
  display_port_info( &meterec->ports[y_pos] );
        
}

void display_ports(struct meterec_s *meterec) 
{
	unsigned int port=0, line=0, i;
	const char **in, **out;
  
	out=meterec->all_input_ports;
	in=meterec->all_output_ports;
		
	printw("  Port %2d ", meterec->pos.port+1);
	display_port_info( &meterec->ports[meterec->pos.port] );
	printw("\n\n");

	while (*in || *out || port < meterec->n_ports) {
	
		printw("  ");
		
		if (*in) {
		
			if (meterec->pos.inout == CON_IN)
				if (meterec->pos.con_in == line)
					attron(A_REVERSE);
					
			if (jack_port_connected_to(meterec->ports[meterec->pos.port].input, *in)) {
				attron(A_BOLD);
				printw("%20s",*in);
				attroff(A_BOLD);
				attroff(A_REVERSE);
				printw("-+");
			}
			else if (port==meterec->pos.port) {
				printw("%20s",*in);
				attroff(A_REVERSE);
				printw(" +");
				color_port(meterec, meterec->pos.port);
			}
			else {
				printw("%20s",*in);
				attroff(A_REVERSE);
				printw(" |");
			}
			in++;
		} else {
			if (port<meterec->pos.port)
				printw("%20s |","");
			else if (port==meterec->pos.port)
				printw("%20s +","");
			else 
				printw("%20s  ","");
		}
		
		
		if (port<meterec->n_ports) {
		
			if (meterec->pos.port == port) {
				printw("-");
				if (!meterec->pos.inout)
					attron(A_REVERSE);
				else
					attron(A_BOLD);
			}
			else 
				printw(" ");

			color_port(meterec, port);

			printw("%s:in_%-2d",  meterec->jack_name, port+1);

			color_set(DEFAULT, NULL);

			if (meterec->ports[port].thru)
				printw("-> ");
			else 
				printw("   ");

			printw("%s:out_%-2d",  meterec->jack_name, port+1);

			attroff(A_BOLD);
			attroff(A_REVERSE);
		
		}
		else 
			for (i=0; i<(2*strlen(meterec->jack_name)+17); i++)
				printw(" ");

		if (meterec->pos.port==port)
			printw("-");
		else 
			printw(" ");
		
		if (*out) {
			if (jack_port_connected_to(meterec->ports[meterec->pos.port].output, *out)) {
				printw("+-");
				attron(A_BOLD);
			}
			else if (port==meterec->pos.port) {
				printw("+ ");
			}
			else
				printw("| ");
				
			if (meterec->pos.inout == CON_OUT)
				if (meterec->pos.con_out == line)
					attron(A_REVERSE);
				
			printw("%s",*out);
			attroff(A_BOLD);
			attroff(A_REVERSE);
			
			out++;
		} else {
			if (port<meterec->pos.port)
				printw("|");
			else if (port==meterec->pos.port)
				printw("+");
			else 
				printw("");
		}
		color_set(DEFAULT, NULL);
		printw("\n");
		
		if (port < meterec->n_ports)
			port ++;
			
		line ++;
  	}
        
	color_set(DEFAULT, NULL);

}
