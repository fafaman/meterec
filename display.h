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

void display_header(struct meterec_s *meterec);
void display_loop(struct meterec_s *meterec);
void display_ports(struct meterec_s *meterec);
void display_session(struct meterec_s *meterec);
void display_port_info(struct port_s *port_p);
void display_port_recmode(struct port_s *port_p);
void display_meter(struct meterec_s *meterec, int display_names, int width, int decay_len);
void init_display_scale(unsigned int width);
void free_scale(void);

