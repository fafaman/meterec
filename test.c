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


int main(int argc, char *argv[])
{
	struct time_s time;
	char time_str[40];
	
	time.rate = 44100;
	time.frm = 44;
	
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
	
	printf("%d %d\n", 44100/1000, (44/44 % 1000) );
	
	
	return 0;
}
