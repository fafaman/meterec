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
#include <string.h>
#include <unistd.h>

#include <sndfile.h>
#include <jack/jack.h>
#include <curses.h>

#include "meterec.h"
#include "queue.h"
#include "conf.h"

void add_event(struct meterec_s *meterec, unsigned int queue, unsigned int type, jack_nframes_t old_playhead, jack_nframes_t new_playhead, unsigned int buffer_pos) {
	
	struct event_s *event;
	static unsigned int id=0;
	
	event = meterec->event;
	
	if (event == NULL) {
		event = (struct event_s *)malloc(sizeof(struct event_s));
		event->prev = NULL;
		event->next = NULL;
		
		meterec->event = event;
	}
	else {
		
		while (event->next)
			event = event->next;
		
		event->next = (struct event_s *)malloc(sizeof(struct event_s));
		event->next->prev = event;
		event->next->next = NULL;
		
		event = event->next;
	}
	
	event->id = id;
	event->type = type;
	event->queue = queue;
	event->old_playhead = old_playhead;
	event->new_playhead = new_playhead;
	event->buffer_pos = buffer_pos;
	
}

int event_match(struct event_s *event, unsigned int queue, unsigned int type) {
	
	int match_type, match_queue;
	
	if (type)
		if (event->type == type)
			match_type = 1;
		else 
			match_type = 0;
	else 
		match_type = 1;
	
	if (queue)
		if (event->queue == queue)
			match_queue = 1;
		else 
			match_queue = 0;
	else 
		match_queue = 1;
	
	return (match_type && match_queue);

}

struct event_s * find_first_event(struct meterec_s *meterec, unsigned int queue, unsigned int type) {
	
	struct event_s *event;
	
	if (meterec->event == NULL)
		return NULL;
		
	event = meterec->event;
	
	while (event) {
		
		if (event_match(event, queue, type)) 
			return event;
		
		event = event->next;
	}
	
	return NULL;
}

struct event_s * find_last_event(struct meterec_s *meterec, unsigned int queue, unsigned int type) {
	
	struct event_s *event;
	
	if (meterec->event == NULL)
		return NULL;
		
	event = meterec->event;
	
	while (event->next)
		event = event->next;
	
	while (event) {
		
		if (event_match(event, queue, type)) 
			return event;
		
		event = event->prev;
	}
	
	return NULL;
}

void rm_event(struct meterec_s *meterec, struct event_s *event) {
	
	if (event == NULL)
		return;
	
	if (event->prev)
		event->prev->next = event->next ;
	else 
		meterec->event = event->next ;
	
	if (event->next)
		event->next->prev = event->prev ;
	
	if (event->prev == NULL && event->next == NULL)
		meterec->event = NULL;
		
	free(event);
	
}

void find_rm_events(struct meterec_s *meterec, unsigned int queue, unsigned int type) {
		
	struct event_s *event;
	
	event = meterec->event;
	
	while (event) {
		event = find_first_event(meterec, queue, type);
		rm_event(meterec, event);
	}
	
}

void event_queue_print(struct meterec_s *meterec, unsigned int where) {
	
	struct event_s *event;
	
	if (where == CURSES) 
		printw(">-------------------------------------------------------\n");
		
	if (where == STDOUT) 
		printf(">-------------------------------------------------------\n");
		
	if (where == LOG) 
		fprintf(meterec->fd_log, ">-------------------------------------------------------\n");
	
	event = meterec->event ;
	while (event) {
		event_print(meterec, where, event);
		event = event->next;
	}
	
	if (where == CURSES) 
		printw("^-------------------------------------------------------\n");
		
	if (where == STDOUT) 
		printf("^-------------------------------------------------------\n");
		
	if (where == LOG) 
		fprintf(meterec->fd_log, "^-------------------------------------------------------\n");
	
}

void event_print(struct meterec_s *meterec, unsigned int where, struct event_s *event) {
	
	const char *stype = NULL;
	const char *squeue = NULL;
	char out[100] = "";
	
	switch (event->type) {
		case ALL:  stype = "ALL"; break;
		case SEEK: stype = "SEEK"; break;
		case LOCK: stype = "LOCK"; break;
		case LOOP: stype = "LOOP"; break;
	}
	
	switch (event->queue) {
		case ALL:  squeue = "ALL"; break;
		case DISK: squeue = "DISK"; break;
		case JACK: squeue = "JACK"; break;
		case PEND: squeue = "PEND"; break;
	}
	
	sprintf(out, "id %d - queue %s - type %s - old %d - new %d - buf %d", event->id, squeue, stype, event->old_playhead, event->new_playhead, event->buffer_pos);
	
	if (where == CURSES) 
		printw("%s\n", out);
		
	if (where == STDOUT) 
		printf("%s\n", out);
		
	if (where == LOG) 
		fprintf(meterec->fd_log, "%s\n", out);
		
}



