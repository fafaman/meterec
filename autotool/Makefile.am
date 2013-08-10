bin_PROGRAMS = meterec test
bin_SCRIPTS = meterec-init-conf

man_MANS = meterec.1 meterec-init-conf.1

#AM_CFLAGS = -Wextra
AM_CFLAGS = -Wall 
#AM_LDFLAGS = @JACK_LIBS@ @SNDFILE_LIBS@ @LIBCONFIG_LIBS@

meterec_SOURCES = conf.c ports.c position.c display.c queue.c keyboard.c session.c disk.c meterec.c

test_SOURCES = queue.c test.c
