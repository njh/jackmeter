dnl Require autoconf version >= 2.59
AC_PREREQ(2.59)


dnl ############# Initialization
AC_INIT([jackmeter], [0.4], [njh@aelius.com], jackmeter)
AC_CONFIG_SRCDIR([jack_meter.c])
AC_CONFIG_AUX_DIR([build-scripts])
AC_CONFIG_MACRO_DIR([build-scripts])
AM_INIT_AUTOMAKE


dnl ############# Compiler and tools Checks
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_C_CONST


dnl ############## Check for packages we depend upon
AC_CHECK_LIB([m], [sqrt], , [AC_MSG_ERROR(Can't find libm)])
AC_CHECK_LIB([mx], [powf])

# Check for JACK (need 0.100.0 for jack_client_open)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)
AC_SUBST(JACK_CFLAGS)
AC_SUBST(JACK_LIBS)


dnl ############## Header and function checks
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_CHECK_FUNCS( atexit usleep )


dnl ############## Output files
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
