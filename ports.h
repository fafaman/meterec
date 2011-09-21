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


void process_port_register(jack_port_id_t port_id, int new, void *arg);
void create_input_port(struct meterec_s *meterec, unsigned int port);
void create_output_port(struct meterec_s *meterec, unsigned int port);
void create_monitor_port(struct meterec_s *meterec) ;
void register_port_old(struct meterec_s *meterec, char *port_name, unsigned int port);
void register_port(struct meterec_s *meterec, char *port_name, unsigned int port);
void connect_all_ports(struct meterec_s *meterec);
void connect_any_port(struct meterec_s *meterec, char *port_name, unsigned int port);
void disconnect_any_port(struct meterec_s *meterec, char *port_name, unsigned int port);
void retreive_connected_ports(struct meterec_s *meterec);
void retreive_existing_ports(struct meterec_s *meterec);
void register_connect_port(struct meterec_s *meterec, char *port_name, unsigned int port);
void deregister_disconnect_port(struct meterec_s *meterec, char *port_name, unsigned int port);
char* port_rename(struct meterec_s *meterec, unsigned int port);
void filter_existing_ports(const char **port_list, const char *port_name_pattern );
