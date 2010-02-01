bin_PROGRAMS = meterec
bin_SCRIPTS = meterec-init-conf

AM_CFLAGS = -g -Wall @JACK_CFLAGS@ @SNDFILE_CFLAGS@
AM_LDFLAGS = -lm @JACK_LIBS@ @SNDFILE_LIBS@

meterec_SOURCES = meterec.c
meterec_LDADD = @JACK_LIBS@ @SNDFILE_LIBS@
