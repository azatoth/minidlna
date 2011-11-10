AM_CFLAGS = -Wall -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 @STATIC_CFLAGS@

SUBDIRS=po

bin_PROGRAMS = minidlna testupnpdescgen
minidlna_SOURCES = minidlna.c upnphttp.c upnpdescgen.c upnpsoap.c \
			upnpreplyparse.c minixml.c \
			getifaddr.c daemonize.c upnpglobalvars.c \
			options.c minissdp.c uuid.c upnpevents.c \
			sql.c utils.c metadata.c scanner.c inotify.c \
			tivo_utils.c tivo_beacon.c tivo_commands.c \
			tagutils/textutils.c tagutils/misc.c tagutils/tagutils.c \
			playlist.c image_utils.c albumart.c log.c


#if NEED_VORBIS
vorbisflag = -lvorbis
#endif

#if NEED_OGG
flacoggflag = -logg
#endif

minidlna_LDADD = \
	@LIBJPEG_LIBS@ \
	@LIBID3TAG_LIBS@ \
	@LIBSQLITE3_LIBS@ \
	@LIBAVFORMAT_LIBS@ \
	@LIBAVUTIL_LIBS@ \
	@LIBAVCODEC_LIBS@ \
	@LIBEXIF_LIBS@ \
	@LIBDL_LIBS@ \
	@LIBRT_LIBS@ \
	@LIBINTL@ \
	@LIBICONV@ \
	-lpthread  -lFLAC  $(flacoggflag) $(vorbisflag)

minidlna_LDFLAGS = @STATIC_LDFLAGS@

testupnpdescgen_SOURCES = testupnpdescgen.c upnpdescgen.c
testupnpdescgen_LDADD = \
	@LIBJPEG_LIBS@ \
	@LIBID3TAG_LIBS@ \
	@LIBSQLITE3_LIBS@ \
	@LIBAVFORMAT_LIBS@ \
	@LIBAVUTIL_LIBS@ \
	@LIBAVCODEC_LIBS@ \
	@LIBEXIF_LIBS@ \
	@LIBDL_LIBS@ \
	-lpthread  -lFLAC  $(flacoggflag) $(vorbisflag)


ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = m4/ChangeLog
