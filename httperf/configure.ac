#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT(httperf, 0.9.0, httperf@linux.hpl.hp.com)
AC_CONFIG_SRCDIR([src/httperf.c])
AC_CONFIG_HEADER([config.h])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE(httperf, 0.9.0)

# Turn on Debug if necessary
AC_ARG_ENABLE(debug,
 [  --enable-debug          enable debug support])

if test "$enable_debug" = yes; then
      CFLAGS="${CFLAGS} -DDEBUG"
fi

# Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC([cc gcc])
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_GCC_TRADITIONAL

# Checks for libraries.
AC_CHECK_LIB(m, sqrt)
AC_CHECK_LIB(crypto, main)
AC_CHECK_LIB(ssl, SSL_version)
# The following checks are for solaris and its ilk
AC_CHECK_LIB(socket, getsockopt)
AC_CHECK_LIB(nsl, socket)
AC_CHECK_LIB(resolv, inet_aton)

# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h libintl.h netdb.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h openssl/ssl.h getopt.h])

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
AC_HEADER_TIME
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
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STRTOD
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gethostbyname gettimeofday memset munmap select socket strcasecmp strchr strdup strerror strncasecmp strrchr strstr strtol strtoul inet_aton getopt_long])

AC_OUTPUT(Makefile man/Makefile src/stat/Makefile src/lib/Makefile src/gen/Makefile src/Makefile)
