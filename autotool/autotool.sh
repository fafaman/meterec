#!/bin/bash

cp autotool/configure.in . &&\
cp autotool/Makefile.am . &&\
aclocal &&\
autoconf &&\
autoheader &&\
automake --add-missing
