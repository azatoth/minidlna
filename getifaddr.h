/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#ifndef __GETIFADDR_H__
#define __GETIFADDR_H__
#include <arpa/inet.h>

/* getifaddr()
 * take a network interface name and write the
 * ip v4 address as text in the buffer
 * returns: 0 success, -1 failure */
int
getifaddr(const char * ifname, char * buf, int len);

int
getsysaddr(char * buf, int len);

int
getsyshwaddr(char * buf, int len);

int
get_remote_mac(struct in_addr ip_addr, unsigned char * mac);

#endif

