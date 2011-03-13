#!/bin/bash

if [ -f Makefile ];
then
make clean
fi

rm -f Makefile aclocal.m4 Makefile.am Makefile.in config config.h configure config.h.in config.log config.status configure.in depcomp install-sh missing stamp-h1
rm -rf autom4te.cache

cp autotool/configure.in . &&\
cp autotool/Makefile.am . &&\
aclocal &&\
autoconf &&\
autoheader &&\
automake --add-missing --copy &&\
exit 0
