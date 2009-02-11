/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#ifndef __UPNPGLOBALVARS_H__
#define __UPNPGLOBALVARS_H__

#include <time.h>
#include <linux/types.h> // Defines __u32

#include "minidlnatypes.h"
#include "config.h"

#include <sqlite3.h>

#define USE_FORK 1
#define DB_VERSION 1

#define RESOURCE_PROTOCOL_INFO_VALUES \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN," \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_PAL;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_NA_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AC3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVHIGH_PRO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01," \
	"http-get:*:audio/x-ms-wma:*," \
	"http-get:*:audio/wav:*," \
	"http-get:*:audio/mp4:*," \
	"http-get:*:audio/x-aiff:*," \
	"http-get:*:audio/x-flac:*," \
	"http-get:*:application/ogg:*," \
	"http-get:*:image/jpeg:*," \
	"http-get:*:image/gif:*," \
	"http-get:*:audio/x-mpegurl:*," \
	"http-get:*:video/mpeg:*," \
	"http-get:*:video/x-msvideo:*," \
	"http-get:*:video/avi:*," \
	"http-get:*:video/mpeg2:*," \
	"http-get:*:video/dvd:*," \
	"http-get:*:video/x-ms-wmv:*"

/* statup time */
extern time_t startup_time;

extern struct runtime_vars_s runtime_vars;
/* runtime boolean flags */
extern int runtime_flags;
#define INOTIFYMASK		0x0001
#define SYSUPTIMEMASK		0x0002

#define SETFLAG(mask)	runtime_flags |= mask
#define GETFLAG(mask)	runtime_flags & mask
#define CLEARFLAG(mask)	runtime_flags &= ~mask

extern const char * pidfilename;

extern char uuidvalue[];

#define SERIALNUMBER_MAX_LEN (10)
extern char serialnumber[];

#define MODELNUMBER_MAX_LEN (48)
extern char modelnumber[];

#define PRESENTATIONURL_MAX_LEN (64)
extern char presentationurl[];

/* lan addresses */
/* MAX_LAN_ADDR : maximum number of interfaces
 * to listen to SSDP traffic */
#define MAX_LAN_ADDR (4)
extern int n_lan_addr;
extern struct lan_addr_s lan_addr[];

/* UPnP-A/V [DLNA] */
extern sqlite3 *db;
#define FRIENDLYNAME_MAX_LEN (64)
extern char friendly_name[];
extern struct media_dir_s * media_dirs;
extern struct album_art_name_s * album_art_names;
extern __u32 updateID;

#endif
