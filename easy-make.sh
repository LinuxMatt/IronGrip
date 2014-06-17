#!/bin/bash

usage() {
	echo -e "\nUSAGE: $0 <clean|build|package|setup|rundeps>\n"
	echo -e "\tsetup  : install required deb packages for building"
	echo -e "\tbuild  : configure and compile this project"
	echo -e "\tpackage: create a Debian package for this project"
	echo -e "\tclean  : remove the files created by the 'build' action"
	echo -e "\trundeps: install recommended deb packages for running"
	echo -e ""
	exit 2
}

LSB=$(which lsb_release)
if [[ "x$LSB" != "x" ]] ; then
	${LSB} -sd
fi

if [[ -z ${1} ]] ; then
	usage
fi

ACTION=$1
cd irongrip/

case "$ACTION" in
	"build" | "package" | "clean" | "setup" | "rundeps")
		;;
	*)
		usage
		;;
esac

./maintainer-build.sh ${ACTION}
cd ..

