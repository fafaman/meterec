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

#include "meterec.h"
#include "conf.h"

void add_event(struct meterec_s *meterec, unsigned int type, unsigned int queue, jack_nframes_t old_playhead, jack_nframes_t new_playhead, unsigned int buffer_pos) {
	
	struct event_s *event;
	
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
	
	event->type = type;
	event->queue = queue;
	event->old_playhead = old_playhead;
	event->new_playhead = new_playhead;
	event->buffer_pos = buffer_pos;
	
}

struct event_s * last_event(struct meterec_s *meterec) {
	
	struct event_s *event;
	
	if (meterec->event == NULL)
		return NULL;
		
	event = meterec->event;
	
	while (event->next)
		event = event->next;
	
	return event;
}

struct event_s * find_first_event(struct meterec_s *meterec, unsigned int queue, unsigned int type) {
	
	struct event_s *event;
	int match_type, match_queue;
	
	if (meterec->event == NULL)
		return NULL;
		
	event = meterec->event;
	
	while (event) {
		
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
			
		if (match_type && match_queue) {
//			fprintf(meterec->fd_log, "Event type %d found in queue %d\n",event->type, event->queue);
			return event;
		}
//		fprintf(meterec->fd_log, "Event search more...\n",event->type, event->queue);

		event = event->next;
	}
	
	return NULL;
}

void rm_last_event(struct meterec_s *meterec) {
	
	struct event_s *event;
	
	if (meterec->event == NULL)
		return;
	
	event = last_event(meterec);
	
	if (event->prev) {
		event->prev->next = NULL;
	}
	else {
		meterec->event = NULL;
	}
	
	free(event);
}

void rm_all_event(struct meterec_s *meterec) {
		
	if (meterec->event == NULL)
		return;
	
	while (meterec->event)
		rm_last_event(meterec);
		
}

void rm_event(struct meterec_s *meterec, struct event_s *event) {

	
	if (event->prev)
		event->prev->next = event->next ;
	
	if (event->next)
		event->next->prev = event->prev ;
	
	if (event->prev == NULL && event->next == NULL)
		meterec->event = NULL;
		
	free(event);
	
}
