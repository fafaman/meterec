#include <sndfile.h>
#include <jack/jack.h>
#include <curses.h>
#include "meterec.h"

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

    if ( port_p->playback_take ) 
    printw(" PLAYING take %d", port_p->playback_take);
  else 
    printw(" PLAYING empty take 0");
  
  if (port_p->name)
    printw(" | %s", port_p->name);

  printw(" | %.1fdB", port_p->db_in);
  printw(" (%.1fdB)", port_p->db_max_in);

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

void display_session(struct meterec_s *meterec, int y_pos, int x_pos) 
{
  unsigned int take, port;

  /* y - port */
  /* x - take */

  
  
  printw("  Take %2d ",x_pos);
  printw("%s",  meterec->takes[x_pos].port_has_track[y_pos]?"[CONTENT]":"[       ]" );
  printw("%s",  meterec->takes[x_pos].port_has_lock[y_pos]?"[LOCKED]":"[      ]" );
  printw("%s", (meterec->ports[y_pos].playback_take == x_pos)?"[PLAYING]":"[       ]" );
  
  printw("\n\n");

  for (port=0; port<meterec->n_ports; port++) {
  
    if (meterec->ports[port].record) 
      color_set(YELLOW, NULL);
    else 
      color_set(GREEN, NULL);
    
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

