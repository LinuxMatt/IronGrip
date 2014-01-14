#!/bin/bash

PN=irongrip

src_prepare() {
	xxd -i pixmaps/${PN}.png > src/${PN}.icon.h
	xxd -i COPYING > src/license.gpl.h
	intltoolize --force --copy --automake || exit 2
	autoreconf --verbose --force --install -Wno-portability
}

src_prepare

