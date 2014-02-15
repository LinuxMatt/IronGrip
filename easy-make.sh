#!/bin/bash

usage() {
	echo -e "\nUSAGE: $0 <clean|build|package|setup>\n"
	echo -e "\tsetup  : install required deb packages for building"
	echo -e "\tbuild  : configure and compile this project"
	echo -e "\tpackage: create a Debian package for this project"
	echo -e "\tclean  : remove the files created by the 'build' action"
	echo -e ""
	exit 2
}

if [[ -z ${1} ]] ; then
	usage
fi

ACTION=$1
cd irongrip/

case "$ACTION" in
	"build" | "package" | "clean" | "setup")
		;;
	*)
		usage
		;;
esac

./maintainer-build.sh ${ACTION}
cd ..

