bin_PROGRAMS = meterec
bin_SCRIPTS = meterec-init-conf meterec-pass-thru

AM_CFLAGS = -g -Wall -Wextra @JACK_CFLAGS@ @SNDFILE_CFLAGS@ @LIBCONFIG_CFLAGS@
AM_LDFLAGS = -lm @JACK_LIBS@ @SNDFILE_LIBS@ @LIBCONFIG_LIBS@

meterec_SOURCES = display.c disk.c conf.c ports.c meterec.c
meterec_LDADD = @JACK_LIBS@ @SNDFILE_LIBS@ @LIBCONFIG_LIBS@
