# what flags you want to pass to the C compiler & linker
AM_CFLAGS = -Wall -O2 -I$(srcdir)/../ -I$(srcdir)/../../
AM_LDFLAGS =

noinst_LIBRARIES = libgen.a
libgen_a_SOURCES = call_seq.c conn_rate.c misc.c rate.c session.c \
	uri_fixed.c uri_wlog.c uri_wset.c \
	wsess.c wsesslog.c wsesspage.c \
	sess_cookie.c
