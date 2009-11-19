/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * author: Ryan Wagoner
 * (c) 2006 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#include "config.h"

/* enum of option available in the miniupnpd.conf */
enum upnpconfigoptions {
	UPNP_INVALID = 0,
	UPNPIFNAME = 1,			/* ext_ifname */
	UPNPLISTENING_IP,		/* listening_ip */
	UPNPPORT,			/* port */
	UPNPPRESENTATIONURL,		/* presentation_url */
	UPNPNOTIFY_INTERVAL,		/* notify_interval */
	UPNPSYSTEM_UPTIME,		/* system_uptime */
	UPNPUUID,			/* uuid */
	UPNPSERIAL,			/* serial */
	UPNPMODEL_NUMBER,		/* model_number */
	UPNPFRIENDLYNAME,		/* how the system should show up to DLNA clients */
	UPNPMEDIADIR,			/* directory to search for UPnP-A/V content */
	UPNPALBUMART_NAMES,		/* list of '/'-delimited file names to check for album art */
	UPNPINOTIFY,			/* enable inotify on the media directories */
	UPNPDBDIR,			/* base directory to store the database, log files, and album art cache */
	ENABLE_TIVO,			/* enable support for streaming images and music to TiVo */
	ENABLE_DLNA_STRICT		/* strictly adhere to DLNA specs */
};

/* readoptionsfile()
 * parse and store the option file values
 * returns: 0 success, -1 failure */
int
readoptionsfile(const char * fname);

/* freeoptions() 
 * frees memory allocated to option values */
void
freeoptions(void);

#define MAX_OPTION_VALUE_LEN (80)
struct option
{
	enum upnpconfigoptions id;
	char value[MAX_OPTION_VALUE_LEN];
};

extern struct option * ary_options;
extern int num_options;

#endif

