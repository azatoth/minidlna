# $Id$
# MiniUPnP project
# http://miniupnp.free.fr/
# Author : Thomas Bernard
# for use with GNU Make
# To install use :
# $ PREFIX=/dummyinstalldir make -f Makefile.linux install
# or :
# $ INSTALLPREFIX=/usr/local make -f Makefile.linux install
# or :
# $ make -f Makefile.linux install
#
#CFLAGS = -Wall -O -D_GNU_SOURCE -g -DDEBUG
#CFLAGS = -Wall -g -Os -D_GNU_SOURCE
CFLAGS = -Wall -g -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
	 -I/usr/include/ffmpeg \
	 -I/usr/include/libavutil -I/usr/include/libavcodec -I/usr/include/libavformat \
	 -I/usr/include/ffmpeg/libavutil -I/usr/include/ffmpeg/libavcodec -I/usr/include/ffmpeg/libavformat
CC = gcc
RM = rm -f
INSTALL = install

INSTALLPREFIX ?= $(PREFIX)/usr
SBININSTALLDIR = $(INSTALLPREFIX)/sbin
ETCINSTALLDIR = $(PREFIX)/etc/miniupnpd

BASEOBJS = minidlna.o upnphttp.o upnpdescgen.o upnpsoap.o \
           upnpreplyparse.o minixml.o \
           getifaddr.o daemonize.o upnpglobalvars.o \
           options.o minissdp.o upnpevents.o \
           sql.o utils.o metadata.o albumart.o scanner.o inotify.o \
           tivo_utils.o tivo_beacon.o tivo_commands.o \
           log.o

ALLOBJS = $(BASEOBJS) $(LNXOBJS)

LIBS = -lexif -ljpeg -ltag_c -lid3tag -lsqlite3 -lavformat -luuid -lgd

TESTUPNPDESCGENOBJS = testupnpdescgen.o upnpdescgen.o

EXECUTABLES = minidlna testupnpdescgen

.PHONY:	all clean install depend genuuid

all:	$(EXECUTABLES)

clean:
	$(RM) $(ALLOBJS)
	$(RM) $(EXECUTABLES)
	$(RM) testupnpdescgen.o

install:	minidlna genuuid
	$(INSTALL) -d $(SBININSTALLDIR)
	$(INSTALL) minidlna $(SBININSTALLDIR)
	$(INSTALL) -d $(ETCINSTALLDIR)
	$(INSTALL) netfilter/iptables_init.sh $(ETCINSTALLDIR)
	$(INSTALL) netfilter/iptables_removeall.sh $(ETCINSTALLDIR)
	$(INSTALL) --mode=0644 minidlna.conf $(ETCINSTALLDIR)
	$(INSTALL) -d $(PREFIX)/etc/init.d
	$(INSTALL) linux/miniupnpd.init.d.script $(PREFIX)/etc/init.d/miniupnpd

# genuuid is using the uuidgen CLI tool which is part of libuuid
# from the e2fsprogs
genuuid:
	sed -i -e "s/^uuid=[-0-9a-f]*/uuid=`(genuuid||uuidgen) 2>/dev/null`/" minidlna.conf

minidlna:	$(BASEOBJS) $(LNXOBJS) $(LIBS)

testupnpdescgen:	$(TESTUPNPDESCGENOBJS)

config.h:	genconfig.sh
	./genconfig.sh

depend:	config.h
	makedepend -f$(MAKEFILE_LIST) -Y \
	$(ALLOBJS:.o=.c) $(TESTUPNPDESCGENOBJS:.o=.c) 2>/dev/null

# DO NOT DELETE

minidlna.o: config.h upnpglobalvars.h minidlnatypes.h
minidlna.o: upnphttp.h upnpdescgen.h minidlnapath.h getifaddr.h upnpsoap.h
minidlna.o: options.h minissdp.h daemonize.h upnpevents.h
minidlna.o: commonrdr.h log.h
upnphttp.o: config.h upnphttp.h upnpdescgen.h minidlnapath.h upnpsoap.h
upnphttp.o: upnpevents.h log.h
upnpdescgen.o: config.h upnpdescgen.h minidlnapath.h upnpglobalvars.h
upnpdescgen.o: minidlnatypes.h upnpdescstrings.h log.h
upnpsoap.o: config.h upnpglobalvars.h minidlnatypes.h log.h utils.h sql.h
upnpsoap.o: upnphttp.h upnpsoap.h upnpreplyparse.h getifaddr.h log.h
upnpreplyparse.o: upnpreplyparse.h minixml.h log.h
minixml.o: minixml.h
getifaddr.o: getifaddr.h log.h
daemonize.o: daemonize.h config.h log.h
upnpglobalvars.o: config.h upnpglobalvars.h
upnpglobalvars.o: minidlnatypes.h
options.o: options.h config.h upnpglobalvars.h
options.o: minidlnatypes.h
minissdp.o: config.h upnpdescstrings.h minidlnapath.h upnphttp.h
minissdp.o: upnpglobalvars.h minidlnatypes.h minissdp.h log.h
upnpevents.o: config.h upnpevents.h minidlnapath.h upnpglobalvars.h
upnpevents.o: minidlnatypes.h upnpdescgen.h log.h
testupnpdescgen.o: config.h upnpdescgen.h
upnpdescgen.o: config.h upnpdescgen.h minidlnapath.h upnpglobalvars.h
upnpdescgen.o: minidlnatypes.h upnpdescstrings.h
scanner.o: upnpglobalvars.h metadata.h utils.h sql.h scanner.h log.h
metadata.o: upnpglobalvars.h metadata.h albumart.h utils.h sql.h log.h
sql.o: sql.h
log.o: log.h
