#!/bin/bash
# This file is intended for maintainers to do a clean build of the project from scratch

PROGRAM=irongrip
INSTALL_PREFIX="${HOME}/opt"

if [[ -z ${1} ]] ; then
	echo "USAGE: $0 <clean|build>"
	exit 2
fi
ACTION=$1

make maintainer-clean
rm Makefile.in aclocal.m4 config.h.in configure  depcomp  install-sh  missing src/Makefile.in 2> /dev/null
rm -r autom4te.cache 2> /dev/null
rm po/Makefile.in.in 2> /dev/null
rm src/irongrip.icon.h 2> /dev/null

if [ "x${ACTION}" != "xbuild" ];
then
	echo "CLEANED UP MAINTAINER FILES ONLY."
	exit 1
fi

echo "* Creating icon include file..."
xxd -i pixmaps/${PROGRAM}.png > src/${PROGRAM}.icon.h
echo "* Running aclocal ..."
aclocal || exit 2
echo "* Running autoheader ..."
autoheader || exit 2
echo "* Running automake ..."
automake --add-missing -c || exit 2
echo "* Running autoconf ..."
autoconf || exit 2
echo "* Running intltoolize ..."
intltoolize  -c || exit 2
echo "* Running configure with prefix=$INSTALL_PREFIX ..."
./configure --prefix=${INSTALL_PREFIX} || exit 2
echo "* Running make ..."
make


