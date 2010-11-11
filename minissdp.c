/* $Id$ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 *
 * Copyright (c) 2006, Thomas Bernard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of the author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "config.h"
#include "upnpdescstrings.h"
#include "minidlnapath.h"
#include "upnphttp.h"
#include "upnpglobalvars.h"
#include "minissdp.h"
#include "log.h"

/* SSDP ip/port */
#define SSDP_PORT (1900)
#define SSDP_MCAST_ADDR ("239.255.255.250")

static int
AddMulticastMembership(int s, in_addr_t ifaddr/*const char * ifaddr*/)
{
	struct ip_mreq imr;	/* Ip multicast membership */

	/* setting up imr structure */
	imr.imr_multiaddr.s_addr = inet_addr(SSDP_MCAST_ADDR);
	/*imr.imr_interface.s_addr = htonl(INADDR_ANY);*/
	imr.imr_interface.s_addr = ifaddr;	/*inet_addr(ifaddr);*/
	
	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&imr, sizeof(struct ip_mreq)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "setsockopt(udp, IP_ADD_MEMBERSHIP): %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int
OpenAndConfSSDPReceiveSocket()
{
	int s;
	int i = 1;
	struct sockaddr_in sockname;
	
	if( (s = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "socket(udp): %s\n", strerror(errno));
		return -1;
	}	

	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
	{
		DPRINTF(E_WARN, L_SSDP, "setsockopt(udp, SO_REUSEADDR): %s\n", strerror(errno));
	}
	
	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_port = htons(SSDP_PORT);
	/* NOTE : it seems it doesnt work when binding on the specific address */
	/*sockname.sin_addr.s_addr = inet_addr(UPNP_MCAST_ADDR);*/
	sockname.sin_addr.s_addr = htonl(INADDR_ANY);
	/*sockname.sin_addr.s_addr = inet_addr(ifaddr);*/

	if(bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "bind(udp): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	i = n_lan_addr;
	while(i>0)
	{
		i--;
		if(AddMulticastMembership(s, lan_addr[i].addr.s_addr) < 0)
		{
			DPRINTF(E_WARN, L_SSDP,
			       "Failed to add multicast membership for address %s\n", 
			       lan_addr[i].str );
		}
	}

	return s;
}

/* open the UDP socket used to send SSDP notifications to
 * the multicast group reserved for them */
static int
OpenAndConfSSDPNotifySocket(in_addr_t addr)
{
	int s;
	unsigned char loopchar = 0;
	int bcast = 1;
	struct in_addr mc_if;
	struct sockaddr_in sockname;
	
	if( (s = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "socket(udp_notify): %s\n", strerror(errno));
		return -1;
	}

	mc_if.s_addr = addr;	/*inet_addr(addr);*/

	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopchar, sizeof(loopchar)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "setsockopt(udp_notify, IP_MULTICAST_LOOP): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&mc_if, sizeof(mc_if)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "setsockopt(udp_notify, IP_MULTICAST_IF): %s\n", strerror(errno));
		close(s);
		return -1;
	}
	
	if(setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "setsockopt(udp_notify, SO_BROADCAST): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_addr.s_addr = addr;	/*inet_addr(addr);*/

	if (bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "bind(udp_notify): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	return s;
}

int
OpenAndConfSSDPNotifySockets(int * sockets)
{
	int i, j;
	for(i=0; i<n_lan_addr; i++)
	{
		sockets[i] = OpenAndConfSSDPNotifySocket(lan_addr[i].addr.s_addr);
		if(sockets[i] < 0)
		{
			for(j=0; j<i; j++)
			{
				close(sockets[j]);
				sockets[j] = -1;
			}
			return -1;
		}
	}
	return 0;
}

/*
 * response from a LiveBox (Wanadoo)
HTTP/1.1 200 OK
CACHE-CONTROL: max-age=1800
DATE: Thu, 01 Jan 1970 04:03:23 GMT
EXT:
LOCATION: http://192.168.0.1:49152/gatedesc.xml
SERVER: Linux/2.4.17, UPnP/1.0, Intel SDK for UPnP devices /1.2
ST: upnp:rootdevice
USN: uuid:75802409-bccb-40e7-8e6c-fa095ecce13e::upnp:rootdevice

 * response from a Linksys 802.11b :
HTTP/1.1 200 OK
Cache-Control:max-age=120
Location:http://192.168.5.1:5678/rootDesc.xml
Server:NT/5.0 UPnP/1.0
ST:upnp:rootdevice
USN:uuid:upnp-InternetGatewayDevice-1_0-0090a2777777::upnp:rootdevice
EXT:
 */

static const char * const known_service_types[] =
{
	uuidvalue,
	"upnp:rootdevice",
	"urn:schemas-upnp-org:device:MediaServer:",
	"urn:schemas-upnp-org:service:ContentDirectory:",
	"urn:schemas-upnp-org:service:ConnectionManager:",
	"urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:",
	0
};

/* not really an SSDP "announce" as it is the response
 * to a SSDP "M-SEARCH" */
static void
SendSSDPAnnounce2(int s, struct sockaddr_in sockname, int st_no,
                  const char * host, unsigned short port)
{
	int l, n;
	char buf[512];
	/* TODO :
	 * follow guideline from document "UPnP Device Architecture 1.0"
	 * put in uppercase.
	 * DATE: is recommended
	 * SERVER: OS/ver UPnP/1.0 minidlna/1.0
	 * - check what to put in the 'Cache-Control' header 
	 * */
	char   szTime[30];
	time_t tTime = time(NULL);
	strftime(szTime, 30,"%a, %d %b %Y %H:%M:%S GMT" , gmtime(&tTime));

	l = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n"
		"CACHE-CONTROL: max-age=%u\r\n"
		"DATE: %s\r\n"
		"ST: %s%s\r\n"
		"USN: %s%s%s%s\r\n"
		"EXT:\r\n"
		"SERVER: " MINIDLNA_SERVER_STRING "\r\n"
		"LOCATION: http://%s:%u" ROOTDESC_PATH "\r\n"
		"Content-Length: 0\r\n"
		"\r\n",
		(runtime_vars.notify_interval<<1)+10,
		szTime,
		known_service_types[st_no], (st_no>1?"1":""),
		uuidvalue, (st_no>0?"::":""), (st_no>0?known_service_types[st_no]:""), (st_no>1?"1":""),
		host, (unsigned int)port);
	//DEBUG DPRINTF(E_DEBUG, L_SSDP, "Sending M-SEARCH response:\n%s", buf);
	n = sendto(s, buf, l, 0,
	           (struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
	if(n < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "sendto(udp): %s\n", strerror(errno));
	}
}

static void
SendSSDPNotifies(int s, const char * host, unsigned short port,
                 unsigned int lifetime)
{
	struct sockaddr_in sockname;
	int l, n, dup, i=0;
	char bufr[512];

	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_port = htons(SSDP_PORT);
	sockname.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);

	for( dup=0; dup<2; dup++ )
	{
		if( dup )
			usleep(200000);
		i=0;
		while(known_service_types[i])
		{
			l = snprintf(bufr, sizeof(bufr), 
					"NOTIFY * HTTP/1.1\r\n"
					"HOST:%s:%d\r\n"
					"CACHE-CONTROL:max-age=%u\r\n"
					"LOCATION:http://%s:%d" ROOTDESC_PATH"\r\n"
					"SERVER: " MINIDLNA_SERVER_STRING "\r\n"
					"NT:%s%s\r\n"
					"USN:%s%s%s%s\r\n"
					"NTS:ssdp:alive\r\n"
					"\r\n",
					SSDP_MCAST_ADDR, SSDP_PORT,
					lifetime,
					host, port,
					known_service_types[i], (i>1?"1":""),
					uuidvalue, (i>0?"::":""), (i>0?known_service_types[i]:""), (i>1?"1":"") );
			if(l>=sizeof(bufr))
			{
				DPRINTF(E_WARN, L_SSDP, "SendSSDPNotifies(): truncated output\n");
				l = sizeof(bufr);
			}
			//DEBUG DPRINTF(E_DEBUG, L_SSDP, "Sending NOTIFY:\n%s", bufr);
			n = sendto(s, bufr, l, 0,
				(struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
			if(n < 0)
			{
				DPRINTF(E_ERROR, L_SSDP, "sendto(udp_notify=%d, %s): %s\n", s, host, strerror(errno));
			}
			i++;
		}
	}
}

void
SendSSDPNotifies2(int * sockets,
                  unsigned short port,
                  unsigned int lifetime)
/*SendSSDPNotifies2(int * sockets, struct lan_addr_s * lan_addr, int n_lan_addr,
                  unsigned short port,
                  unsigned int lifetime)*/
{
	int i;
	DPRINTF(E_DEBUG, L_SSDP, "Sending SSDP notifies\n");
	for(i=0; i<n_lan_addr; i++)
	{
		SendSSDPNotifies(sockets[i], lan_addr[i].str, port, lifetime);
	}
}

/* ProcessSSDPRequest()
 * process SSDP M-SEARCH requests and responds to them */
void
ProcessSSDPRequest(int s, unsigned short port)
/*ProcessSSDPRequest(int s, struct lan_addr_s * lan_addr, int n_lan_addr,
                   unsigned short port)*/
{
	int n;
	char bufr[1500];
	socklen_t len_r;
	struct sockaddr_in sendername;
	int i, l;
	int lan_addr_index = 0;
	char * st = NULL, * mx = NULL, * man = NULL, * mx_end = NULL;
	int st_len = 0, mx_len = 0, man_len = 0, mx_val = 0;
	len_r = sizeof(struct sockaddr_in);

	n = recvfrom(s, bufr, sizeof(bufr), 0,
	             (struct sockaddr *)&sendername, &len_r);
	if(n < 0)
	{
		DPRINTF(E_ERROR, L_SSDP, "recvfrom(udp): %s\n", strerror(errno));
		return;
	}

	if(memcmp(bufr, "NOTIFY", 6) == 0)
	{
		/* ignore NOTIFY packets. We could log the sender and device type */
		return;
	}
	else if(memcmp(bufr, "M-SEARCH", 8) == 0)
	{
		//DEBUG DPRINTF(E_DEBUG, L_SSDP, "Received SSDP request:\n%.*s", n, bufr);
		for(i=0; i < n; i++)
		{
			if( bufr[i] == '*' )
				break;
		}
		if( !strcasestr(bufr+i, "HTTP/1.1") )
		{
			return;
		}
		while(i < n)
		{
			while((i < n - 1) && (bufr[i] != '\r' || bufr[i+1] != '\n'))
				i++;
			i += 2;
			if((i < n - 3) && (strncasecmp(bufr+i, "ST:", 3) == 0))
			{
				st = bufr+i+3;
				st_len = 0;
				while(*st == ' ' || *st == '\t') st++;
				while(st[st_len]!='\r' && st[st_len]!='\n') st_len++;
			}
			else if(strncasecmp(bufr+i, "MX:", 3) == 0)
			{
				mx = bufr+i+3;
				mx_len = 0;
				while(*mx == ' ' || *mx == '\t') mx++;
				while(mx[mx_len]!='\r' && mx[mx_len]!='\n') mx_len++;
        			mx_val = strtol(mx, &mx_end, 10);
			}
			else if(strncasecmp(bufr+i, "MAN:", 4) == 0)
			{
				man = bufr+i+4;
				man_len = 0;
				while(*man == ' ' || *man == '\t') man++;
				while(man[man_len]!='\r' && man[man_len]!='\n') man_len++;
			}
		}
		/*DPRINTF(E_INFO, L_SSDP, "SSDP M-SEARCH packet received from %s:%d\n",
	           inet_ntoa(sendername.sin_addr),
	           ntohs(sendername.sin_port) );*/
		if( ntohs(sendername.sin_port) <= 1024 || ntohs(sendername.sin_port) == 1900 )
		{
			DPRINTF(E_INFO, L_SSDP, "WARNING: Ignoring invalid SSDP M-SEARCH from %s [bad source port %d]\n",
			   inet_ntoa(sendername.sin_addr), ntohs(sendername.sin_port));
		}
		else if( !man || (strncmp(man, "\"ssdp:discover\"", 15) != 0) )
		{
			DPRINTF(E_INFO, L_SSDP, "WARNING: Ignoring invalid SSDP M-SEARCH from %s [bad MAN header %.*s]\n",
			   inet_ntoa(sendername.sin_addr), man_len, man);
		}
		else if( !mx || mx == mx_end || mx_val < 0 ) {
			DPRINTF(E_INFO, L_SSDP, "WARNING: Ignoring invalid SSDP M-SEARCH from %s [bad MX header %.*s]\n",
			   inet_ntoa(sendername.sin_addr), mx_len, mx);
		}
		else if( st && (st_len > 0) )
		{
			DPRINTF(E_INFO, L_SSDP, "SSDP M-SEARCH from %s:%d ST: %.*s, MX: %.*s, MAN: %.*s\n",
	        	   inet_ntoa(sendername.sin_addr),
	           	   ntohs(sendername.sin_port),
			   st_len, st, mx_len, mx, man_len, man);
			/* find in which sub network the client is */
			for(i = 0; i<n_lan_addr; i++)
			{
				if( (sendername.sin_addr.s_addr & lan_addr[i].mask.s_addr)
				   == (lan_addr[i].addr.s_addr & lan_addr[i].mask.s_addr))
				{
					lan_addr_index = i;
					break;
				}
			}
			/* Responds to request with a device as ST header */
			for(i = 0; known_service_types[i]; i++)
			{
				l = strlen(known_service_types[i]);
				if(l<=st_len && (0 == memcmp(st, known_service_types[i], l)))
				{
					/* Check version number - must always be 1 currently. */
					if( (st[st_len-2] == ':') && (atoi(st+st_len-1) != 1) )
						break;
					usleep(random()>>20);
					SendSSDPAnnounce2(s, sendername,
					                  i,
					                  lan_addr[lan_addr_index].str, port);
					break;
				}
			}
			/* Responds to request with ST: ssdp:all */
			/* strlen("ssdp:all") == 8 */
			if(st_len==8 && (0 == memcmp(st, "ssdp:all", 8)))
			{
				for(i=0; known_service_types[i]; i++)
				{
					l = (int)strlen(known_service_types[i]);
					SendSSDPAnnounce2(s, sendername,
					                  i,
					                  lan_addr[lan_addr_index].str, port);
				}
			}
		}
		else
		{
			DPRINTF(E_INFO, L_SSDP, "Invalid SSDP M-SEARCH from %s:%d\n",
	        	   inet_ntoa(sendername.sin_addr), ntohs(sendername.sin_port));
		}
	}
	else
	{
		DPRINTF(E_WARN, L_SSDP, "Unknown udp packet received from %s:%d\n",
		       inet_ntoa(sendername.sin_addr), ntohs(sendername.sin_port));
	}
}

/* This will broadcast ssdp:byebye notifications to inform 
 * the network that UPnP is going down. */
int
SendSSDPGoodbye(int * sockets, int n_sockets)
{
	struct sockaddr_in sockname;
	int n, l;
	int i, j;
	char bufr[512];

	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_port = htons(SSDP_PORT);
	sockname.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);

	for(j=0; j<n_sockets; j++)
	{
		for(i=0; known_service_types[i]; i++)
		{
			l = snprintf(bufr, sizeof(bufr),
			             "NOTIFY * HTTP/1.1\r\n"
			             "HOST:%s:%d\r\n"
			             "NT:%s%s\r\n"
			             "USN:%s%s%s%s\r\n"
			             "NTS:ssdp:byebye\r\n"
			             "\r\n",
			             SSDP_MCAST_ADDR, SSDP_PORT,
			             known_service_types[i], (i>1?"1":""),
			             uuidvalue, (i>0?"::":""), (i>0?known_service_types[i]:""), (i>1?"1":"") );
			//DEBUG DPRINTF(E_DEBUG, L_SSDP, "Sending NOTIFY:\n%s", bufr);
			n = sendto(sockets[j], bufr, l, 0,
			           (struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
			if(n < 0)
			{
				DPRINTF(E_ERROR, L_SSDP, "sendto(udp_shutdown=%d): %s\n", sockets[j], strerror(errno));
				return -1;
			}
		}
	}
	return 0;
}
