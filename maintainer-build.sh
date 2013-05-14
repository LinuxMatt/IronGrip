#!/bin/bash
# This file is intended for maintainers to do a clean build of the project from scratch

PROGRAM=irongrip

if [[ -z ${1} ]] ; then
	echo "USAGE: $0 <clean|build|package>"
	exit 2
fi
ACTION=$1

make maintainer-clean
make clean
# EXTRA CLEANUP
rm -r autom4te.cache 2> /dev/null
rm Makefile 2> /dev/null
rm Makefile.in 2> /dev/null
rm aclocal.m4 2> /dev/null
rm config.h 2> /dev/null
rm config.h.in 2> /dev/null
rm config.log 2> /dev/null
rm config.status 2> /dev/null
rm configure 2> /dev/null
rm depcomp 2> /dev/null
rm install-sh 2> /dev/null
rm intltool-extract 2> /dev/null
rm intltool-extract.in 2> /dev/null
rm intltool-merge 2> /dev/null
rm intltool-merge.in 2> /dev/null
rm intltool-update 2> /dev/null
rm intltool-update.in 2> /dev/null
rm libcddb/Makefile.in 2> /dev/null
rm missing 2> /dev/null
rm po/Makefile.in.in 2> /dev/null
rm src/Makefile.in 2> /dev/null
rm src/irongrip.icon.h 2> /dev/null
rm src/license.gpl.h 2> /dev/null
rm stamp-h1 2>/dev/null
rm config.guess 2>/dev/null
rm config.sub 2>/dev/null
rm ltmain.sh 2>/dev/null
#DEB CLEANUP
rm debian/files 2>/dev/null
rm debian/irongrip.debhelper.log 2>/dev/null
rm debian/irongrip.postinst.debhelper 2>/dev/null
rm debian/irongrip.postrm.debhelper 2>/dev/null
rm debian/irongrip.substvars 2>/dev/null
rm -r debian/irongrip/ 2>/dev/null

# ../irongrip_0.4-1_amd64.changes
# ../irongrip_0.4-1_amd64.deb
# ../irongrip_0.4-1.debian.tar.gz
# ../irongrip_0.4-1.dsc
# ../irongrip_0.4.orig.tar.gz

if [ "x${ACTION}" == "xclean" ];
then
	echo "CLEANED UP MAINTAINER FILES ONLY."
	exit 1
fi

echo "* Creating icon include file..."
xxd -i pixmaps/${PROGRAM}.png > src/${PROGRAM}.icon.h
echo "* Creating COPYING include file..."
xxd -i COPYING > src/license.gpl.h
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
echo "* Running configure..."
./configure || exit 2
echo "* Running make ..."
make || exit 2

if [ "x${ACTION}" != "xpackage" ];
then
	exit 0
fi

VERSION=$(head -1 debian/changelog | cut -f2 -d" " | tr -d \(\) | cut -f1 -d"-")
ORIGTGZ=${PROGRAM}_${VERSION}.orig.tar.gz
cd ..
rm ${ORIGTGZ} 2>/dev/null
tar -czf ${ORIGTGZ} ${PROGRAM}/ || exit 2
cd -
dpkg-buildpackage


