#
# XWiimote
#
# Copyright (c) 2011, 2012 David Herrmann <dh.herrmann@googlemail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

AC_PREREQ(2.68)
AC_INIT([xf86-input-xwiimote],
        0.5,
        [https://github.com/dvdhrm/xorg-input-xwiimote],
        xf86-input-xwiimote)

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE([dist-xz -Wall -Werror 1.11 foreign subdir-objects no-dist-gzip tar-pax])
AM_SILENT_RULES([yes])

AM_PROG_AR
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CC
AC_CHECK_FUNCS([ffs])

m4_ifndef([XORG_MACROS_VERSION],
          [AC_FATAL([xorg-macros 1.8 or later required])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AC_ARG_WITH(xorg-module-dir,
            AC_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])
inputdir=${moduledir}/input
AC_SUBST(inputdir)

PKG_CHECK_MODULES(SYSDEP, libudev libxwiimote >= 2)
AC_SUBST(SYSDEP_CFLAGS)
AC_SUBST(SYSDEP_LIBS)

PKG_CHECK_MODULES(XORG, xorg-server xproto)
XORG_CFLAGS="$CWARNFLAGS $XORG_CFLAGS"

DRIVER_NAME=xwiimote
AC_SUBST([DRIVER_NAME])

AC_OUTPUT([Makefile])
