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
#include "position.h"
#include "meterec.h"
#include "display.h"
#include "disk.h"
#include "conf.h"
#include "ports.h"
#include "queue.h"
#include "keyboard.h"

char* realloc_freetext(char **name) 
{
	char *new;
	
	new = (char *) malloc( MAX_NAME_LEN + 1 );
	
	if (*name) 
		free(*name);
	
	*name = new;
	
	*new     = '_';
	*(new+1) = '\0';
	
	return new;
}


void *keyboard_thread(void *arg) {
	
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
			
			if (key == 10 || key == 27) {
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
		
		switch (meterec->display.view) {
		case EDIT: 
			
			switch (key) {
				
				/* 
				** Rename takes 
				*/
				case 'i' : /* rename the current take */
					text = realloc_freetext(&meterec->takes[x_pos].name);
					freetext = MAX_NAME_LEN;
					break;
				
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
				
				case 'i' : /* rename the current port */
					text = realloc_freetext(&meterec->ports[y_pos].name);
					freetext = MAX_NAME_LEN;
					break;
					
				case 'n': 
					meterec->display.names = !meterec->display.names ;
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
					if (meterec->pos.inout < CON_OUT)
						meterec->pos.inout = CON_IN;
					meterec->display.needs_update++;
					break;
				case KEY_RIGHT:
					meterec->pos.inout ++;
					if (meterec->pos.inout > CON_IN)
						meterec->pos.inout = CON_OUT;
					meterec->display.needs_update++;
					break;
				case 'c':
					if (meterec->pos.inout == CON_IN)
						register_connect_port(meterec, (char*)meterec->all_output_ports[meterec->pos.con_in], meterec->pos.port);
					else if (meterec->pos.inout == CON_OUT)
						register_connect_port(meterec, (char*)meterec->all_input_ports[meterec->pos.con_out], meterec->pos.port);
					meterec->display.needs_update++;
					break;
				case 'x':
					if (meterec->pos.inout == CON_IN)
						deregister_disconnect_port(meterec, (char*)meterec->all_output_ports[meterec->pos.con_in], meterec->pos.port);
					else if (meterec->pos.inout == CON_OUT)
						deregister_disconnect_port(meterec, (char*)meterec->all_input_ports[meterec->pos.con_out], meterec->pos.port);
					meterec->display.needs_update++;
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
			
			case 'T' : /* toggle pass thru on all ports */
				if ( meterec->ports[y_pos].thru ) 
					for ( port=0 ; port < meterec->n_ports ; port++) 
						meterec->ports[port].thru = 0;
				else 
					for ( port=0 ; port < meterec->n_ports ; port++) 
						meterec->ports[port].thru = 1;
				meterec->display.needs_update++;
				break;
			
			case 't' : /* toggle pass thru on this port */
				meterec->ports[y_pos].thru = !meterec->ports[y_pos].thru;
				meterec->display.needs_update++;
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
				if (meterec->display.view == PORT && meterec->pos.inout) {
					if (meterec->pos.inout == CON_IN)
						meterec->pos.con_in --;
					if (meterec->pos.con_in < 0)
						meterec->pos.con_in = meterec->pos.n_con_in;
					if (meterec->pos.inout == CON_OUT)
						meterec->pos.con_out --;
					if (meterec->pos.con_out < 0)
						meterec->pos.con_out = meterec->pos.n_con_out;
				} else {
					meterec->ports[meterec->pos.port].monitor = 0;
					if ( meterec->pos.port == 0 )
						meterec->pos.port = meterec->n_ports - 1;
					else
						meterec->pos.port--;
					meterec->ports[meterec->pos.port].monitor = 1;
				}
				meterec->display.needs_update++;
				break;
			
			case KEY_DOWN :
				if (meterec->display.view == PORT && meterec->pos.inout) {
					if (meterec->pos.inout == CON_IN)
						meterec->pos.con_in ++;
					if (meterec->pos.con_in > meterec->pos.n_con_in)
						meterec->pos.con_in = 0;
					if (meterec->pos.inout == CON_OUT)
						meterec->pos.con_out ++;
					if (meterec->pos.con_out > meterec->pos.n_con_out)
						meterec->pos.con_out = 0;
				} else {
					meterec->ports[meterec->pos.port].monitor = 0;
					if ( meterec->pos.port == meterec->n_ports - 1 )
						meterec->pos.port = 0;
					else 
						meterec->pos.port++;
					meterec->ports[meterec->pos.port].monitor = 1;
				}
				meterec->display.needs_update++;
				break;
			
			case 9: /* TAB */
				if (meterec->display.view==VU)
					meterec->display.view=EDIT;
				else if (meterec->display.view==EDIT) {
					meterec->display.view=PORT;
				}
				else if (meterec->display.view==PORT)
					meterec->display.view=VU;
				break;
			
			case 10: /* RETURN */
				if (meterec->playback_sts == ONGOING)
					stop(meterec);
				else {
					if (meterec->record_sts == OFF)
						start_record(meterec);
					if (meterec->playback_sts == OFF) 
						roll(meterec);
				}
				break;
			
			case 127: /* BACKSPACE */
			case 263: /* BACKSPACE */
				if (meterec->record_sts == ONGOING && meterec->playback_sts == ONGOING) 
					meterec->record_cmd = RESTART;
				else if (meterec->record_sts == OFF && meterec->playback_sts == OFF)
					start_record(meterec);
				else if (meterec->record_sts == ONGOING && meterec->playback_sts == OFF)
					cancel_record(meterec);
				break;
			
			case ' ':
				if (meterec->playback_sts == ONGOING && meterec->record_sts == OFF)
					stop(meterec);
				else if (meterec->playback_sts == OFF)
					roll(meterec);
				break;
			
			case '-': /* SUPR */
				clr_loop(meterec, BOUND_ALL);
				break;
			
			case '/': /* SUPR */
				clr_loop(meterec, BOUND_LOW);
				break;
			
			case '*': /* SUPR */
				clr_loop(meterec, BOUND_HIGH);
				break;
			
			case '+': 
				if (set_loop(meterec, meterec->jack.playhead)) {
					/* The disk tread cannot be aware of this loop as it 
					is already processing the data, 
					so let's seek to the begining of the loop ourselves */
					pthread_mutex_lock( &meterec->event_mutex );
					add_event(meterec, DISK, SEEK, MAX_UINT, meterec->loop.low, MAX_UINT);
					pthread_mutex_unlock( &meterec->event_mutex );
				}
				break;
			
			case 'Q':
			case 'q':
				meterec->keyboard_cmd = STOP;
				halt(0);
				break;
			
		}
		
		/* set index using SHIFT */
		if ( KEY_F(13) <= key && key <= KEY_F(24) ) 
			meterec->seek_index[key - KEY_F(13)] = meterec->jack.playhead ;
		
		/* set loop using CONTROL */
		if ( KEY_F(25) <= key && key <= KEY_F(36) ) {
			/* store index before setting loop if index is free */
			if (meterec->seek_index[key - KEY_F(25)] == MAX_UINT) {
				meterec->seek_index[key - KEY_F(25)] = meterec->jack.playhead ;
				if (set_loop(meterec, meterec->jack.playhead)) {
					/* The disk tread cannot be aware of this loop as it 
					is already processing the data, 
					so let's seek to the begining of the loop ourselves */
					pthread_mutex_lock( &meterec->event_mutex );
					add_event(meterec, DISK, SEEK, MAX_UINT, meterec->loop.low, MAX_UINT);
					pthread_mutex_unlock( &meterec->event_mutex );
				}
			}
			else
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
		
	}
	
	return 0;
	
}
