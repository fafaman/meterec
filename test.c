/*

  meterec 
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009-2011 Fabrice Lebas
  
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

void p(struct meterec_s *meterec) {
	
	struct event_s *event;
	
	event = meterec->event ;
	while (event) {
		printf("queue %d - type %d - old %d - new %d - buf %d\n", event->queue , event->type,event->old_playhead, event->new_playhead, event->buffer_pos);
		event = event->next;
	}
	printf("================================================================================\n");
}

int main(int argc, char *argv[])
{
	int opt;
	struct event_s *event;
	struct meterec_s *meterec;
	
	while ((opt = getopt(argc, argv, "hv")) != -1) {
		switch (opt) {
				
			case 'h':
			case 'v':
			default:
				/* Show usage/version information */
				printf( "%s: No options available!", argv[0] );
				break;
		}
	}
	
	
	meterec = (struct meterec_s *) malloc( sizeof(struct meterec_s) ) ;
	
	meterec->event = NULL;
	
	
	add_event(meterec,1,1,1,1,1);
	p(meterec);
	
	event = find_first_event(meterec, 1, 0);
	rm_event(meterec, event);
	p(meterec);
	
	add_event(meterec,1,1,1,1,1);
	add_event(meterec,2,1,1,1,2);
	add_event(meterec,3,1,1,1,3);
	p(meterec);
	
	event = find_first_event(meterec, 1, 0);
	rm_event(meterec, event);
	p(meterec);
	
	event = find_first_event(meterec, 3, 0);
	rm_event(meterec, event);
	p(meterec);
	
	event = find_first_event(meterec, 2, 0);
	rm_event(meterec, event);
	p(meterec);
	
	event = find_first_event(meterec, 1, 0);
	rm_event(meterec, event);
	p(meterec);
	
	add_event(meterec,1,1,1,1,1);
	add_event(meterec,2,1,1,1,2);
	add_event(meterec,2,1,1,1,3);
	add_event(meterec,3,1,1,1,4);
	add_event(meterec,2,1,1,1,5);
	add_event(meterec,3,1,1,1,6);
	p(meterec);
	
	find_rm_events(meterec, 2, 0);
	p(meterec);
	
	find_rm_events(meterec, 1, 0);
	p(meterec);
	
	find_rm_events(meterec, 2, 0);
	p(meterec);
	
	find_rm_events(meterec, 3, 0);
	p(meterec);
	
	free(meterec);
	
	return 0;

}



/*
int main(int argc, char *argv[])
{
	struct time_s time;
	char time_str[40];
	
	time.rate = 44100;
	time.frm = 123456789;
	
	time_hms(&time);
	time_sprint(&time, time_str); printf("%s %dh\n", time_str, time.frm);
		
	time_frm(&time);
	time_sprint(&time, time_str); printf("%s %df\n", time_str, time.frm);
	time_hms(&time);
	time_sprint(&time, time_str); printf("%s %dh\n", time_str, time.frm);
	
	time_frm(&time);
	time_sprint(&time, time_str); printf("%s %df\n", time_str, time.frm);
	time_hms(&time);
	time_sprint(&time, time_str); printf("%s %dh\n", time_str, time.frm);
	
	time_frm(&time);
	time_sprint(&time, time_str); printf("%s %df\n", time_str, time.frm);
	time_hms(&time);
	time_sprint(&time, time_str); printf("%s %dh\n", time_str, time.frm);
	
	time_frm(&time);
	time_sprint(&time, time_str); printf("%s %df\n", time_str, time.frm);
	time_hms(&time);
	time_sprint(&time, time_str); printf("%s %dh\n", time_str, time.frm);
	
	time_frm(&time);
	time_sprint(&time, time_str); printf("%s %df\n", time_str, time.frm);
	time_hms(&time);
	time_sprint(&time, time_str); printf("%s %dh\n", time_str, time.frm);
	
	printf("%d %d\n", (890000/44100 + 5)/10, (44/44 % 1000) );
	
	
	return 0;
}
*/
