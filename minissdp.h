/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2007 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */
#ifndef __MINISSDP_H__
#define __MINISSDP_H__

/*#include "minidlnatypes.h"*/

struct sockets
{
	int snotify[MAX_LAN_ADDR];
#ifdef TIVO_SUPPORT
	int sbeacon;
	struct sockaddr_in tivo_bcast;
#endif
	struct timeval *timeout;
};

int
OpenAndConfSSDPReceiveSocket();
/* OpenAndConfSSDPReceiveSocket(int n_lan_addr, struct lan_addr_s * lan_addr);*/

/*int
OpenAndConfSSDPNotifySocket(const char * addr);*/

int
OpenAndConfSSDPNotifySockets(int * sockets);
/*OpenAndConfSSDPNotifySockets(int * sockets,
                             struct lan_addr_s * lan_addr, int n_lan_addr);*/

/*void
SendSSDPNotifies(int s, const char * host, unsigned short port,
                 unsigned int lifetime);*/
void
SendSSDPNotifies2(int * sockets,
                  unsigned short port,
                  unsigned int lifetime);
/*SendSSDPNotifies2(int * sockets, struct lan_addr_s * lan_addr, int n_lan_addr,
                  unsigned short port,
                  unsigned int lifetime);*/

void
ProcessSSDPRequest(int s, unsigned short port);
/*ProcessSSDPRequest(int s, struct lan_addr_s * lan_addr, int n_lan_addr,
                   unsigned short port);*/

int
SendSSDPGoodbye(int * sockets, int n);

void *
start_notify(void * arg);

#endif

