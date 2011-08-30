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

/* type of events */
#define ALL 0
#define SEEK 1
#define LOOP 2
#define LOCK 3

/* queuees */
#define ALL 0
#define DISK 1
#define DISK_PENDING 2
#define JACK 3


void add_event(struct meterec_s *meterec, unsigned int type, unsigned int queue, jack_nframes_t old_playhead, jack_nframes_t new_playhead, unsigned int buffer_pos);
void find_last_event(struct meterec_s *meterec) ;
void rm_last_event(struct meterec_s *meterec) ;
struct event_s * find_first_event(struct meterec_s *meterec, unsigned int queue, unsigned int type);
void clear_event(struct meterec_s *meterec);
void rm_event(struct meterec_s *meterec, struct event_s *event);
