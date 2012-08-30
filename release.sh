#!/bin/bash

RELEASE=`grep AC_INIT autotool/configure.in | awk -F"," '{print $2}' | sed 's/.*\[\([0-9.]*\)\]/\1/'`

echo Relasing meterec-$RELEASE
echo =====================

pushd ../

mv meterec-dev meterec-$RELEASE

tar -zcvf meterec-$RELEASE.tgz \
meterec-$RELEASE/*.c \
meterec-$RELEASE/*.h \
meterec-$RELEASE/meterec-init-conf \
#meterec-$RELEASE/meterec-pass-thru \
meterec-$RELEASE/README \
meterec-$RELEASE/NEWS \
meterec-$RELEASE/TODO \
meterec-$RELEASE/INSTALL \
meterec-$RELEASE/AUTHORS \
meterec-$RELEASE/COPYING \
#meterec-$RELEASE/Makefile.in \
meterec-$RELEASE/Makefile.am \
meterec-$RELEASE/configure \
#meterec-$RELEASE/configure.in \
meterec-$RELEASE/depcomp \
meterec-$RELEASE/missing \
meterec-$RELEASE/install-sh \
#meterec-$RELEASE/config.h.in \
meterec-$RELEASE/aclocal.m4 \

mv meterec-$RELEASE meterec-dev

popd

echo =====================
echo Done in ../meterec-$RELEASE.tgz
echo =====================
echo "cd ../"
echo "tar -zxvf meterec-$RELEASE.tgz"
echo "cd meterec-$RELEASE"
echo "./configure && make"
echo "./meterec-init-conf meterec"
echo "./meterec"
echo =====================
echo "scp README fafaman,meterec@web.sourceforge.net:/home/project-web/meterec/htdocs/index.txt"
echo "cd ../"
echo "scp meterec-$RELEASE.tgz fafaman,meterec@frs.sourceforge.net:/home/frs/project/m/me/meterec"

