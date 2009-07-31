/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2007 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */
#ifndef __MINIDLNATYPES_H__
#define __MINIDLNATYPES_H__

#include "config.h"
#include <netinet/in.h>

/* structure for storing lan addresses
 * with ascii representation and mask */
struct lan_addr_s {
	char str[16];	/* example: 192.168.0.1 */
	struct in_addr addr, mask;	/* ip/mask */
};

struct runtime_vars_s {
	int port;	/* HTTP Port */
	int notify_interval;	/* seconds between SSDP announces */
};

enum media_types {
	ALL_MEDIA,
	AUDIO_ONLY,
	VIDEO_ONLY,
	IMAGES_ONLY,
	NO_MEDIA
};

enum file_types {
	TYPE_UNKNOWN,
	TYPE_DIR,
	TYPE_FILE
};

enum client_types {
	EXbox = 1,
	EPS3,
	ESamsungTV,
	EDenonReceiver,
	EFreeBox,
	EPopcornHour,
	EStandardDLNA150 = 100
};

struct media_dir_s {
	char * path;            /* Base path */
	enum media_types type;  /* type of files to scan */
	struct media_dir_s * next;
};

struct album_art_name_s {
	char * name;            /* Base path */
	struct album_art_name_s * next;
};

struct client_cache_s {
	struct in_addr addr;
	unsigned char mac[6];
	enum client_types type;
	u_int32_t flags;
	time_t age;
};

#endif
