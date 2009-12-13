#!/bin/bash

make clean
 
rm Makefile aclocal.m4 Makefile.am Makefile.in config config.h configure config.h.in config.log config.status configure.in depcomp install-sh missing stamp-h1
rm config.h.in~
rm -rf autom4te.cache

cp autotool/configure.in . &&\
cp autotool/Makefile.am . &&\
aclocal &&\
autoconf &&\
autoheader &&\
automake --add-missing
