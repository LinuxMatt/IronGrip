## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@ -I../libcddb

bin_PROGRAMS = irongrip
irongrip_SOURCES = irongrip.c
irongrip_LDADD = @PACKAGE_LIBS@ $(INTLLIBS) ../libcddb/libcddb.a

