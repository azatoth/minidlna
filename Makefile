# $Id$
# MiniDLNA project
# http://sourceforge.net/projects/minidlna/
# (c) 2008-2009 Justin Maggard
# for use with GNU Make
# To install use :
# $ DESTDIR=/dummyinstalldir make install
# or :
# $ INSTALLPREFIX=/usr/local make install
# or :
# $ make install
#
#CFLAGS = -Wall -O -D_GNU_SOURCE -g -DDEBUG
#CFLAGS = -Wall -g -Os -D_GNU_SOURCE
CFLAGS = -Wall -g -O3 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
		 -I/usr/include/ffmpeg \
		 -I/usr/include/libavutil -I/usr/include/libavcodec -I/usr/include/libavformat \
		 -I/usr/include/ffmpeg/libavutil -I/usr/include/ffmpeg/libavcodec -I/usr/include/ffmpeg/libavformat
#STATIC_LINKING: CFLAGS += -DSTATIC
#STATIC_LINKING: LDFLAGS = -static
CC = gcc
RM = rm -f
INSTALL = install
DEPDIR = .deps

INSTALLPREFIX ?= $(DESTDIR)/usr
SBININSTALLDIR = $(INSTALLPREFIX)/sbin
ETCINSTALLDIR = $(DESTDIR)/etc

BASEOBJS = minidlna.o upnphttp.o upnpdescgen.o upnpsoap.o \
		   upnpreplyparse.o minixml.o \
		   getifaddr.o daemonize.o upnpglobalvars.o \
		   options.o minissdp.o uuid.o upnpevents.o \
		   sql.o utils.o metadata.o scanner.o inotify.o \
		   tivo_utils.o tivo_beacon.o tivo_commands.o \
		   tagutils/textutils.o tagutils/misc.o tagutils/tagutils.o \
		   playlist.o image_utils.o albumart.o log.o

ALLOBJS = $(BASEOBJS) $(LNXOBJS)

LIBS = -lpthread -lexif -ljpeg -lsqlite3 -lavformat -lavutil -lavcodec -lid3tag -lFLAC -logg -lvorbis
#STATIC_LINKING: LIBS = -lvorbis -logg -lm -lsqlite3 -lpthread -lexif -ljpeg -lFLAC -lm -lid3tag -lz -lavformat -lavutil -lavcodec -lm

TESTUPNPDESCGENOBJS = testupnpdescgen.o upnpdescgen.o

EXECUTABLES = minidlna testupnpdescgen

.PHONY:	all clean distclean install

all:	$(EXECUTABLES)

clean:
	$(RM) $(ALLOBJS)
	$(RM) $(EXECUTABLES)
	$(RM) testupnpdescgen.o
	$(RM) -r $(DEPDIR)

distclean: clean
	$(RM) config.h

install:	minidlna
	$(INSTALL) -d $(SBININSTALLDIR)
	$(INSTALL) minidlna $(SBININSTALLDIR)
	$(INSTALL) -d $(ETCINSTALLDIR)
	$(INSTALL) --mode=0644 minidlna.conf $(ETCINSTALLDIR)

minidlna: config.h $(BASEOBJS) $(LNXOBJS)
	$(if $(findstring 1,$(V)),,@echo Linking $@;) $(CC) $(LDFLAGS) -o $@ $(BASEOBJS) $(LNXOBJS) $(LIBS)


testupnpdescgen: config.h $(TESTUPNPDESCGENOBJS)
	$(if $(findstring 1,$(V)),,@echo Linking $@;) $(CC) $(LDFLAGS) -o $@ $(TESTUPNPDESCGENOBJS)

config.h:	genconfig.sh
	./genconfig.sh

%.o : %.c
	@@mkdir -p $(dir $(DEPDIR)/$*.Tpo)
	$(if $(findstring 1,$(V)),,@echo Compiling $@;) $(CC) -c $(CFLAGS) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<
	@@mv -f $(DEPDIR)/$*.Tpo $(DEPDIR)/$*.Po

-include $(SRCS:%.cpp=$(DEPDIR)/%.Po)
