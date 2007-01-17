SUBDIRS = gen lib stat

# what flags you want to pass to the C compiler & linker
AM_CFLAGS = -Wall -I$(srcdir)/{gen,lib,stat}/
AM_LDFLAGS = -lm

bin_PROGRAMS = httperf idleconn
httperf_SOURCES = httperf.c object.c call.c conn.c sess.c core.c event.c http.c timer.c
idleconn_SOURCES = idleconn.c

httperf_LDADD = gen/libgen.a lib/libutil.a stat/libstat.a
