bin_PROGRAMS = tinytd

tinytd_SOURCES = \
	src/main.c \
	src/runtime.c src/runtime.h \
	src/mem.c src/mem.h \
	src/cpu.c src/cpu.h \
	src/fetch.c src/fetch.h \
	src/watch.c src/watch.h \
	src/trends.c src/trends.h \
	src/config.c src/config.h \
	src/writer.c src/writer.h \
	src/debug.c src/debug.h \
	src/reduce_config.c src/reduce_config.h \
	src/pipeline.c src/pipeline.h \
	src/scheduler.c src/scheduler.h

tinytd_CFLAGS = -I$(top_srcdir) $(AM_CFLAGS)
tinytd_LDADD = $(top_builddir)/common/libcommon.a -lrt
