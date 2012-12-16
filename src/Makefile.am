## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@ -I../libcddb

bin_PROGRAMS = irongrip
irongrip_SOURCES = irongrip.c

## The 2 lines below won't work with old versions of automake (e.g. on Ubuntu 6.06 LTS)
## because the '/' in the libcddb path was not supported yet.
## If you know how to fix this file, please tell me ;)
irongrip_LDADD = @PACKAGE_LIBS@ $(INTLLIBS) ../libcddb/libcddb.a
SUBDIRS = ../libcddb

