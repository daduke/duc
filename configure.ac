#                                               -*- Autoconf -*-
# Process this file with autoconf, et al to produce a configure script.
#
#    autoreconf -v -f
# which is just:
#    aclocal && autoconf && automake -a -f && ./configure && make

AC_PREREQ([2.68])

AC_INIT([duc], [0.1], [duc@zevv.nl])
AM_INIT_AUTOMAKE([foreign no-dependencies])
AM_SILENT_RULES([yes])
AC_CONFIG_HEADER([config.h])
AC_GNU_SOURCE

# Turn off GNU standards like README, etc, for now.
# Do not do dependency tracking either.

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

# Checks for libraries.
AC_CHECK_LIB([m], [main])

# Check for pkg-config, needs fallback for when it doesn't exist...
PKG_PROG_PKG_CONFIG

# Check for tokyocabinet
PKG_CHECK_MODULES([TC],[tokyocabinet])

# Check for Cairo support
PKG_CHECK_MODULES([CAIRO],[cairo])

# Check for Pango support
PKG_CHECK_MODULES([PANGO],[pango])

# Check for Pangocairo support
PKG_CHECK_MODULES([PANGOCAIRO],[pangocairo])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([floor memset realpath strchr strdup strerror])

AC_CONFIG_FILES([Makefile libduc/Makefile duc/Makefile])
AC_OUTPUT