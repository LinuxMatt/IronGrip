## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@

noinst_LIBRARIES = libcddb.a
libcddb_a_SOURCES = cddb.c cddb_cmd.c cddb_conn.c cddb_disc.c cddb_error.c cddb_log.c cddb_net.c cddb_regex.c cddb_site.c cddb_track.c cddb_util.c ll.c

