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

void add_event(struct meterec_s *meterec, unsigned int type, jack_nframes_t old_playhead, jack_nframes_t new_playhead, unsigned int buffer_pos) {
	
	if (meterec->event == NULL) {
		meterec->event = (struct event_s *)malloc(sizeof(struct event_s));
		meterec->event->prev = NULL;
		meterec->event->next = NULL;
	}
	else {
		
		while (meterec->event->next)
			meterec->event = meterec->event->next;
		
		meterec->event->next = (struct event_s *)malloc(sizeof(struct event_s));
		meterec->event->next->prev = meterec->event;
		meterec->event->next->next = NULL;
		
		meterec->event = meterec->event->next;
	}
	
	meterec->event->type = type;
	meterec->event->queue = COMMAND;
	meterec->event->old_playhead = old_playhead;
	meterec->event->new_playhead = new_playhead;
	meterec->event->buffer_pos = buffer_pos;
	
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
	int match_type, match_queue
	
	if (meterec->event == NULL)
		return NULL;
		
	event = meterec->event;
	
	while (event->next) {
		
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
			
		if (match_type && match_queue)
			return event;
		
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

void rm_first_event(struct meterec_s *meterec) {
		
	struct event_s *event;
	
	if (meterec->event == NULL)
		return;
	
	if (meterec->event->next) {
		meterec->event = meterec->event->next;
		free(meterec->event->prev);
		meterec->event->prev = NULL;
	}
	else {
		free(meterec->event);
		meterec->event = NULL;
	}
	
}

void rm_all_event(struct meterec_s *meterec) {
		
	if (meterec->event == NULL)
		return;
	
	while (meterec->event)
		rm_last_event(meterec);
		
}
