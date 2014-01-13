dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([irongrip], [0.7])
AC_CONFIG_SRCDIR([configure.ac])
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE
IT_PROG_INTLTOOL([0.34.90])

AC_ISC_POSIX
AC_PROG_RANLIB
AC_PROG_CC
AM_PROG_CC_STDC
AC_PROG_INSTALL
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_CHECK_HEADERS([arpa/inet.h fcntl.h libintl.h limits.h netdb.h netinet/in.h pthread.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h sys/timeb.h unistd.h])
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([alarm bzero dup2 ftime gethostbyname memset mkdir regcomp select socket strchr strdup strerror strrchr strtol])

pkg_modules="gtk+-2.0 >= 2.8.0, gthread-2.0 >= 2.4.0, glib-2.0 >= 2.10.0"
PKG_CHECK_MODULES(PACKAGE, [$pkg_modules])
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

GETTEXT_PACKAGE=irongrip
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

PIXMAPDIR=pixmaps
AC_SUBST(PIXMAPDIR)
AC_DEFINE_UNQUOTED(PIXMAPDIR,"$PIXMAPDIR", [Pictures directory.])

AM_GLIB_GNU_GETTEXT

CFLAGS="-DDEBUG -O0 -ggdb -Wall -Wno-deprecated-declarations -std=gnu99"

AC_DEFINE([PROGRAM_NAME], ["IronGrip"], ["Program name"])

AC_OUTPUT([
libcddb/Makefile
src/Makefile
po/Makefile.in
Makefile
])

