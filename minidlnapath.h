/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2008 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#ifndef __MINIDLNAPATH_H__
#define __MINIDLNAPATH_H__

#include "config.h"

/* Paths and other URLs in the minidlna http server */

#define ROOTDESC_PATH 				"/rootDesc.xml"

#define CONTENTDIRECTORY_PATH			"/ContentDir.xml"
#define CONTENTDIRECTORY_CONTROLURL		"/ctl/ContentDir"
#define CONTENTDIRECTORY_EVENTURL		"/evt/ContentDir"

#define CONNECTIONMGR_PATH			"/ConnectionMgr.xml"
#define CONNECTIONMGR_CONTROLURL		"/ctl/ConnectionMgr"
#define CONNECTIONMGR_EVENTURL			"/evt/ConnectionMgr"

#define X_MS_MEDIARECEIVERREGISTRAR_PATH	"/X_MS_MediaReceiverRegistrar.xml"
#define X_MS_MEDIARECEIVERREGISTRAR_CONTROLURL	"/ctl/X_MS_MediaReceiverRegistrar"
#define X_MS_MEDIARECEIVERREGISTRAR_EVENTURL	"/evt/X_MS_MediaReceiverRegistrar"

#endif

