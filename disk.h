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

void *writer_thread(void *d);
void *reader_thread(void *d);
float read_disk_buffer_level(struct meterec_s *meterec);
float write_disk_buffer_level(struct meterec_s *meterec);
unsigned int set_thread_delay(struct meterec_s *meterec);
void read_disk_seek(struct meterec_s *meterec, unsigned int seek);
