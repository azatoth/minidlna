/*
 * Linux/C based server for TiVo Home Media Option protocol
 *
 * Based on version 1.5.1 of
 *    "TiVo Connect Automatic Machine; Discovery Protocol Specification"
 * Based on version 1.1.0 of
 *    "TiVo Home Media Option; Music and Photos Server Protocol Specification"
 *
 * Dave Clemans, April 2003
 *
 * byRequest TiVo HMO Server
 * Copyright (C) 2003  Dave Clemans
 *
 * This file is based on byRequest, and is part of MiniDLNA.
 *
 * byRequest is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * byRequest is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with byRequest. If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#ifdef TIVO_SUPPORT
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/poll.h>
#include <netdb.h>

#include "tivo_beacon.h"
#include "upnpglobalvars.h"
#include "log.h"

static struct aBeacon* topBeacon = NULL;

/* OpenAndConfHTTPSocket() :
 * setup the socket used to handle incoming HTTP connections. */
int
OpenAndConfTivoBeaconSocket()
{
	int s;
	int i = 1;
	struct sockaddr_in beacon;

	if( (s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		DPRINTF(E_ERROR, L_TIVO, "socket(http): %s\n", strerror(errno));
		return -1;
	}

	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
	{
		DPRINTF(E_WARN, L_TIVO, "setsockopt(http, SO_REUSEADDR): %s\n", strerror(errno));
	}

	memset(&beacon, 0, sizeof(struct sockaddr_in));
	beacon.sin_family = AF_INET;
	beacon.sin_port = htons(2190);
	beacon.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(s, (struct sockaddr *)&beacon, sizeof(struct sockaddr_in)) < 0)
	{
		DPRINTF(E_ERROR, L_TIVO, "bind(http): %s\n", strerror(errno));
		close(s);
		return -1;
	}
	i = 1;
	if(setsockopt(s, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i)) < 0 )
	{
		DPRINTF(E_WARN, L_TIVO, "setsockopt(http, SO_BROADCAST): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	return s;
}

/*
 * Returns the interface broadcast address to be used for beacons
 */
uint32_t
getBcastAddress(void)
{
	int i, rval;
	int s = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sin;
	struct sockaddr_in addr;
	struct ifreq ifr;

	for (i=1; i > 0; i++)
	{
		ifr.ifr_ifindex = i;
		if( ioctl(s, SIOCGIFNAME, &ifr) < 0 )
			break;
		if(ioctl(s, SIOCGIFADDR, &ifr, sizeof(struct ifreq)) < 0)
			continue;
		memcpy(&addr, &ifr.ifr_addr, sizeof(addr));
		if(strcmp(inet_ntoa(addr.sin_addr), lan_addr[0].str) == 0)
		{
			rval = ioctl(s, SIOCGIFBRDADDR, &ifr);
			if( rval < 0 )
			{
				DPRINTF(E_ERROR, L_TIVO, "Failed to get broadcast addr on %s [%s]\n", ifr.ifr_name, strerror(errno));
				close(s);
				return INADDR_BROADCAST;
			}
			memcpy(&sin, &ifr.ifr_broadaddr, sizeof(sin));
			close(s);
			DPRINTF(E_DEBUG, L_TIVO, "Interface: %s broadcast addr %s\n", ifr.ifr_name, inet_ntoa(sin.sin_addr));
			return ntohl((uint32_t)(sin.sin_addr.s_addr));
		}
	}

	return INADDR_BROADCAST;
}

/*
 * Send outgoing beacon to the specified address
 * This will either be a specific or broadcast address
 */
void
sendBeaconMessage(int fd, struct sockaddr_in * client, int len, int broadcast)
{
	char * mesg;
	int mesg_len;

	mesg_len = asprintf(&mesg, "TiVoConnect=1\n"
	                           "swversion=%s\n"
	                           "method=%s\n"
	                           "identity=%s\n"
	                           "machine=%s\n"
	                           "platform=pc/minidlna\n"
	                           "services=TiVoMediaServer:%d/http\n",
	                           "1.0",
	                           broadcast ? "broadcast" : "connected",
	                           uuidvalue, friendly_name, runtime_vars.port);
	DPRINTF(E_DEBUG, L_TIVO, "Sending TiVo beacon to %s\n", inet_ntoa(client->sin_addr));
	sendto(fd, mesg, mesg_len, 0, (struct sockaddr*)client, len);
	free(mesg);
}

/*
 * Parse and save a received beacon packet from another server, or from
 * a TiVo.
 *
 * Returns true if this was a broadcast beacon msg
 */
int
rcvBeaconMessage(char * beacon)
{
	char * tivoConnect = NULL;
	char * method = NULL;
	char * identity = NULL;
	char * machine = NULL;
	char * platform = NULL;
	char * services = NULL;
	char * cp;
	char * scp;
	char * tokptr;
	struct aBeacon * b;
	time_t current;

	cp = strtok_r(beacon, "=\r\n", &tokptr);
	while( cp != NULL )
	{
		scp = cp;
		cp = strtok_r( NULL, "=\r\n", &tokptr );
		if( strcasecmp(scp, "tivoconnect") == 0 )
			tivoConnect = cp;
		else if( strcasecmp(scp, "method") == 0 )
			method = cp;
		else if( strcasecmp(scp, "identity") == 0 )
			identity = cp;
		else if( strcasecmp(scp, "machine") == 0 )
			machine = cp;
		else if( strcasecmp(scp, "platform") == 0 )
			platform = cp;
		else if( strcasecmp(scp, "services") == 0 )
			services = cp;
		cp = strtok_r(NULL, "=\r\n", &tokptr);
	}

	if( tivoConnect == NULL )
		return 0;

	/* It's pointless to respond to our own beacon */
	if( strcmp(identity, uuidvalue) == 0)
		return 0;

	current = time(NULL);
	for( b = topBeacon; b != NULL; b = b->next )
	{
		if( strcasecmp(machine, b->machine) == 0 ||
		    strcasecmp(identity, b->identity) == 0 )
			break;
	}
	if( b == NULL )
	{
		b = calloc(1, sizeof(*b));

		if( machine )
			b->machine = strdup(machine);
		if( identity )
			b->identity = strdup(identity);

		b->next = topBeacon;
		topBeacon = b;

		DPRINTF(E_DEBUG, L_TIVO, "Received new beacon: machine(%s) platform(%s) services(%s)\n", 
		         machine ? machine : "-",
		         platform ? platform : "-", 
		         services ? services : "-" );
	}
#ifdef DEBUG
	int len;
	char buf[32];
	static time_t lastSummary = 0;

	b->lastSeen = current;
	if( !lastSummary )
		lastSummary = current;

	if( lastSummary + 1800 < current )
	{  /* Give a summary of received server beacons every half hour or so */
		len = 0;
		for( b = topBeacon; b != NULL; b = b->next )
		{
			len += strlen(b->machine) + 32;
		}
		scp = malloc(len + 128);
		strcpy( scp, "Known servers: " );
		for( b = topBeacon; b != NULL; b = b->next )
		{
			strcat(scp, b->machine);
			sprintf(buf, "(%ld)", current - b->lastSeen);
			strcat(scp, buf);
			if( b->next != NULL )
				strcat(scp, ",");
		}
		strcat(scp, "\n");
		DPRINTF(E_DEBUG, L_TIVO, "%s\n", scp);
		free(scp);
		lastSummary = current;
	}
#endif
	if( strcasecmp(method, "broadcast") == 0 )
		return 1;
	return 0;
}

void ProcessTiVoBeacon(int s)
{
	int n;
	char *cp;
	struct sockaddr_in sendername;
	socklen_t len_r;
	char bufr[1500];
	len_r = sizeof(struct sockaddr_in);

	/* We only expect to see beacon msgs from TiVo's and possibly other tivo servers */
	n = recvfrom(s, bufr, sizeof(bufr), 0,
	             (struct sockaddr *)&sendername, &len_r);
	if( n > 0 )
		bufr[n] = '\0';
	for( cp = bufr; *cp; cp++ )
		/* do nothing */;
	if( cp[-1] == '\r' || cp[-1] == '\n' )
		*--cp = '\0';
	if( cp[-1] == '\r' || cp[-1] == '\n' )
		*--cp = '\0';

	if( rcvBeaconMessage(bufr) )
		sendBeaconMessage(s, &sendername, len_r, 0);
}
#endif // TIVO_SUPPORT
