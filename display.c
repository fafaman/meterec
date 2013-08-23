/*

  meterec
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009-2013 Fabrice Lebas
  
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

#include <sndfile.h>
#include <jack/jack.h>
#include <curses.h>
#include "position.h"
#include "meterec.h"
#include "disk.h"
#include "ports.h"
#include "display.h"

WINDOW * mainwin = NULL;

void display_init_curses(struct meterec_s *meterec) {
	
	fprintf(meterec->fd_log, "Starting ncurses interface...\n");
	
	mainwin = initscr();
	
	if ( mainwin == NULL ) {
		fprintf(meterec->fd_log, "Error initialising ncurses.\n");
		exit(1);
	}
	
	curs_set(0);
	start_color();
	
	/* choose our color pairs */
	init_pair(GREEN,  COLOR_GREEN,   COLOR_BLACK);
	init_pair(YELLOW, COLOR_YELLOW,  COLOR_BLACK);
	init_pair(BLUE,   COLOR_BLUE,    COLOR_BLACK);
	init_pair(RED,    COLOR_RED,     COLOR_BLACK);
	
	clear();
	
	meterec->curses_sts = ONGOING;
	
}

void display_cleanup_curses(struct meterec_s *meterec) {
	
	delwin(mainwin);
	endwin();
	refresh();
	fprintf(meterec->fd_log, "Stopped ncurses interface.\n");
	
	meterec->curses_sts = OFF;
}


void display_init_windows(struct meterec_s *meterec, unsigned int w, unsigned int h) {
	
	unsigned int p = meterec->n_ports;
	
	/*                                h,     w,   x,    y */
	meterec->display.wrds = newwin(    1,   20,   0,    0);
	meterec->display.wwrs = newwin(    1,   20,   1,    0);
	meterec->display.wttl = newwin(    2, w-59,   0,   20);
	meterec->display.wloo = newwin(    1,   39,   0, w-39);
	meterec->display.wcpu = newwin(    1,   39,   1, w-39);
	meterec->display.wleg = newwin(    2,    9,   2,    0);
	meterec->display.wsc1 = newwin(    2,  w-9,   2,    9);
	meterec->display.wtak = newwin(    2,  w-9,   2,    9);
	meterec->display.wpor = newwin(    p,    9,   4,    0);
	meterec->display.wses = newwin(  h-5,  w-9,   4,    9);
	meterec->display.wvum = newwin(    p,  w-9,   4,    9);
	meterec->display.wsc2 = newwin(    2,  w-9, p+4,    9);
	meterec->display.wclr = newwin(h-p-7,    w, p+6,    0);
	meterec->display.wcon = newwin(  h-3,    w,   2,    0);
	meterec->display.wbot = newwin(    1, w-17, h-1,    0);
	meterec->display.wbdb = newwin(    1,   17, h-1, w-17);
	
	
}

void display_changed_size(struct meterec_s *meterec) {
	
	unsigned int w, h;
	
	getmaxyx(stdscr, h, w);
	
	if (meterec->display.width == w)
		if (meterec->display.height == h)
			return;
	
	flushinp();
	clear();
	refresh();
	
	meterec->display.width = w;
	meterec->display.height = h;
	
	display_init_windows(meterec, w, h);
	
	display_init_scale(0, meterec->display.wsc1);
	display_init_scale(1, meterec->display.wsc2);
	
	display_init_title(meterec);
	display_init_legend(meterec->display.wleg);
	display_init_clr(meterec->display.wclr);
	
	display_refresh_view(meterec);
	
}

void display_changed_view(struct meterec_s *meterec) {
	
	static unsigned int view = NONE;
	
	if (meterec->display.view == view)
		return;
	
	view = meterec->display.view;
	
	display_refresh_view(meterec);
	
}

void display_refresh_view(struct meterec_s *meterec) {
	
	touchwin(meterec->display.wttl);
	wnoutrefresh(meterec->display.wttl);
	
	switch (meterec->display.view) {
		
		case VU : 
			touchwin(meterec->display.wsc1);
			touchwin(meterec->display.wsc2);
			touchwin(meterec->display.wleg);
			touchwin(meterec->display.wclr);
			wnoutrefresh(meterec->display.wsc1);
			wnoutrefresh(meterec->display.wsc2);
			wnoutrefresh(meterec->display.wleg);
			wnoutrefresh(meterec->display.wclr);
			break;
		
		case EDIT :
			
			break;
		
		case PORT :
			retreive_connected_ports(meterec);
			retreive_existing_ports(meterec);
			filter_existing_ports(meterec->all_input_ports, meterec->jack_name);
			filter_existing_ports(meterec->all_output_ports, meterec->jack_name);
			count_all_io_ports(meterec);
			display_connections_init(meterec);
			display_connections_fill_ports(meterec);
			display_connections_fill_conns(meterec);
			break;
	}
	
}

void display_changed_static_content(struct meterec_s *meterec) {
	
	if (meterec->display.needs_update == meterec->display.needed_update)
		return;
	
	switch (meterec->display.view) {
		
		case VU : 
			break;
		
		case EDIT :
			break;
		
		case PORT :
			display_connections_fill_ports(meterec);
			display_connections_fill_conns(meterec);
			break;
	}
	
	meterec->display.needed_update++;
}

void display_dynamic_content(struct meterec_s *meterec) {
	
	display_rd_status(meterec);
	display_wr_status(meterec);
	display_loop(meterec);
	display_cpu_load_digital(meterec);
	
	switch (meterec->display.view) {
		
		case VU : 
			display_ports_modes(meterec);
			display_meter(meterec);
			break;
		
		case EDIT :
			display_ports_modes(meterec);
			display_take_info(meterec);
			display_session(meterec);
			break;
		
		case PORT :
			display_ports_tiny_meters(meterec);
			
			break;
	}
	
	display_port_info(meterec);
	display_port_db_digital(meterec);
	
}

void display_debug_windows(struct meterec_s *meterec) {
	
	/* ALL */
	display_box(meterec->display.wrds);
	display_box(meterec->display.wwrs);
	display_box(meterec->display.wttl);
	display_box(meterec->display.wloo);
	display_box(meterec->display.wcpu);
	
	/* VU */
	if (meterec->display.view==VU) {
		display_box(meterec->display.wsc1);
		display_box(meterec->display.wpor);
		display_box(meterec->display.wvum);
		display_box(meterec->display.wsc2);
		display_box(meterec->display.wclr);
	}
	
	/* EDIT */
	if (meterec->display.view==EDIT) {
		display_box(meterec->display.wtak);
		display_box(meterec->display.wses);
	}
	
	/* PORTs */
	else if (meterec->display.view==PORT) {
		display_box(meterec->display.wcon);
		display_box(meterec->display.wpoo);
		display_box(meterec->display.wci );
		display_box(meterec->display.wpi );
		display_box(meterec->display.wt  );
		display_box(meterec->display.wpo );
		display_box(meterec->display.wco );
		display_box(meterec->display.wpii);
	
	}
	
	/* ALL */
	display_box(meterec->display.wbot);
	display_box(meterec->display.wbdb);

}

void display_box(WINDOW *win) {
	
	wclear(win);
	box(win, 0,0);
	wnoutrefresh(win);
	
}

void display_view_change(struct meterec_s *meterec) {
	
}

static int iec_scale(float db, int size) {
	
	float def = 0.0f; /* Meter deflection %age */
	
	if (db < -70.0f) 
		def = 0.0f;
	else if (db < -60.0f) 
		def = (db + 70.0f) * 0.25f;
	else if (db < -50.0f) 
		def = (db + 60.0f) * 0.5f + 2.5f;
	else if (db < -40.0f) 
		def = (db + 50.0f) * 0.75f + 7.5f;
	else if (db < -30.0f) 
		def = (db + 40.0f) * 1.5f + 15.0f;
	else if (db < -20.0f) 
		def = (db + 30.0f) * 2.0f + 30.0f;
	else if (db < 0.0f) 
		def = (db + 20.0f) * 2.5f + 50.0f;
	else 
		def = 100.0f;
	
	return (int)((def / 100.0f) * ((float) size));
}


void display_init_title(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wttl;
	unsigned int len = strlen(meterec->session) + 4;
	unsigned int w = getmaxx(win);
	unsigned int pos = (w - len) / 2;
	
	wclear(win);
	
	mvwprintw(win, 0, pos, "~ %s ~", meterec->session);
	
	wnoutrefresh(win);
}

void display_fill_remaining(unsigned int remain) {
	unsigned int i;
	unsigned int width, y, x;
	int spaces;
	
	width = getmaxx(stdscr);
	getyx(stdscr, y, x); (void)y;
	spaces = width - x - remain;
	
	if (spaces < 0)
		return;
	
	for (i=0; i<spaces; i++) 
		printw(" ");
}

void display_right_aligned(char *message, unsigned int remain) {
	unsigned int i, len;
	unsigned int width, y, x;
	int spaces;
	
	len =strlen(message);
	
	width = getmaxx(stdscr);
	getyx(stdscr, y, x); (void)y;
	spaces = width - x - remain - len;
	
	if (spaces < 0)
		spaces += len;
		
	if (spaces < 0)
		return;
	
	for (i=0; i<spaces; i++) 
		printw(" ");
	
	if ( spaces == width - x - remain - len)
		printw("%s",message);
}

void display_port_info(struct meterec_s *meterec) {
	
	unsigned int len, w, take, length;
	unsigned int port = meterec->pos.port;
	struct port_s *port_p = &meterec->ports[port];
	char *take_name = NULL;
	char *port_name = port_p->name;
	WINDOW *win = meterec->display.wbot;
	
	wclear(win);
	
	wprintw(win, "Port %2d ", port+1);
	
	if (port_p->playback_take)
		take_name = meterec->takes[port_p->playback_take].name;
	
	if (take_name == NULL)
		take_name = "";
	
	if (port_p->record==REC)
		wprintw(win, "|REC|");
	else if (port_p->record==OVR)
		wprintw(win, "|OVR|");
	else if (port_p->record==DUB)
		wprintw(win, "|DUB|");
	else 
		wprintw(win, "|   |");
	
	if (port_p->thru)
		wprintw(win, "THRU|");
	else 
		wprintw(win, "    |");
	
	if (port_p->mute)
		wprintw(win, "MUTED|");
	else 
		wprintw(win, "     |");
	
	take = meterec->ports[port].playback_take;
	length = meterec->takes[take].info.frames + meterec->takes[take].offset ;
	if ( !take || length < meterec->jack.playhead )
		wprintw(win, "EOT|");
	else 
		wprintw(win, "   |");
	
	if ( port_p->playback_take ) 
		wprintw(win, " PLAYING take %d {%s}", port_p->playback_take, take_name);
	else 
		wprintw(win, " PLAYING no take");
	
	len = strlen(port_name);
	w = getmaxx(win);
	
	mvwprintw(win, 0, w-len-2, "(%s)", port_name);
	
	wnoutrefresh(win);
	
}

void display_port_db_digital(struct meterec_s *meterec) {
	
	unsigned int port = meterec->pos.port;
	struct port_s *port_p = &meterec->ports[port];
	WINDOW *win = meterec->display.wbdb;
	
	wclear(win);
	
	if (meterec->display.vu_bound == IN) {
		wprintw(win, "%5.1fdB ", port_p->db_in);
		if (port_p->clip_in) {
			wcolor_set(win, RED, NULL);
			wattron(win, A_REVERSE);
		}
		wprintw(win, "(%5.1fdB)", port_p->db_max_in);
	}
	else if (meterec->display.vu_bound == OUT) {
		wprintw(win, "%5.1fdB ", port_p->db_out);
		if (port_p->clip_out) {
			wcolor_set(win, RED, NULL);
			wattron(win, A_REVERSE);
		}
		wprintw(win, "(%5.1fdB)", port_p->db_max_out);
	}
	else if (meterec->display.vu_bound == NONE) {
		wprintw(win, "---.-dB (---.-dB)");
	}
	
	wattroff(win, A_REVERSE);
	wcolor_set(win, DEFAULT, NULL);
	
	wnoutrefresh(win);
	
}

void display_tiny_meter(struct meterec_s *meterec, unsigned int port, int side, WINDOW *win) {
	
	/*char *blink = " \0.\0o\0O\0*\0X\0";*/
	
	char *blink = " \0.\0-\0+\0*\0X\0";
	
	int pos = 0;
	
	if (side == OUT) 
		pos = iec_scale( meterec->ports[port].db_out, 5);
	
	if (side == IN) 
		pos = iec_scale( meterec->ports[port].db_in, 5);
	
	wprintw(win, blink + 2*pos);
	
}

void display_ports_tiny_meters(struct meterec_s *meterec) {
	
	unsigned int port;
	
	wclear(meterec->display.wtmi);
	wclear(meterec->display.wtmo);
	
	for (port=0; port < meterec->n_ports; port++) {
		
		wmove(meterec->display.wtmi, port, 0);
		wmove(meterec->display.wtmo, port, 0);
		
		display_tiny_meter(meterec, port, IN, meterec->display.wtmi);
		display_tiny_meter(meterec, port, OUT, meterec->display.wtmo);
		
	}
	
	wnoutrefresh(meterec->display.wtmi);
	wnoutrefresh(meterec->display.wtmo);
}

void display_ports_modes(struct meterec_s *meterec) {
	
	unsigned int port, take, length;
	WINDOW *win; 
	
	win = meterec->display.wpor;
	
	wclear(win);
	
	for (port=0; port < meterec->n_ports; port++) {
		
		mvwprintw(win, port, 0, "%02d",port+1);
		
		display_tiny_meter(meterec, port, IN, win);
		
		if ( meterec->ports[port].record == REC )
			wprintw(win, "R");
		else if ( meterec->ports[port].record == DUB )
			wprintw(win, "D");
		else if ( meterec->ports[port].record == OVR )
			wprintw(win, "O");
		else 
			wprintw(win, " ");
		
		if ( meterec->ports[port].thru )
			wprintw(win, "T");
		else 
			wprintw(win, " ");
		
		if ( meterec->ports[port].mute )
			wprintw(win, "M");
		else 
			wprintw(win, " ");
			
		take = meterec->ports[port].playback_take;
		length = meterec->takes[take].info.frames + meterec->takes[take].offset ;
		if ( !take || length < meterec->jack.playhead )
			wprintw(win, "E");
		else 
			wprintw(win, " ");
		
		display_tiny_meter(meterec, port, OUT, win);
		
	}
	
	wnoutrefresh(win);
}

static void color_port(struct meterec_s *meterec, unsigned int port, WINDOW *win) {
	
	if ( meterec->ports[port].record ) 
		if (meterec->record_sts == ONGOING)
			wcolor_set(win, RED, NULL);
		else 
			wcolor_set(win, YELLOW, NULL);
	else 
		if (meterec->ports[port].mute)
			wcolor_set(win, DEFAULT, NULL);
		else 
			wcolor_set(win, GREEN, NULL);
}

void display_meter(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wvum;
	unsigned int w = getmaxx(win);
	unsigned int port, size_in, size_out, len;
	unsigned int acs=32, size=0, dkmax=0, dkpeak=0;
	unsigned int side=meterec->display.vu_bound;
	int clipped=0;
	char *name;
	
	wclear(win);
	
	for ( port=0 ; port < meterec->n_ports ; port++) {
		
		size_in = iec_scale(meterec->ports[port].db_in, w-1);
		
		if (size_in > meterec->ports[port].dkmax_in)
			meterec->ports[port].dkmax_in = size_in;
		
		if (size_in > meterec->ports[port].dkpeak_in) {
			meterec->ports[port].dkpeak_in = size_in;
			meterec->ports[port].dktime_in = 0;
		} 
		else if (meterec->ports[port].dktime_in++ > meterec->display.decay_len) {
			meterec->ports[port].dkpeak_in = size_in;
		}
		
		
		size_out = iec_scale(meterec->ports[port].db_out, w-1);
		
		if (size_out > meterec->ports[port].dkmax_out)
			meterec->ports[port].dkmax_out = size_out;
		
		if (size_out > meterec->ports[port].dkpeak_out) {
			meterec->ports[port].dkpeak_out = size_out;
			meterec->ports[port].dktime_out = 0;
		} 
		else if (meterec->ports[port].dktime_out++ > meterec->display.decay_len) {
			meterec->ports[port].dkpeak_out = size_out;
		}
		
		if (side == IN) {
			size = size_in;
			dkpeak = meterec->ports[port].dkpeak_in;
			dkmax = meterec->ports[port].dkmax_in;
			clipped = meterec->ports[port].clip_in;
			acs = ACS_BOARD;
		}
		
		if (side == OUT) {
			size = size_out;
			dkpeak = meterec->ports[port].dkpeak_out;
			dkmax = meterec->ports[port].dkmax_out;
			clipped = meterec->ports[port].clip_out;
			acs = ACS_DIAMOND;
		}
		
		color_port(meterec, port, win);
		
		if (meterec->pos.port == port) {
			wattron(win, A_REVERSE);
			mvwhline(win, port, 0, 32, w);
		}
		else 
			wattroff(win, A_REVERSE);
		
		name = meterec->ports[port].name;
		if (meterec->display.names && name) {
			len = strlen(name);
			mvwprintw(win, port, w-len-5, "%s", name);
		}
		
		if (side) {
			mvwhline(win, port, 0, acs, size);
			mvwaddch(win, port, dkpeak, acs);
			//mvwaddch(win, port, dkmax, ACS_PLUS);
			mvwprintw(win, port, dkmax, "X");
		}
		
		if (clipped) {
			wcolor_set(win, RED, NULL);
			wattron(win, A_REVERSE);
			mvwprintw(win, port, w-1, "C");
			wattroff(win, A_REVERSE);
			wcolor_set(win, DEFAULT, NULL);
		}
	}
	
	/*
	wattroff(win, A_REVERSE);
	wcolor_set(win, DEFAULT, NULL);
	*/
	
	wnoutrefresh(win);
}

void display_meter_old(struct meterec_s *meterec, int display_names, int decay_len)
{
	int size_out, size_in, i;
	unsigned int port, width;
	WINDOW *win = meterec->display.wvum;;
	
	wclear(win);
	
	width = getmaxx(win);
	
	for ( port=0 ; port < meterec->n_ports ; port++) {
		
		color_port(meterec, port, win);
		
		if (meterec->pos.port == port) 
			wattron(win, A_REVERSE);
		else 
			wattroff(win, A_REVERSE);
		
		wmove(win, port, 0);
		
		size_in = iec_scale( meterec->ports[port].db_in, width );
		size_out = iec_scale( meterec->ports[port].db_out, width );
		
		if (size_in > meterec->ports[port].dkmax_in)
			meterec->ports[port].dkmax_in = size_in;
		
		if (size_in > meterec->ports[port].dkpeak_in) {
			meterec->ports[port].dkpeak_in = size_in;
			meterec->ports[port].dktime_in = 0;
		} 
		else if (meterec->ports[port].dktime_in++ > decay_len) {
			meterec->ports[port].dkpeak_in = size_in;
		}
		
		for ( i=0; i<width; i++) {
			
			if (display_names)
				if (i == width/5) 
					if (meterec->ports[port].name) {
						wprintw(win, "%s",meterec->ports[port].name);
						i += strlen(meterec->ports[port].name);
					}
			
			if (i < size_in-1) {
				wprintw(win, "#");
			}
			else if ( i==meterec->ports[port].dkpeak_in-1 ) {
				wprintw(win, "I");
			}
			else if ( i==meterec->ports[port].dkmax_in-1 ) {
				if (i>width-3)
					wprintw(win, "X");
				else
					wprintw(win, ":");
			}
			else if ( i < size_out-1 ) {
				wprintw(win, "-");
			}
			else {
				wprintw(win, " ");
			}
		}
	}
	
	wattroff(win, A_REVERSE);
	wcolor_set(win, DEFAULT, NULL);
	
	wnoutrefresh(win);
}


void display_init_legend(WINDOW *win) {
	
	unsigned int w = getmaxx(win);
	wclear(win);
	
	mvwprintw(win, 0, 0, "PPiRTMEo");
	mvwhline(win, 1, 0, 0, w-1);
	
	wnoutrefresh(win);
	
}

void display_init_clr(WINDOW *win) {
	
	wclear(win);
	wnoutrefresh(win);
	
}


void display_init_scale(int side, WINDOW *win) {
	
	unsigned int i=0, width;
	const int marks[12] = { 0, -3, -5, -10, -15, -20, -25, -30, -35, -40, -50, -60 };
	
	if (side)
		side = 1;
	
	wclear(win);
	
	width = getmaxx(win);
	
	mvwhline(win, !side, 0, 0, width);
	
	/* 'draw' on each of the db marks */
	for(i=0; i < 12; i++) {
		char mark[5];
		int pos = iec_scale( marks[i], width ) - 1;
		int spos, slen;
		
		/* Create string of the db value */
		snprintf(mark, 4, "%d", marks[i]);
		
		/* Position the label string */
		slen = strlen(mark);
		spos = pos - (slen/2);
		if (spos<0) 
			spos=0;
		if (spos+slen > width) 
			spos = width - slen;
		
		mvwprintw(win, side, spos, "%s", mark);
		
		/* Position ticks along the scale */
		mvwaddch(win, !side, pos, ACS_PLUS);
		
	}
	
}

void display_rd_buffer(struct meterec_s *meterec, WINDOW *win) {
	int size, i;
	static int peak=0;
	static char *pedale = "|";
	const int width = 11;
	
	size = width * read_disk_buffer_level(meterec);
	
	if (size > peak && meterec->playback_sts == ONGOING) 
		peak = size;
		
	
	for (i=0; i<width; i++) {
		if (i == peak-1)
			wprintw(win, ":");
		else if (i > size-1)
			wprintw(win, "I");
		else 
			wprintw(win, " ");
	}
	wprintw(win, "%s", pedale);
	
	if (meterec->playback_sts==ONGOING) {
		if      (*pedale=='/')
			pedale = "-";
		else if (*pedale=='-')
			pedale = "\\";
		else if (*pedale=='\\')
			pedale = "|";
		else if (*pedale=='|')
			pedale = "/";
	}
	
}

void display_wr_buffer(struct meterec_s *meterec, WINDOW *win) {
	int size, i;
	static int peak=0;
	static char *pedale = "|";
	const int width = 11;
	
	size = width * write_disk_buffer_level(meterec);
	
	if (size > peak) 
		peak = size;
		
	wprintw(win, "%s", pedale);
	for (i=0; i<width; i++) {
		if (i < size-1)
			wprintw(win, "I");
		else if (i == peak-1)
			wprintw(win, ":");
		else 
			wprintw(win, " ");
	}
	
	if (meterec->record_sts==ONGOING) {
		if      (*pedale=='/')
			pedale = "-";
		else if (*pedale=='-')
			pedale = "\\";
		else if (*pedale=='\\')
			pedale = "|";
		else if (*pedale=='|')
			pedale = "/";
	}
	
}

void display_cpu_load_digital(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wcpu;
	
	wclear(win);
	
	mvwprintw(win, 0, 39-7, "%6.2f%%", jack_cpu_load(meterec->client));
	
	wnoutrefresh(win);
}

void display_loop(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wloo;
	struct time_s low, high, now;
	wclear(win);
	
	if (meterec->loop.low == MAX_UINT) 
		wprintw(win, "[-:--:--.---]");
	else {
		low.frm = meterec->loop.low;
		low.rate = meterec->jack.sample_rate ;
		time_hms(&low);
		wprintw(win, "[%d:%02d:%02d.%03d]", low.h, low.m, low.s, low.ms);
	}
	
	now.frm = meterec->jack.playhead;
	now.rate = meterec->jack.sample_rate ;
	time_hms(&now);
	wprintw(win, " %d:%02d:%02d.%03d ", now.h, now.m, now.s, now.ms);
		
	if (meterec->loop.high == MAX_UINT) 
		wprintw(win, "[-:--:--.---]");
	else {
		high.frm = meterec->loop.high;
		high.rate = meterec->jack.sample_rate ;
		time_hms(&high);
		wprintw(win, "[%d:%02d:%02d.%03d]", high.h, high.m, high.s, high.ms);
	}
	
	wnoutrefresh(win);
}

void display_rd_status(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wrds;
	wclear(win);
	
	wprintw(win, "[> ");
		
	if (meterec->playback_sts==OFF) 
		wprintw(win, "%-8s","OFF");
	else if (meterec->playback_sts==STARTING) 
		wprintw(win, "%-8s","STARTING");
	else if (meterec->playback_sts==ONGOING) 
		wprintw(win, "%-8s","ONGOING");
	else if (meterec->playback_sts==PAUSED) 
		wprintw(win, "%-8s","PAUSED");
	else if (meterec->playback_sts==STOPING) 
		wprintw(win, "%-8s","STOPING");
	
	wprintw(win, "]");
	
	display_rd_buffer(meterec, win);
	
	wnoutrefresh(win);
}

void display_wr_status(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wwrs;
	wclear(win);
	
	if (meterec->record_sts) 
	color_set(RED, NULL);
	
	wprintw(win, "[O ");
	
	if (meterec->record_sts==OFF) 
		wprintw(win, "%-8s","OFF");
	else if (meterec->record_sts==STARTING) 
		wprintw(win, "%-8s","STARTING");
	else if (meterec->record_sts==ONGOING) 
		wprintw(win, "%-8s","ONGOING");
	else if (meterec->record_sts==STOPING) 
		wprintw(win, "%-8s","STOPING");
	
	wprintw(win, "]");
	
	display_wr_buffer(meterec, win);
	
	if (meterec->record_sts) {
		attron(A_BOLD);
		wprintw(win, " Take %d",meterec->n_takes+1);
		attroff(A_BOLD); 
	}
	
	
	if (meterec->write_disk_buffer_overflow)
		wprintw(win, " OVERFLOWS(%d)",meterec->write_disk_buffer_overflow);
	
	color_set(DEFAULT, NULL);
	
	wnoutrefresh(win);
}

void display_take_info(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wtak;
	char *take_name ="";
	unsigned int port, take, len, w;
	
	wclear(win);
	
	port = meterec->pos.port;
	take = meterec->pos.take;
	
	take_name = meterec->takes[take].name;
	
	if (take_name == NULL)
		take_name = "";
	
	wprintw(win, "Take %2d ",take);
	wprintw(win, "%s",  meterec->takes[take].port_has_track[port]?"|CONTENT":"|       " );
	wprintw(win, "%s",  meterec->takes[take].port_has_lock[port]?"|LOCKED":"|      " );
	wprintw(win, "%s", (meterec->ports[port].playback_take == take)?"|PLAYING|":"|       |" );
	wprintw(win, " [%s] ", meterec->takes[take].lenght);
	
	len = strlen(take_name);
	w = getmaxx(win);
	mvwprintw(win, 0, w-len-2, "{%s}\n", take_name);
	
	wnoutrefresh(win);

}

void display_session(struct meterec_s *meterec) 
{
	
	WINDOW *win = meterec->display.wses;
	unsigned int take, port;
	unsigned int y_pos, x_pos;
	
	wclear(win);
		
	y_pos = meterec->pos.port;
	x_pos = meterec->pos.take;
	
	for (port=0; port<meterec->n_ports; port++) {
		
		color_port(meterec, port, win);
		
		if (y_pos == port) 
			wattron(win, A_REVERSE);
		else 
			wattroff(win, A_REVERSE);
		
		for (take=1; take<meterec->n_takes+1; take++) {
			
			if ((y_pos == port) || (x_pos == take))
				wattron(win, A_REVERSE);
			else 
				wattroff(win, A_REVERSE);
			
			if ((y_pos == port) && (x_pos == take))
				wattroff(win, A_REVERSE);
			
			if ( meterec->ports[port].playback_take == take )
				wattron(win, A_BOLD);
			
			if ( meterec->takes[take].port_has_lock[port] )
				wprintw(win, meterec->takes[take].port_has_track[port]?"L":"l");
			else if ( meterec->ports[port].playback_take == take ) 
				wprintw(win, "P");
			else if ( meterec->takes[take].port_has_track[port] ) 
				wprintw(win, "X");
			else 
				wprintw(win, "-");
			
			wattroff(win, A_BOLD);
			
		}
		
		wprintw(win, "\n");
	}
	
	wattroff(win, A_REVERSE);
	wcolor_set(win, DEFAULT, NULL);
	
	wnoutrefresh(win);
}

void display_connections(struct meterec_s *meterec) {
	
}

void display_connections_fill_ports(struct meterec_s *meterec) {
	
	unsigned int port, i, len, w;
	const char **in, **out;
	
	wclear(meterec->display.wpi);
	wclear(meterec->display.wpii);
	wclear(meterec->display.wpo);
	wclear(meterec->display.wpoo);
	
	for (port=0; port<meterec->n_ports; port++) {
		
		if (port == meterec->pos.port) {
			if (!meterec->pos.inout) {
				wattron(meterec->display.wpi, A_REVERSE);
				wattron(meterec->display.wpo, A_REVERSE);
			} else {
				wattron(meterec->display.wpi, A_BOLD);
				wattron(meterec->display.wpo, A_BOLD);
			}
		}
		
		mvwprintw(meterec->display.wpi, port, 0, "%s:in_%-2d",  meterec->jack_name, port+1);
		mvwprintw(meterec->display.wpo, port, 0, "%s:out_%-2d", meterec->jack_name, port+1);
		
		wattroff(meterec->display.wpi, A_BOLD);
		wattroff(meterec->display.wpo, A_BOLD);
		wattroff(meterec->display.wpi, A_REVERSE);
		wattroff(meterec->display.wpo, A_REVERSE);
		
	}
	
	out=meterec->all_output_ports;
	w = getmaxx(meterec->display.wpoo);
	i = 0;
	while (out && *out) {
		if (meterec->pos.inout == CON_OUT && meterec->pos.con_out == i) {
			wattron(meterec->display.wpoo, A_REVERSE);
			mvwhline(meterec->display.wpoo, i, 0, 32, w);
		}
		if (jack_port_connected_to(meterec->ports[meterec->pos.port].input, *out))
			wattron(meterec->display.wpoo, A_BOLD);
		len = strlen(*out);
		mvwprintw(meterec->display.wpoo, i, w-len, "%s",*out);
		wattroff(meterec->display.wpoo, A_BOLD);
		wattroff(meterec->display.wpoo, A_REVERSE);
		i++;
		out++;
	}
	
	in=meterec->all_input_ports;
	w = getmaxx(meterec->display.wpii);
	i = 0;
	while (in && *in) {
		if (meterec->pos.inout == CON_IN && meterec->pos.con_in == i) {
			wattron(meterec->display.wpii, A_REVERSE);
			mvwhline(meterec->display.wpii, i, 0, 32, w);
		}
		if (jack_port_connected_to(meterec->ports[meterec->pos.port].output, *in))
			wattron(meterec->display.wpii, A_BOLD);
		mvwprintw(meterec->display.wpii, i, 0, "%s",*in);
		wattroff(meterec->display.wpii, A_BOLD);
		wattroff(meterec->display.wpii, A_REVERSE);
		i++;
		in++;
	}
	
	wnoutrefresh(meterec->display.wpi);
	wnoutrefresh(meterec->display.wpii);
	wnoutrefresh(meterec->display.wpo);
	wnoutrefresh(meterec->display.wpoo);
}

void display_connections_fill_conns(struct meterec_s *meterec) {
	
	unsigned int port, i, h;
	const char **in, **out;
	
	wclear(meterec->display.wci);
	wclear(meterec->display.wt);
	wclear(meterec->display.wco);
	
	for (port=0; port<meterec->n_ports; port++) {
		
		if (port == meterec->pos.port && !meterec->pos.inout) {
			wattron(meterec->display.wt, A_REVERSE);
			mvwhline(meterec->display.wt, port, 0, 32, 5);
		}
		
		if (meterec->ports[port].thru) 
			mvwhline(meterec->display.wt, port, 0, 0, 4);
		
		wattroff(meterec->display.wt, A_REVERSE);
	}
	
	h = getmaxy(meterec->display.wci);
	mvwvline(meterec->display.wci, 0, 1, 0, h);
	
	h = getmaxy(meterec->display.wco);
	mvwvline(meterec->display.wco, 0, 1, 0, h);
	
	mvwaddch(meterec->display.wci, meterec->pos.port, 1, ACS_LTEE);
	mvwaddch(meterec->display.wci, meterec->pos.port, 2, ACS_HLINE);
	mvwaddch(meterec->display.wco, meterec->pos.port, 1, ACS_RTEE);
	mvwaddch(meterec->display.wco, meterec->pos.port, 0, ACS_HLINE);
	
	in=meterec->all_output_ports;
	i = 0;
	while (in && *in) {
		if (jack_port_connected_to(meterec->ports[meterec->pos.port].input, *in)) {
			if (i==meterec->pos.port)
				mvwaddch(meterec->display.wci, i, 1, ACS_PLUS);
			else 
				mvwaddch(meterec->display.wci, i, 1, ACS_RTEE);
			
			mvwaddch(meterec->display.wci, i, 0, ACS_HLINE);
		}
		i++;
		in++;
	}
	
	out=meterec->all_input_ports;
	i = 0;
	while (out && *out) {
		if (jack_port_connected_to(meterec->ports[meterec->pos.port].output, *out)) {
			if (i==meterec->pos.port)
				mvwaddch(meterec->display.wco, i, 1, ACS_PLUS);
			else 
				mvwaddch(meterec->display.wco, i, 1, ACS_LTEE);
			
			mvwaddch(meterec->display.wco, i, 2, ACS_HLINE);
		}
		i++;
		out++;
	}
	
	
	wnoutrefresh(meterec->display.wci);
	wnoutrefresh(meterec->display.wt);
	wnoutrefresh(meterec->display.wco);
	
}

void display_connections_init(struct meterec_s *meterec) {
	
	WINDOW *win = meterec->display.wcon;
	
	/*
	** Windows structures
	**
	** | outputs | icon | meterec_ins | thru | meterec_outs | ocon | inputs |
	** 
	*/
	
	unsigned int ilen = strlen(meterec->jack_name) + 6 ;
	unsigned int olen = strlen(meterec->jack_name) + 7 ;
	const unsigned int tlen = 5, clen = 3, mlen = 1;
	unsigned int oolen, iilen;
	unsigned int h, w, x, y;
	
	getbegyx(win,y,x);
	getmaxyx(win,h,w);
	oolen = (w - ilen - olen - tlen - clen - mlen ) / 2;
	iilen = (w - ilen - olen - tlen - clen - mlen - oolen);
	
	meterec->display.wpoo = newwin(h, oolen, y, x);
	meterec->display.wci  = newwin(h, clen,  y, x+oolen);
	meterec->display.wtmi = newwin(h, mlen,  y, x+oolen+clen);
	meterec->display.wpi  = newwin(h, ilen,  y, x+oolen+clen+mlen);
	meterec->display.wt   = newwin(h, tlen,  y, x+oolen+clen+mlen+ilen);
	meterec->display.wpo  = newwin(h, olen,  y, x+oolen+clen+mlen+ilen+tlen);
	meterec->display.wtmo = newwin(h, mlen,  y, x+oolen+clen+mlen+ilen+tlen+olen);
	meterec->display.wco  = newwin(h, clen,  y, x+oolen+clen+mlen+ilen+tlen+olen+mlen);
	meterec->display.wpii = newwin(h, iilen, y, x+oolen+clen+mlen+ilen+tlen+olen+mlen+clen);
	
}

void display_ports(struct meterec_s *meterec) 
{
	unsigned int port=0, i;
	int line=0;
	const char **in, **out;
	WINDOW *win = meterec->display.wcon;
	
	out=meterec->all_input_ports;
	in=meterec->all_output_ports;
	
	wclear(win);
	
	while ((in && *in) || (out && *out) || port < meterec->n_ports) {
		
		wprintw(win, "  ");
		
		if (in && *in) {
		
			if (meterec->pos.inout == CON_IN)
				if (meterec->pos.con_in == line)
					wattron(win, A_REVERSE);
					
			if (jack_port_connected_to(meterec->ports[meterec->pos.port].input, *in)) {
				wattron(win, A_BOLD);
				wprintw(win, "%20s",*in);
				wattroff(win, A_BOLD);
				wattroff(win, A_REVERSE);
				wprintw(win, "-+");
			}
			else if (port==meterec->pos.port) {
				wprintw(win, "%20s",*in);
				wattroff(win, A_REVERSE);
				wprintw(win, " +");
				color_port(meterec, meterec->pos.port, win);
			}
			else {
				wprintw(win, "%20s",*in);
				wattroff(win, A_REVERSE);
				wprintw(win, " |");
			}
			in++;
		} else {
			if (port<meterec->pos.port)
				wprintw(win, "%20s |","");
			else if (port==meterec->pos.port)
				wprintw(win, "%20s +","");
			else 
				wprintw(win, "%20s  ","");
		}
		
		
		if (port<meterec->n_ports) {
		
			if (meterec->pos.port == port) {
				wprintw(win, "-");
				if (!meterec->pos.inout)
					wattron(win, A_REVERSE);
				else
					wattron(win, A_BOLD);
			}
			else 
				wprintw(win, " ");
			
			color_port(meterec, port, win);
			
			wprintw(win, "%s:in_%-2d",  meterec->jack_name, port+1);
			
			wcolor_set(win, DEFAULT, NULL);
			
			if (meterec->ports[port].thru)
				wprintw(win, "-> ");
			else 
				wprintw(win, "   ");
			
			wprintw(win, "%s:out_%-2d",  meterec->jack_name, port+1);
			
			wattroff(win, A_BOLD);
			wattroff(win, A_REVERSE);
		
		}
		else 
			for (i=0; i<(2*strlen(meterec->jack_name)+17); i++)
				wprintw(win, " ");
		
		if (meterec->pos.port==port)
			wprintw(win, "-");
		else 
			wprintw(win, " ");
		
		if (out && *out) {
			if (jack_port_connected_to(meterec->ports[meterec->pos.port].output, *out)) {
				wprintw(win, "+-");
				wattron(win, A_BOLD);
			}
			else if (port==meterec->pos.port) {
				wprintw(win, "+ ");
			}
			else
				wprintw(win, "| ");
				
			if (meterec->pos.inout == CON_OUT)
				if (meterec->pos.con_out == line)
					wattron(win, A_REVERSE);
				
			wprintw(win, "%s",*out);
			wattroff(win, A_BOLD);
			wattroff(win, A_REVERSE);
			
			out++;
		} else {
			if (port<meterec->pos.port)
				wprintw(win, "|");
			else if (port==meterec->pos.port)
				wprintw(win, "+");
			else 
				wprintw(win, "");
		}
		wcolor_set(win, DEFAULT, NULL);
		wprintw(win, "\n");
		
		if (port < meterec->n_ports)
			port ++;
			
		line ++;
	}
	
	wcolor_set(win, DEFAULT, NULL);
	
	wnoutrefresh(win);
	
}
