/*

  meterec
  Console based multi track digital peak meter and recorder for JACK
  Copyright (C) 2009-2020 Fabrice Lebas

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

struct time_s
{
	unsigned int h,m,s,ms,frm,rate;
};


void time_null_sprint(char * string);
void time_zero_sprint(char * string);
void time_sprint(struct time_s * time, char * string);
void time_hms(struct time_s * time);
void time_frm(struct time_s * time);

void time_init_frm(struct time_s *time, unsigned int rate, unsigned int frames);
void time_init_hms(struct time_s *time, unsigned int rate, unsigned int h, unsigned int m, unsigned int s, unsigned int ms);
