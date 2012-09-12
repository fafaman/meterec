bin_PROGRAMS = meterec test
bin_SCRIPTS = meterec-init-conf

man_MANS = meterec.1 meterec-init-conf.1

AM_CFLAGS = -g -Wall -Wextra @JACK_CFLAGS@ @SNDFILE_CFLAGS@ @LIBCONFIG_CFLAGS@
AM_LDFLAGS = -lm @JACK_LIBS@ @SNDFILE_LIBS@ @LIBCONFIG_LIBS@

meterec_SOURCES = conf.c ports.c position.c display.c queue.c keyboard.c session.c disk.c meterec.c
meterec_LDADD = @JACK_LIBS@ @SNDFILE_LIBS@ @LIBCONFIG_LIBS@

test_SOURCES = queue.c test.c
test_LDADD = @JACK_LIBS@ @SNDFILE_LIBS@ @LIBCONFIG_LIBS@
