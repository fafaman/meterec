#!/bin/bash

RELEASE=0.7

echo Relasing meterec-$RELEASE
echo =====================

pushd ../

mv meterec-dev meterec-$RELEASE

tar -zcvf meterec-$RELEASE.tgz \
meterec-$RELEASE/*.c \
meterec-$RELEASE/*.h \
meterec-$RELEASE/meterec-init-conf \
meterec-$RELEASE/meterec-pass-thru \
meterec-$RELEASE/README \
meterec-$RELEASE/NEWS \
meterec-$RELEASE/TODO \
meterec-$RELEASE/INSTALL \
meterec-$RELEASE/AUTHORS \
meterec-$RELEASE/COPYING \
meterec-$RELEASE/Makefile.in \
meterec-$RELEASE/Makefile.am \
meterec-$RELEASE/configure \
meterec-$RELEASE/configure.in \
meterec-$RELEASE/depcomp \
meterec-$RELEASE/missing \
meterec-$RELEASE/install-sh \
meterec-$RELEASE/config.h.in \
meterec-$RELEASE/aclocal.m4 \
meterec-$RELEASE/autom4te.cache/* 

mv meterec-$RELEASE meterec-dev

popd

echo =====================
echo Done in ../meterec-$RELEASE.tgz
