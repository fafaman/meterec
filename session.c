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
#include <jack/session.h>

#include "meterec.h"

void session_callback(jack_session_event_t *event, void *arg) {
	
	// this is a direct reply and we dont have state to save here.
	// in a gtk app we would forward the event to the gui thread
	// using g_idle_add() and execute similar code there.
	
	char retval[100];
	struct meterec_s *meterec;
	
	
	meterec = (struct meterec_s *)arg ;
	
	snprintf (retval, 100, 
		"meterec -j %s -u %s", 
		meterec->jack_name, 
		event->client_uuid
		);
	event->command_line = strdup(retval);
	
	event->flags = JackSessionNeedTerminal;
	
	jack_session_reply( meterec->client, event );
	
	if (event->type == JackSessionSaveAndQuit) {
		jack_session_event_free (event);
		halt(0);
	}
	
	jack_session_event_free (event);
	
}


