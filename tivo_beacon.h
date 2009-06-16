/* TiVo discovery
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2009 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#include "config.h"
#ifdef TIVO_SUPPORT
/*
 *  * A saved copy of a beacon from another tivo or another server
 *   */
struct aBeacon
{
   time_t               lastSeen;
   char*                machine;
   char*                identity;
   char*                platform;
   char*                swversion;
   char*                method;
   char*                services;
   struct aBeacon*      next;
};

uint32_t
getBcastAddress();

int
OpenAndConfTivoBeaconSocket();

void
sendBeaconMessage(int fd, struct sockaddr_in * client, int len, int broadcast);
#endif
