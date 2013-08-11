#!/bin/bash

RELEASE=`grep AC_INIT autotool/configure.in | awk -F"," '{print $2}' | sed 's/.*\[\([0-9.]*\)\]/\1/'`

DEVAREA=`pwd | xargs basename`

echo Relasing meterec-$RELEASE
echo =====================

pushd ../

mv $DEVAREA meterec-$RELEASE

tar -zcvf meterec-$RELEASE.tgz \
meterec-$RELEASE/*.c \
meterec-$RELEASE/*.h \
meterec-$RELEASE/meterec.1 \
meterec-$RELEASE/meterec-init-conf.1 \
meterec-$RELEASE/meterec-init-conf \
meterec-$RELEASE/README \
meterec-$RELEASE/NEWS \
meterec-$RELEASE/TODO \
meterec-$RELEASE/INSTALL \
meterec-$RELEASE/AUTHORS \
meterec-$RELEASE/COPYING \
meterec-$RELEASE/Makefile.in \
meterec-$RELEASE/Makefile.am \
meterec-$RELEASE/config.h.in \
meterec-$RELEASE/configure.in \
meterec-$RELEASE/configure \
meterec-$RELEASE/depcomp \
meterec-$RELEASE/missing \
meterec-$RELEASE/install-sh \
meterec-$RELEASE/aclocal.m4 \

mv meterec-$RELEASE $DEVAREA 

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
echo "cd ../"
echo "scp meterec-$RELEASE.tgz fafaman,meterec@frs.sourceforge.net:/home/frs/project/m/me/meterec"
echo "cd $DEVAREA"
echo "scp index.html fafaman,meterec@web.sourceforge.net:/home/project-web/meterec/htdocs/index.html"
echo "scp README fafaman,meterec@web.sourceforge.net:/home/project-web/meterec/htdocs/english.txt"
echo "scp NEWS fafaman,meterec@frs.sourceforge.net:/home/frs/project/m/me/meterec/README.txt"
