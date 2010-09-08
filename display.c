#include <sndfile.h>
#include <jack/jack.h>
#include <curses.h>
#include "meterec.h"

void display_session(struct meterec_s *meterec, int y_pos, int x_pos) 
{
  unsigned int take, port;

  /* y - port */
  /* x - take */


  printw("  Port %2d ", y_pos+1);
  if (meterec->ports[y_pos].record==REC)
     printw("[REC]");
  else if (meterec->ports[y_pos].record==OVR)
     printw("[OVR]");
  else if (meterec->ports[y_pos].record==DUB)
     printw("[DUB]");
  else 
     printw("[   ]");
     
  if ( meterec->ports[y_pos].playback_take ) 
    printw(" PLAYING take %d", meterec->ports[y_pos].playback_take);
  else 
    printw(" PLAYING empty take 0");
    
  printw("\n");
  
  
  printw("  Take %2d ",x_pos);
  printw("%s",  meterec->takes[x_pos].port_has_track[y_pos]?"[CONTENT]":"[       ]" );
  printw("%s",  meterec->takes[x_pos].port_has_lock[y_pos]?"[LOCKED]":"[      ]" );
  printw("%s", (meterec->ports[y_pos].playback_take == x_pos)?"[PLAYING]":"[       ]" );
  
  printw("\n");

  for (port=0; port<meterec->n_ports; port++) {
  
    if (meterec->ports[port].record) 
      color_set(YELLOW, NULL);
    else 
      color_set(GREEN, NULL);
    
    if (y_pos == port) 
       attron(A_REVERSE);
    else 
       attroff(A_REVERSE);
  
    printw("%02d",port+1,meterec->ports[port].playback_take);

    if ( meterec->ports[port].record == REC )
      printw("R");
    else if ( meterec->ports[port].record == DUB )
      printw("D");
    else if ( meterec->ports[port].record == OVR )
      printw("O");
    else 
      printw("=");
      
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
        printw("L");
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
        
}

