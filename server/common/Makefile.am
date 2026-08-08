noinst_LIBRARIES = libcommon.a

libcommon_a_SOURCES = \
	ringbuf/shm.c ringbuf/shm.h \
	ringbuf/writer.c ringbuf/writer.h \
	ringbuf/reader.c ringbuf/reader.h \
	ringbuf/layout.h cell.h \
	metrics.c metrics.h \
	metrics_ex.c metrics_ex.h \
	event.c \ event.h \
	sysfs.c sysfs.h \
	log/core.c \
	log/stderr.c \
	log/syslog.c \
	log/journal.c \
	log/log.h \
	log/log_internal.h \
	config/ini.c config/ini.h \
	config/paths.c config/paths.h \
	config/read.c config/read.h \
	timer.c timer.h \
	proto/v1.h \
	wire.h \
	reduce_ema.c \ reduce_latest.c \
	reduce.h

libcommon_a_CFLAGS = -I$(top_srcdir)
