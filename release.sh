#!/bin/bash

RELEASE=0.5

./autotool/autotool.sh &&\
cd ../ &&\
gtar -zcvf meterec-rel-$RELEASE.tgz \
meterec-$RELEASE/meterec.c \
meterec-$RELEASE/meterec-init-conf \
meterec-$RELEASE/NEWS \
meterec-$RELEASE/TODO \
meterec-$RELEASE/INSTALL \
meterec-$RELEASE/AUTHORS \
meterec-$RELEASE/COPYING \
meterec-$RELEASE/Makefile.in \
meterec-$RELEASE/Makefile.am \
meterec-$RELEASE/configure \
meterec-$RELEASE/configure.in
meterec-$RELEASE/depcomp \
meterec-$RELEASE/missing \
meterec-$RELEASE/install-sh \
meterec-$RELEASE/config.h.in \
meterec-$RELEASE/aclocal.m4 \
meterec-$RELEASE/autom4te.cache/* \
cd meterec-$RELEASE

