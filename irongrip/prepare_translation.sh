#!/bin/bash
NAME="irongrip"
xgettext -d ${NAME} -o po/${NAME}.po --keyword=_ src/${NAME}.c

