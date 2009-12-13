#!/bin/bash

cp autotool/configure.in . &&\
cp autotool/Makefile.am . &&\
aclocal &&\
autoconf &&\
autoheader &&\
automake --add-missing &&\
./configure &&\
make clean &&\
make &&\
date ; ls -la meterec

