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

#include "position.h"
#include "meterec.h"
#include "conf.h"

void time_sprint(struct time_s * time, char * string) {
	sprintf(string, "%u:%02u:%02u.%03u",time->h, time->m, time->s, time->ms);
	
}

void time_hms(struct time_s * time) {
	
	unsigned int rate = time->rate;
	unsigned int mil = (time->rate/1000);
	unsigned int frm = time->frm;
	
	time->ms =( frm / mil) % 1000; 
	time->s = ( frm / rate ) % 60;   frm /= 60;
	time->m = ( frm / rate ) % 60;   frm /= 60;
	time->h = ( frm / rate );
	
}

void time_hms2(struct time_s * time) {
	
	unsigned int rate = time->rate;
	
	time->h = (unsigned int) ( time->frm / rate ) / 3600;
	time->m = (unsigned int) ((time->frm / rate ) / 60 ) % 60;
	time->s = (unsigned int) ( time->frm / rate ) % 60;
	time->ms =(unsigned int) ( 1000*time->frm / rate ) % 1000;
	
}

void time_frm(struct time_s * time) {

	unsigned int mil = (time->rate/1000);
	
	time->frm =  (unsigned int) (
		time->h  * time->rate * 3600 +
		time->m  * time->rate * 60 +
		time->s  * time->rate +
		time->ms * mil 
		);
}
