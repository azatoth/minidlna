/* miniupnp project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard
 * This software is subject to the coditions detailed in
 * the LICENCE file provided within the distribution */
#ifndef __UPNPDESCSTRINGS_H__
#define __UPNPDESCSTRINGS_H__

#include "config.h"

/* strings used in the root device xml description */
#ifdef READYNAS
#define ROOTDEV_MANUFACTURER		"NETGEAR"
#else
#define ROOTDEV_MANUFACTURER		"Justin Maggard"
#endif
#define ROOTDEV_MANUFACTURERURL		OS_URL
#define ROOTDEV_MODELNAME		"Windows Media Connect compatible (minidlna)"
#define ROOTDEV_MODELDESCRIPTION	OS_NAME " *ReadyNAS dev DLNA"
#define ROOTDEV_MODELURL		OS_URL

#endif
