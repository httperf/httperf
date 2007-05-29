SUBDIRS = gen lib stat

# what flags you want to pass to the C compiler & linker
AM_CFLAGS = -I$(srcdir) -I$(srcdir)/gen -I$(srcdir)/lib -I$(srcdir)/stat

bin_PROGRAMS = httperf idleconn
httperf_SOURCES = httperf.c httperf.h object.c object.h call.c call.h conn.c \
  conn.h sess.c sess.h core.c core.h event.c event.h http.c http.h timer.c \
  timer.h
idleconn_SOURCES = idleconn.c

httperf_LDADD = gen/libgen.a lib/libutil.a stat/libstat.a
