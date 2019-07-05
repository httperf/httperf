#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT(httperf, 0.9.1, httperf@linux.hpl.hp.com)
AC_CONFIG_SRCDIR([src/httperf.c])
AC_CONFIG_HEADER([config.h])

AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE(httperf, 0.9.1)

# Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC([cc gcc])
AC_PROG_CC_C99

AC_PROG_INSTALL
AC_PROG_LIBTOOL

# Are we building the idleconn program?
AC_ARG_ENABLE([idleconn],
	AS_HELP_STRING([--enable-idleconn],
		[Build the idleconn program (default=no), requires libevent]),
	[enable_idleconn=$enableval],
	[enable_idleconn=no])

if test "$enable_idleconn" = "yes"; then
AC_CHECK_LIB([event],
	[event_init],
	,
	[AC_MSG_ERROR([libevent is required to build idleconn])])
fi


AM_CONDITIONAL(IDLECONN, 
	[test "x$enable_idleconn" = xyes])

# Checks for libraries.
AC_CHECK_LIB(m, sqrt)
AC_CHECK_LIB(crypto, main)
AC_CHECK_LIB(ssl, SSL_version, , AC_MSG_WARN([SSL Disabled]) )
# The following checks are for solaris and its ilk
AC_SEARCH_LIBS([getsockopt], [socket])
AC_SEARCH_LIBS([socket], [socket nsl])
AC_SEARCH_LIBS([gethostbyname], [socket nsl])
AC_SEARCH_LIBS([inet_aton], [resolv])

# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_TIME
AC_CHECK_HEADERS([openssl/ssl.h getopt.h])

if test "$ac_cv_header_openssl_ssl_h" = "yes" \
	-a "$ac_cv_lib_ssl_SSL_version" = "yes" \
	-a "$ac_cv_lib_crypto_main" = "yes"; then
  CFLAGS="${CFLAGS} -DHAVE_SSL"
  LDFLAGS="${LDFLAGS}"  
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_TYPE(u_char, unsigned char)
AC_CHECK_TYPE(u_short, unsigned short)
AC_CHECK_TYPE(u_int, unsigned int)
AC_CHECK_TYPE(u_long, unsigned long)

# Check for unsigned long long int (stat/basic.c)
AC_TYPE_UNSIGNED_LONG_LONG_INT
if test "$ac_cv_type_unsigned_long_long_int" != "yes" ; then
	AC_CHECK_TYPE(u_wide, unsigned long)
else
	AC_CHECK_TYPE(u_wide, unsigned long long)
fi

# Checks for library functions.
AC_FUNC_MMAP
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STRTOD
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([getopt_long])

# Turn on Debug if necessary
AC_ARG_ENABLE(debug,
 [  --enable-debug          enable debug support])

if test "$enable_debug" = yes; then
      CFLAGS="${CFLAGS} -DDEBUG"
fi

AC_ARG_WITH(epoll, AS_HELP_STRING([--with-epoll], [use epoll if available]))
AS_IF([test "$with_epoll" != "no"],
	AC_CHECK_FUNC(epoll_create,
		AC_DEFINE([HAVE_EPOLL], 1, [#undef HAVE_EPOLL])))

AC_OUTPUT(Makefile man/Makefile src/stat/Makefile src/lib/Makefile src/gen/Makefile src/Makefile)
