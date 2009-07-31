/* $Id$ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#if defined(sun)
#include <sys/sockio.h>
#endif

#include "getifaddr.h"
#include "log.h"

int
getifaddr(const char * ifname, char * buf, int len)
{
	/* SIOCGIFADDR struct ifreq *  */
	int s;
	struct ifreq ifr;
	int ifrlen;
	struct sockaddr_in * addr;
	ifrlen = sizeof(ifr);
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if(s < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "socket(PF_INET, SOCK_DGRAM): %s\n", strerror(errno));
		return -1;
	}
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if(ioctl(s, SIOCGIFADDR, &ifr, &ifrlen) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "ioctl(s, SIOCGIFADDR, ...): %s\n", strerror(errno));
		close(s);
		return -1;
	}
	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	if(!inet_ntop(AF_INET, &addr->sin_addr, buf, len))
	{
		DPRINTF(E_ERROR, L_GENERAL, "inet_ntop(): %s\n", strerror(errno));
		close(s);
		return -1;
	}
	close(s);
	return 0;
}

int
getsysaddr(char * buf, int len)
{
	int i;
	int s = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	struct ifreq ifr;
	int ret = -1;

	for (i=1; i > 0; i++)
	{
		ifr.ifr_ifindex = i;
		if( ioctl(s, SIOCGIFNAME, &ifr) < 0 )
			break;
		if(ioctl(s, SIOCGIFADDR, &ifr, sizeof(struct ifreq)) < 0)
			continue;
		memcpy(&addr, &ifr.ifr_addr, sizeof(addr));
		if(strncmp(inet_ntoa(addr.sin_addr), "127.", 4) == 0)
			continue;
		if(!inet_ntop(AF_INET, &addr.sin_addr, buf, len))
		{
			DPRINTF(E_ERROR, L_GENERAL, "inet_ntop(): %s\n", strerror(errno));
			close(s);
			break;
		}
		ret = 0;
		break;
	}
	close(s);

	return(ret);
}

int
getifhwaddr(const char * ifname, char * buf, int len)
{
	/* SIOCGIFADDR struct ifreq *  */
	int s;
	struct ifreq ifr;
	int ifrlen;
	unsigned char addr[6];
	char mac_string[4];
	int i;
	ifrlen = sizeof(ifr);
	if( len < 12 )
	{
		return -2;
	}
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if(s < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "socket(PF_INET, SOCK_DGRAM): %s\n", strerror(errno));
		return -1;
	}
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if(ioctl(s, SIOCGIFHWADDR, &ifr, &ifrlen) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "ioctl(s, SIOCGIFHWADDR, ...): %s\n", strerror(errno));
		close(s);
		return -1;
	}
	close(s);

	memmove( addr, ifr.ifr_hwaddr.sa_data, 6);
	for (i=0; i<6; ++i) {
		sprintf(mac_string, "%2.2x", addr[i]);
		strcat(buf, mac_string);
	}
	return 0;
}

int
get_remote_mac(struct in_addr ip_addr, unsigned char * mac)
{
	struct in_addr arp_ent;
	FILE * arp;
	char remote_ip[16];
	int matches, hwtype, flags;
	memset(mac, 0xFF, 6);

 	arp = fopen("/proc/net/arp", "r");
	if( !arp )
		return 1;
	while( !feof(arp) )
	{
	        matches = fscanf(arp, "%s 0x%X 0x%X %hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		                      remote_ip, &hwtype, &flags,
		                      &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		if( matches != 9 )
			continue;
		inet_pton(AF_INET, remote_ip, &arp_ent);
		if( ip_addr.s_addr == arp_ent.s_addr )
			break;
		mac[0] = 0xFF;
	}
	fclose(arp);

	if( mac[0] == 0xFF )
	{
		memset(mac, 0xFF, 6);
		return 1;
	}

	return 0;
}
