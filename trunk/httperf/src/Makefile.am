SUBDIRS = gen lib stat

# what flags you want to pass to the C compiler & linker
AM_CFLAGS = -I$(srcdir) -I$(srcdir)/gen -I$(srcdir)/lib -I$(srcdir)/stat

bin_PROGRAMS = httperf

if IDLECONN
bin_PROGRAMS += idleconn
idleconn_SOURCES = idleconn.c
idleconn_LDADD = lib/libutil.a
endif

httperf_SOURCES = httperf.c httperf.h object.c object.h call.c call.h conn.c \
  conn.h sess.c sess.h core.c core.h localevent.c localevent.h http.c http.h \
  timer.c timer.h

httperf_LDADD = gen/libgen.a lib/libutil.a stat/libstat.a
