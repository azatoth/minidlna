/* MiniDLNA project
 * http://minidlna.sourceforge.net/
 * (c) 2008 Justin Maggard
 *
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution 
 *
 * Portions of the code from the MiniUPnP Project
 * (c) Thomas Bernard licensed under BSD revised license
 * detailed in the LICENSE.miniupnpd file provided within
 * the distribution.
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <syslog.h>
#include <ctype.h>
#include "config.h"
#include "upnphttp.h"
#include "upnpdescgen.h"
#include "miniupnpdpath.h"
#include "upnpsoap.h"
#include "upnpevents.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/sendfile.h>

#include "upnpglobalvars.h"
#include <sqlite3.h>
#include <libexif/exif-loader.h>
#if 0 //JPEG_RESIZE
#include <gd.h>
#endif

struct upnphttp * 
New_upnphttp(int s)
{
	struct upnphttp * ret;
	if(s<0)
		return NULL;
	ret = (struct upnphttp *)malloc(sizeof(struct upnphttp));
	if(ret == NULL)
		return NULL;
	memset(ret, 0, sizeof(struct upnphttp));
	ret->socket = s;
	return ret;
}

void
CloseSocket_upnphttp(struct upnphttp * h)
{
	if(close(h->socket) < 0)
	{
		syslog(LOG_ERR, "CloseSocket_upnphttp: close(%d): %m", h->socket);
	}
	h->socket = -1;
	h->state = 100;
}

void
Delete_upnphttp(struct upnphttp * h)
{
	if(h)
	{
		if(h->socket >= 0)
			CloseSocket_upnphttp(h);
		if(h->req_buf)
			free(h->req_buf);
		if(h->res_buf)
			free(h->res_buf);
		free(h);
	}
}

/* parse HttpHeaders of the REQUEST */
static void
ParseHttpHeaders(struct upnphttp * h)
{
	char * line;
	char * colon;
	char * p;
	int n;
	line = h->req_buf;
	/* TODO : check if req_buf, contentoff are ok */
	while(line < (h->req_buf + h->req_contentoff))
	{
		colon = strchr(line, ':');
		if(colon)
		{
			if(strncasecmp(line, "Content-Length", 14)==0)
			{
				p = colon;
				while(*p < '0' || *p > '9')
					p++;
				h->req_contentlen = atoi(p);
				/*printf("*** Content-Lenght = %d ***\n", h->req_contentlen);
				printf("    readbufflen=%d contentoff = %d\n",
					h->req_buflen, h->req_contentoff);*/
			}
			else if(strncasecmp(line, "SOAPAction", 10)==0)
			{
				p = colon;
				n = 0;
				while(*p == ':' || *p == ' ' || *p == '\t')
					p++;
				while(p[n]>=' ')
				{
					n++;
				}
				if((p[0] == '"' && p[n-1] == '"')
				  || (p[0] == '\'' && p[n-1] == '\''))
				{
					p++; n -= 2;
				}
				h->req_soapAction = p;
				h->req_soapActionLen = n;
			}
#ifdef ENABLE_EVENTS
			else if(strncasecmp(line, "Callback", 8)==0)
			{
				p = colon;
				while(*p != '<' && *p != '\r' )
					p++;
				n = 0;
				while(p[n] != '>' && p[n] != '\r' )
					n++;
				h->req_Callback = p + 1;
				h->req_CallbackLen = MAX(0, n - 1);
			}
			else if(strncasecmp(line, "SID", 3)==0)
			{
				p = colon + 1;
				while(isspace(*p))
					p++;
				n = 0;
				while(!isspace(p[n]))
					n++;
				h->req_SID = p;
				h->req_SIDLen = n;
			}
			/* Timeout: Seconds-nnnn */
/* TIMEOUT
Recommended. Requested duration until subscription expires,
either number of seconds or infinite. Recommendation
by a UPnP Forum working committee. Defined by UPnP vendor.
 Consists of the keyword "Second-" followed (without an
intervening space) by either an integer or the keyword "infinite". */
			else if(strncasecmp(line, "Timeout", 7)==0)
			{
				p = colon + 1;
				while(isspace(*p))
					p++;
				if(strncasecmp(p, "Second-", 7)==0) {
					h->req_Timeout = atoi(p+7);
				}
			}
#endif
#if 1
			// Range: bytes=xxx-yyy
			else if(strncasecmp(line, "Range", 5)==0)
			{
				p = colon + 1;
				while(isspace(*p))
					p++;
				if(strncasecmp(p, "bytes=", 6)==0) {
					h->reqflags |= FLAG_RANGE;
					h->req_RangeEnd = atoll(index(p+6, '-')+1);
					h->req_RangeStart = atoll(p+6);
printf("Range Start-End: %lld-%lld\n", h->req_RangeStart, h->req_RangeEnd);
				}
			}
			else if(strncasecmp(line, "Host", 4)==0)
			{
				h->reqflags |= FLAG_HOST;
			}
			else if(strncasecmp(line, "Transfer-Encoding", 17)==0)
			{
				p = colon + 1;
				while(isspace(*p))
					p++;
				if(strncasecmp(p, "chunked", 7)==0)
				{
					h->reqflags |= FLAG_CHUNKED;
				}
			}
			else if(strncasecmp(line, "getcontentFeatures.dlna.org", 27)==0)
			{
				p = colon + 1;
				while(isspace(*p))
					p++;
				if( (*p != '1') || !isspace(p[1]) )
					h->reqflags |= FLAG_INVALID_REQ;
			}
			else if(strncasecmp(line, "TimeSeekRange.dlna.org", 22)==0)
			{
				h->reqflags |= FLAG_TIMESEEK;
			}
			else if(strncasecmp(line, "realTimeInfo.dlna.org", 21)==0)
			{
				h->reqflags |= FLAG_REALTIMEINFO;
			}
			else if(strncasecmp(line, "transferMode.dlna.org", 21)==0)
			{
				p = colon + 1;
				while(isspace(*p))
					p++;
				if(strncasecmp(p, "Streaming", 9)==0)
				{
					h->reqflags |= FLAG_XFERSTREAMING;
				}
				if(strncasecmp(p, "Interactive", 11)==0)
				{
					h->reqflags |= FLAG_XFERINTERACTIVE;
				}
				if(strncasecmp(p, "Background", 10)==0)
				{
					h->reqflags |= FLAG_XFERBACKGROUND;
				}
			}
#endif
		}
		while(!(line[0] == '\r' && line[1] == '\n'))
			line++;
		line += 2;
	}
	if( h->reqflags & FLAG_CHUNKED )
	{
		if( h->req_buflen > h->req_contentoff )
		{
			h->req_chunklen = strtol(line, NULL, 16);
			while(!(line[0] == '\r' && line[1] == '\n'))
			{
				line++;
				h->req_contentoff++;
			}
			h->req_contentoff += 2;
		}
		else
		{
			h->req_chunklen = -1;
		}
	}
}

/* very minimalistic 400 error message */
static void
Send400(struct upnphttp * h)
{
	static const char body400[] =
		"<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>"
		"<BODY><H1>Bad Request</H1>The request is invalid"
		" for this HTTP version.</BODY></HTML>\r\n";
	h->respflags = FLAG_HTML;
	BuildResp2_upnphttp(h, 400, "Bad Request",
	                    body400, sizeof(body400) - 1);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

/* very minimalistic 404 error message */
static void
Send404(struct upnphttp * h)
{
/*
	static const char error404[] = "HTTP/1.1 404 Not found\r\n"
		"Connection: close\r\n"
		"Content-type: text/html\r\n"
		"\r\n"
		"<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>"
		"<BODY><H1>Not Found</H1>The requested URL was not found"
		" on this server.</BODY></HTML>\r\n";
	int n;
	n = send(h->socket, error404, sizeof(error404) - 1, 0);
	if(n < 0)
	{
		syslog(LOG_ERR, "Send404: send(http): %m");
	}*/
	static const char body404[] =
		"<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>"
		"<BODY><H1>Not Found</H1>The requested URL was not found"
		" on this server.</BODY></HTML>\r\n";
	h->respflags = FLAG_HTML;
	BuildResp2_upnphttp(h, 404, "Not Found",
	                    body404, sizeof(body404) - 1);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

/* very minimalistic 404 error message */
static void
Send406(struct upnphttp * h)
{
	static const char body406[] =
		"<HTML><HEAD><TITLE>406 Not Acceptable</TITLE></HEAD>"
		"<BODY><H1>Not Acceptable</H1>An unsupported operation "
		" was requested.</BODY></HTML>\r\n";
	h->respflags = FLAG_HTML;
	BuildResp2_upnphttp(h, 406, "Not Acceptable",
	                    body406, sizeof(body406) - 1);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

/* very minimalistic 404 error message */
static void
Send416(struct upnphttp * h)
{
	static const char body416[] =
		"<HTML><HEAD><TITLE>416 Requested Range Not Satisfiable</TITLE></HEAD>"
		"<BODY><H1>Requested Range Not Satisfiable</H1>The requested range"
		" was outside the file's size.</BODY></HTML>\r\n";
	h->respflags = FLAG_HTML;
	BuildResp2_upnphttp(h, 416, "Requested Range Not Satisfiable",
	                    body416, sizeof(body416) - 1);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

/* very minimalistic 501 error message */
static void
Send501(struct upnphttp * h)
{
/*
	static const char error501[] = "HTTP/1.1 501 Not Implemented\r\n"
		"Connection: close\r\n"
		"Content-type: text/html\r\n"
		"\r\n"
		"<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>"
		"<BODY><H1>Not Implemented</H1>The HTTP Method "
		"is not implemented by this server.</BODY></HTML>\r\n";
	int n;
	n = send(h->socket, error501, sizeof(error501) - 1, 0);
	if(n < 0)
	{
		syslog(LOG_ERR, "Send501: send(http): %m");
	}
*/
	static const char body501[] = 
		"<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>"
		"<BODY><H1>Not Implemented</H1>The HTTP Method "
		"is not implemented by this server.</BODY></HTML>\r\n";
	h->respflags = FLAG_HTML;
	BuildResp2_upnphttp(h, 501, "Not Implemented",
	                    body501, sizeof(body501) - 1);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

static const char *
findendheaders(const char * s, int len)
{
	while(len-->0)
	{
		if(s[0]=='\r' && s[1]=='\n' && s[2]=='\r' && s[3]=='\n')
			return s;
		s++;
	}
	return NULL;
}

#ifdef HAS_DUMMY_SERVICE
static void
sendDummyDesc(struct upnphttp * h)
{
	static const char xml_desc[] = "<?xml version=\"1.0\"?>\r\n"
		"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
		" <specVersion>"
		"    <major>1</major>"
		"    <minor>0</minor>"
		"  </specVersion>"
		"  <actionList />"
		"  <serviceStateTable />"
		"</scpd>\r\n";
	BuildResp_upnphttp(h, xml_desc, sizeof(xml_desc)-1);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}
#endif

/* Sends the description generated by the parameter */
static void
sendXMLdesc(struct upnphttp * h, char * (f)(int *))
{
	char * desc;
	int len;
	desc = f(&len);
	if(!desc)
	{
		static const char error500[] = "<HTML><HEAD><TITLE>Error 500</TITLE>"
		   "</HEAD><BODY>Internal Server Error</BODY></HTML>\r\n";
		syslog(LOG_ERR, "Failed to generate XML description");
		h->respflags = FLAG_HTML;
		BuildResp2_upnphttp(h, 500, "Internal Server Error",
		                    error500, sizeof(error500)-1);
	}
	else
	{
		BuildResp_upnphttp(h, desc, len);
	}
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
	free(desc);
}

/* ProcessHTTPPOST_upnphttp()
 * executes the SOAP query if it is possible */
static void
ProcessHTTPPOST_upnphttp(struct upnphttp * h)
{
	if((h->req_buflen - h->req_contentoff) >= h->req_contentlen)
	{
		if(h->req_soapAction)
		{
			/* we can process the request */
//printf("__LINE %d__ SOAPAction: %s [%d]\n", __LINE__, h->req_soapAction, h->req_soapActionLen);
//			syslog(LOG_INFO, "SOAPAction: %.*s",
//				h->req_soapActionLen, h->req_soapAction);
//printf("__LINE %d__ SOAPAction: %.*s\n", __LINE__, h->req_soapActionLen, h->req_soapAction);
			ExecuteSoapAction(h, 
				h->req_soapAction,
				h->req_soapActionLen);
		}
		else
		{
			static const char err400str[] =
				"<html><body>Bad request</body></html>";
			syslog(LOG_INFO, "No SOAPAction in HTTP headers");
			h->respflags = FLAG_HTML;
			BuildResp2_upnphttp(h, 400, "Bad Request",
			                    err400str, sizeof(err400str) - 1);
			SendResp_upnphttp(h);
			CloseSocket_upnphttp(h);
		}
	}
	else
	{
		/* waiting for remaining data */
		h->state = 1;
	}
}

#ifdef ENABLE_EVENTS
static void
ProcessHTTPSubscribe_upnphttp(struct upnphttp * h, const char * path)
{
	const char * sid;
	syslog(LOG_DEBUG, "ProcessHTTPSubscribe %s", path);
	syslog(LOG_DEBUG, "Callback '%.*s' Timeout=%d",
	       h->req_CallbackLen, h->req_Callback, h->req_Timeout);
	syslog(LOG_DEBUG, "SID '%.*s'", h->req_SIDLen, h->req_SID);
	if(!h->req_Callback && !h->req_SID) {
		/* Missing or invalid CALLBACK : 412 Precondition Failed.
		 * If CALLBACK header is missing or does not contain a valid HTTP URL,
		 * the publisher must respond with HTTP error 412 Precondition Failed*/
		BuildResp2_upnphttp(h, 412, "Precondition Failed", 0, 0);
		SendResp_upnphttp(h);
		CloseSocket_upnphttp(h);
	} else {
	/* - add to the subscriber list
	 * - respond HTTP/x.x 200 OK 
	 * - Send the initial event message */
/* Server:, SID:; Timeout: Second-(xx|infinite) */
		if(h->req_Callback) {
			sid = upnpevents_addSubscriber(path, h->req_Callback,
			                               h->req_CallbackLen, h->req_Timeout);
			h->respflags = FLAG_TIMEOUT;
			if(sid) {
				syslog(LOG_DEBUG, "generated sid=%s", sid);
				h->respflags |= FLAG_SID;
				h->req_SID = sid;
				h->req_SIDLen = strlen(sid);
			}
			BuildResp_upnphttp(h, 0, 0);
		} else {
			/* subscription renew */
			/* Invalid SID
412 Precondition Failed. If a SID does not correspond to a known,
un-expired subscription, the publisher must respond
with HTTP error 412 Precondition Failed. */
			if(renewSubscription(h->req_SID, h->req_SIDLen, h->req_Timeout) < 0) {
				BuildResp2_upnphttp(h, 412, "Precondition Failed", 0, 0);
			} else {
				/* A DLNA device must enforce a 5 minute timeout */
				h->respflags = FLAG_TIMEOUT;
				h->req_Timeout = 300;
				BuildResp_upnphttp(h, 0, 0);
			}
		}
		SendResp_upnphttp(h);
		CloseSocket_upnphttp(h);
	}
}

static void
ProcessHTTPUnSubscribe_upnphttp(struct upnphttp * h, const char * path)
{
	syslog(LOG_DEBUG, "ProcessHTTPUnSubscribe %s", path);
	syslog(LOG_DEBUG, "SID '%.*s'", h->req_SIDLen, h->req_SID);
	/* Remove from the list */
	if(upnpevents_removeSubscriber(h->req_SID, h->req_SIDLen) < 0) {
		BuildResp2_upnphttp(h, 412, "Precondition Failed", 0, 0);
	} else {
		BuildResp_upnphttp(h, 0, 0);
	}
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}
#endif

/* Parse and process Http Query 
 * called once all the HTTP headers have been received. */
static void
ProcessHttpQuery_upnphttp(struct upnphttp * h)
{
	char HttpCommand[16];
	char HttpUrl[128];
	char * HttpVer;
	char * p;
	int i;
	p = h->req_buf;
	if(!p)
		return;
	for(i = 0; i<15 && *p != ' ' && *p != '\r'; i++)
		HttpCommand[i] = *(p++);
	HttpCommand[i] = '\0';
	while(*p==' ')
		p++;
	if(strncmp(p, "http://", 7) == 0)
	{
		p = p+7;
		while(*p!='/')
			p++;
	}
	for(i = 0; i<127 && *p != ' ' && *p != '\r'; i++)
		HttpUrl[i] = *(p++);
	HttpUrl[i] = '\0';
	while(*p==' ')
		p++;
	HttpVer = h->HttpVer;
	for(i = 0; i<15 && *p != '\r'; i++)
		HttpVer[i] = *(p++);
	HttpVer[i] = '\0';
	syslog(LOG_INFO, "HTTP REQUEST : %s %s (%s)",
	       HttpCommand, HttpUrl, HttpVer);
	//DEBUG printf("HTTP REQUEST:\n%s\n", h->req_buf);
	ParseHttpHeaders(h);

	if( (h->reqflags & FLAG_CHUNKED) && (h->req_chunklen > (h->req_buflen - h->req_contentoff) || h->req_chunklen < 0) )
	{
		/* waiting for remaining data */
		printf("*** %d < %d\n", (h->req_buflen - h->req_contentoff), h->req_contentlen);
		printf("Chunked request [%ld].  Need more input.\n", h->req_chunklen);
		h->state = 2;
	}
	else if(strcmp("POST", HttpCommand) == 0)
	{
		h->req_command = EPost;
		ProcessHTTPPOST_upnphttp(h);
	}
	else if((strcmp("GET", HttpCommand) == 0) || (strcmp("HEAD", HttpCommand) == 0))
	{
		if( ((strcmp(h->HttpVer, "HTTP/1.1")==0) && !(h->reqflags & FLAG_HOST)) || (h->reqflags & FLAG_INVALID_REQ) )
		{
			syslog(LOG_NOTICE, "Invalid request, responding ERROR 400.  (No Host specified in HTTP headers?)");
			Send400(h);
		}
		else if( h->reqflags & FLAG_TIMESEEK )
		{
			syslog(LOG_NOTICE, "DLNA TimeSeek requested, responding ERROR 406");
			Send406(h);
		}
		else if(strcmp("GET", HttpCommand) == 0)
		{
			h->req_command = EGet;
		}
		else
		{
			h->req_command = EHead;
		}
		if(strcmp(ROOTDESC_PATH, HttpUrl) == 0)
		{
			sendXMLdesc(h, genRootDesc);
		}
		else if(strcmp(CONTENTDIRECTORY_PATH, HttpUrl) == 0)
		{
			sendXMLdesc(h, genContentDirectory);
		}
		else if(strcmp(CONNECTIONMGR_PATH, HttpUrl) == 0)
		{
			sendXMLdesc(h, genConnectionManager);
		}
		else if(strcmp(X_MS_MEDIARECEIVERREGISTRAR_PATH, HttpUrl) == 0)
		{
			sendXMLdesc(h, genX_MS_MediaReceiverRegistrar);
		}
#ifdef HAS_DUMMY_SERVICE
		else if(strcmp(DUMMY_PATH, HttpUrl) == 0)
		{
			sendDummyDesc(h);
		}
#endif
		else if(strncmp(HttpUrl, "/MediaItems/", 12) == 0)
		{
			SendResp_dlnafile(h, HttpUrl+12);
			CloseSocket_upnphttp(h);
		}
		else if(strncmp(HttpUrl, "/Thumbnails/", 12) == 0)
		{
			SendResp_thumbnail(h, HttpUrl+12);
			CloseSocket_upnphttp(h);
		}
#if 0 //JPEG_RESIZE
		else if(strncmp(HttpUrl, "/Resized/", 7) == 0)
		{
			SendResp_resizedimg(h, HttpUrl+7);
			CloseSocket_upnphttp(h);
		}
#endif
		else
		{
			syslog(LOG_NOTICE, "%s not found, responding ERROR 404", HttpUrl);
			Send404(h);
		}
	}
#ifdef ENABLE_EVENTS
	else if(strcmp("SUBSCRIBE", HttpCommand) == 0)
	{
		h->req_command = ESubscribe;
		ProcessHTTPSubscribe_upnphttp(h, HttpUrl);
	}
	else if(strcmp("UNSUBSCRIBE", HttpCommand) == 0)
	{
		h->req_command = EUnSubscribe;
		ProcessHTTPUnSubscribe_upnphttp(h, HttpUrl);
	}
#else
	else if(strcmp("SUBSCRIBE", HttpCommand) == 0)
	{
		syslog(LOG_NOTICE, "SUBSCRIBE not implemented. ENABLE_EVENTS compile option disabled");
		Send501(h);
	}
#endif
	else
	{
		syslog(LOG_NOTICE, "Unsupported HTTP Command %s", HttpCommand);
		Send501(h);
	}
}


void
Process_upnphttp(struct upnphttp * h)
{
	char buf[2048];
	int n;
	if(!h)
		return;
	switch(h->state)
	{
	case 0:
		n = recv(h->socket, buf, 2048, 0);
		if(n<0)
		{
			syslog(LOG_ERR, "recv (state0): %m");
			h->state = 100;
		}
		else if(n==0)
		{
			syslog(LOG_WARNING, "HTTP Connection closed inexpectedly");
			h->state = 100;
		}
		else
		{
			const char * endheaders;
			/* if 1st arg of realloc() is null,
			 * realloc behaves the same as malloc() */
			h->req_buf = (char *)realloc(h->req_buf, n + h->req_buflen + 1);
			memcpy(h->req_buf + h->req_buflen, buf, n);
			h->req_buflen += n;
			h->req_buf[h->req_buflen] = '\0';
			/* search for the string "\r\n\r\n" */
			endheaders = findendheaders(h->req_buf, h->req_buflen);
			if(endheaders)
			{
				h->req_contentoff = endheaders - h->req_buf + 4;
				ProcessHttpQuery_upnphttp(h);
			}
		}
		break;
	case 1:
	case 2:
		n = recv(h->socket, buf, 2048, 0);
		if(n<0)
		{
			syslog(LOG_ERR, "recv (state1): %m");
			h->state = 100;
		}
		else if(n==0)
		{
			syslog(LOG_WARNING, "HTTP Connection closed inexpectedly");
			h->state = 100;
		}
		else
		{
			/*fwrite(buf, 1, n, stdout);*/	/* debug */
			h->req_buf = (char *)realloc(h->req_buf, n + h->req_buflen);
			memcpy(h->req_buf + h->req_buflen, buf, n);
			h->req_buflen += n;
			if((h->req_buflen - h->req_contentoff) >= h->req_contentlen)
			{
				if( h->state == 1 )
					ProcessHTTPPOST_upnphttp(h);
				else if( h->state == 2 )
					ProcessHttpQuery_upnphttp(h);
			}
		}
		break;
	default:
		syslog(LOG_WARNING, "Unexpected state: %d", h->state);
	}
}

static const char httpresphead[] =
	"%s %d %s\r\n"
	/*"Content-Type: text/xml; charset=\"utf-8\"\r\n"*/
	"Content-Type: %s\r\n"
	"Connection: close\r\n"
	"Content-Length: %d\r\n"
	/*"Server: miniupnpd/1.0 UPnP/1.0\r\n"*/
//	"Accept-Ranges: bytes\r\n"
//	"DATE: Wed, 24 Sep 2008 05:57:19 GMT\r\n"
	//"Server: " MINIUPNPD_SERVER_STRING "\r\n"
	;	/*"\r\n";*/
/*
		"<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body>"

		"</s:Body>"
		"</s:Envelope>";
*/
/* with response code and response message
 * also allocate enough memory */

void
BuildHeader_upnphttp(struct upnphttp * h, int respcode,
                     const char * respmsg,
                     int bodylen)
{
	int templen;
	if(!h->res_buf)
	{
		templen = sizeof(httpresphead) + 128 + bodylen;
		h->res_buf = (char *)malloc(templen);
		h->res_buf_alloclen = templen;
	}
	h->res_buflen = snprintf(h->res_buf, h->res_buf_alloclen,
	                         //httpresphead, h->HttpVer,
	                         httpresphead, "HTTP/1.1",
	                         respcode, respmsg,
	                         (h->respflags&FLAG_HTML)?"text/html":"text/xml; charset=\"utf-8\"",
							 bodylen);
	/* Additional headers */
#ifdef ENABLE_EVENTS
	if(h->respflags & FLAG_TIMEOUT) {
		h->res_buflen += snprintf(h->res_buf + h->res_buflen,
		                          h->res_buf_alloclen - h->res_buflen,
		                          "Timeout: Second-");
		if(h->req_Timeout) {
			h->res_buflen += snprintf(h->res_buf + h->res_buflen,
			                          h->res_buf_alloclen - h->res_buflen,
			                          "%d\r\n", h->req_Timeout);
		} else {
			h->res_buflen += snprintf(h->res_buf + h->res_buflen,
			                          h->res_buf_alloclen - h->res_buflen,
			                          "300\r\n");
			                          //JM DLNA must force to 300 - "infinite\r\n");
		}
	}
	if(h->respflags & FLAG_SID) {
		h->res_buflen += snprintf(h->res_buf + h->res_buflen,
		                          h->res_buf_alloclen - h->res_buflen,
		                          "SID: %s\r\n", h->req_SID);
	}
#endif
#if 0 // DLNA
	h->res_buflen += snprintf(h->res_buf + h->res_buflen,
	                          h->res_buf_alloclen - h->res_buflen,
	                          "Server: Microsoft-Windows-NT/5.1 UPnP/1.0 UPnP-Device-Host/1.0\r\n");
	char   szTime[30];
	time_t curtime = time(NULL);
	strftime(szTime, 30,"%a, %d %b %Y %H:%M:%S GMT" , gmtime(&curtime));
	h->res_buflen += snprintf(h->res_buf + h->res_buflen,
	                          h->res_buf_alloclen - h->res_buflen,
	                          "Date: %s\r\n", szTime);
//	h->res_buflen += snprintf(h->res_buf + h->res_buflen,
//	                          h->res_buf_alloclen - h->res_buflen,
//	                          "contentFeatures.dlna.org: \r\n");
//	h->res_buflen += snprintf(h->res_buf + h->res_buflen,
//	                          h->res_buf_alloclen - h->res_buflen,
//	                          "EXT:\r\n");
#endif
	h->res_buf[h->res_buflen++] = '\r';
	h->res_buf[h->res_buflen++] = '\n';
	if(h->res_buf_alloclen < (h->res_buflen + bodylen))
	{
		h->res_buf = (char *)realloc(h->res_buf, (h->res_buflen + bodylen));
		h->res_buf_alloclen = h->res_buflen + bodylen;
	}
}

void
BuildResp2_upnphttp(struct upnphttp * h, int respcode,
                    const char * respmsg,
                    const char * body, int bodylen)
{
	BuildHeader_upnphttp(h, respcode, respmsg, bodylen);
	if( h->req_command == EHead )
		return;
	if(body)
		memcpy(h->res_buf + h->res_buflen, body, bodylen);
	h->res_buflen += bodylen;
}

/* responding 200 OK ! */
void
BuildResp_upnphttp(struct upnphttp * h,
                        const char * body, int bodylen)
{
	BuildResp2_upnphttp(h, 200, "OK", body, bodylen);
}

void
SendResp_upnphttp(struct upnphttp * h)
{
	int n;
printf("HTTP RESPONSE:\n%.*s\n", h->res_buflen, h->res_buf);
	n = send(h->socket, h->res_buf, h->res_buflen, 0);
	if(n<0)
	{
		syslog(LOG_ERR, "send(res_buf): %m");
	}
	else if(n < h->res_buflen)
	{
		/* TODO : handle correctly this case */
		syslog(LOG_ERR, "send(res_buf): %d bytes sent (out of %d)",
						n, h->res_buflen);
	}
}

void
SendResp_thumbnail(struct upnphttp * h, char * object)
{
	char header[1500];
	char sql_buf[256];
	char **result;
	char date[30];
	time_t curtime = time(NULL);
	int n;
	ExifData *ed;
	ExifLoader *l;

	memset(header, 0, 1500);

	if( h->reqflags & FLAG_XFERSTREAMING || h->reqflags & FLAG_RANGE )
	{
		syslog(LOG_NOTICE, "Hey, you can't specify transferMode as Streaming with an image!");
		Send406(h);
		return;
	}

	sprintf(sql_buf, "SELECT PATH from OBJECTS where OBJECT_ID = '%s'", object);
	sqlite3_get_table(db, sql_buf, &result, 0, 0, 0);
	printf("Serving up thumbnail for ObjectId: %s [%s]\n", object, result[1]);

	if( access(result[1], F_OK) == 0 )
	{
		strftime(date, 30,"%a, %d %b %Y %H:%M:%S GMT" , gmtime(&curtime));

		l = exif_loader_new();
		exif_loader_write_file(l, result[1]);
		ed = exif_loader_get_data(l);
		exif_loader_unref(l);

		if( !ed->size )
		{
			Send404(h);
			goto error;
		}
		sprintf(header, "HTTP/1.1 200 OK\r\n"
				"Content-Type: image/jpeg\r\n"
				"Content-Length: %d\r\n"
				"Connection: close\r\n"
				"Date: %s\r\n"
				"EXT:\r\n"
			 	"contentFeatures.dlna.org: DLNA.ORG_PN=JPEG_TN\r\n"
				"Server: RAIDiator/4.1, UPnP/1.0, MiniDLNA_TN/1.0\r\n",
				ed->size, date);

		if( h->reqflags & FLAG_XFERBACKGROUND )
		{
			strcat(header, "transferMode.dlna.org: Background\r\n");
		}
		else //if( h->reqflags & FLAG_XFERINTERACTIVE )
		{
			strcat(header, "transferMode.dlna.org: Interactive\r\n");
		}
		strcat(header, "\r\n");

		n = send(h->socket, header, strlen(header), 0);
		if(n<0)
		{
			syslog(LOG_ERR, "send(res_buf): %m");
		} 
		else if(n < h->res_buflen)
		{
			/* TODO : handle correctly this case */
			syslog(LOG_ERR, "send(res_buf): %d bytes sent (out of %d)",
							n, h->res_buflen);
		}

		if( h->req_command == EHead )
		{
			exif_data_unref(ed);
			goto error;
		}

		n = send(h->socket, ed->data, ed->size, 0);
		if(n<0)
		{
			syslog(LOG_ERR, "send(res_buf): %m");
		} 
		else if(n < h->res_buflen)
		{
			/* TODO : handle correctly this case */
			syslog(LOG_ERR, "send(res_buf): %d bytes sent (out of %d)",
							n, h->res_buflen);
		}
		exif_data_unref(ed);
	}
	error:
	sqlite3_free_table(result);
}

#if 0 //JPEG_RESIZE
void
SendResp_resizedimg(struct upnphttp * h, char * object)
{
	char header[1500];
	char sql_buf[256];
	char **result;
	char date[30];
	time_t curtime = time(NULL);
	int n;
	FILE *imgfile;
	gdImagePtr imsrc = 0, imdst = 0;
	int dstw, dsth, srcw, srch, size;
	char * data;

	memset(header, 0, 1500);

	if( h->reqflags & FLAG_XFERSTREAMING || h->reqflags & FLAG_RANGE )
	{
		syslog(LOG_NOTICE, "You can't specify transferMode as Streaming with a resized image!");
		Send406(h);
		return;
	}

	sprintf(sql_buf, "SELECT o.PATH, d.WIDTH, d.HEIGHT from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID) where OBJECT_ID = '%s'", object);
	sqlite3_get_table(db, sql_buf, &result, 0, 0, 0);
	printf("Serving up resized image for ObjectId: %s [%s]\n", object, result[1]);

	if( access(result[3], F_OK) == 0 )
	{
		strftime(date, 30,"%a, %d %b %Y %H:%M:%S GMT" , gmtime(&curtime));

		imgfile = fopen(result[3], "r");
		imsrc = gdImageCreateFromJpeg(imgfile);
		imdst = gdImageCreateTrueColor(dstw, dsth);
		srcw = atoi(result[4]);
		srch = atoi(result[5]);
		dstw = 640;
		dsth = ((((640<<10)/srcw)*srch)>>10);

		if( !imsrc )
		{
			Send404(h);
			goto error;
		}
		if( dsth > 480 )
		{
			dsth = 480;
			dstw = (((480<<10)/srch) * srcw>>10);
		}
		gdImageCopyResized(imdst, imsrc, 0, 0, 0, 0, dstw, dsth, srcw, srch);
		data = (char *)gdImageJpegPtr(imdst, &size, -1);
		sprintf(header, "%s 200 OK\r\n"
				"Content-Type: image/jpeg\r\n"
				"Content-Length: %d\r\n"
				"Connection: close\r\n"
				"Date: %s\r\n"
				"EXT:\r\n"
			 	"contentFeatures.dlna.org: DLNA.ORG_PN=JPEG_TN\r\n"
				"Server: RAIDiator/4.1, UPnP/1.0, MiniDLNA_TN/1.0\r\n",
				h->HttpVer, size, date);

		if( h->reqflags & FLAG_XFERINTERACTIVE )
		{
			strcat(header, "transferMode.dlna.org: Interactive\r\n");
		}
		else if( h->reqflags & FLAG_XFERBACKGROUND )
		{
			strcat(header, "transferMode.dlna.org: Background\r\n");
		}
		strcat(header, "\r\n");

		n = send(h->socket, header, strlen(header), 0);
		if(n<0)
		{
			syslog(LOG_ERR, "send(res_buf): %m");
		} 
		else if(n < h->res_buflen)
		{
			/* TODO : handle correctly this case */
			syslog(LOG_ERR, "send(res_buf): %d bytes sent (out of %d)",
							n, h->res_buflen);
		}

		if( h->req_command == EHead )
		{
			goto error;
		}

		n = send(h->socket, data, size, 0);
		if(n<0)
		{
			syslog(LOG_ERR, "send(res_buf): %m");
		} 
		else if(n < h->res_buflen)
		{
			/* TODO : handle correctly this case */
			syslog(LOG_ERR, "send(res_buf): %d bytes sent (out of %d)",
							n, h->res_buflen);
		}
		gdFree(data);  
	}
	error:
	gdImageDestroy(imsrc);  
	gdImageDestroy(imdst);  
	sqlite3_free_table(result);
}
#endif

void
SendResp_dlnafile(struct upnphttp * h, char * object)
{
	char header[1500];
	char hdr_buf[512];
	char sql_buf[256];
	char **result;
	int rows;
	char date[30];
	time_t curtime = time(NULL);
	off_t total;
	char *path, *mime, *dlna;
	
	memset(header, 0, 1500);

	sprintf(sql_buf, "SELECT o.PATH, d.MIME, d.DLNA_PN from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID) where OBJECT_ID = '%s'", object);
	sqlite3_get_table(db, sql_buf, &result, &rows, 0, 0);
	if( !rows )
	{
		syslog(LOG_NOTICE, "%s not found, responding ERROR 404", object);
		Send404(h);
		goto error;
	}

	path = result[3];
	mime = result[4];
	dlna = result[5];
	printf("ObjectId: %s [%s]\n", object, path);

	if( h->reqflags & FLAG_XFERSTREAMING )
	{
		if( strncmp(mime, "imag", 4) == 0 )
		{
			syslog(LOG_NOTICE, "Hey, you can't specify transferMode as Streaming with an image!");
			Send406(h);
			goto error;
		}
	}
	if( h->reqflags & FLAG_XFERINTERACTIVE )
	{
		if( h->reqflags & FLAG_REALTIMEINFO )
		{
			syslog(LOG_NOTICE, "Bad realTimeInfo flag with Interactive request!");
			Send400(h);
			goto error;
		}
		if( strncmp(mime, "imag", 4) != 0 )
		{
			syslog(LOG_NOTICE, "Hey, you can't specify transferMode as Interactive without an image!");
			Send406(h);
			goto error;
		}
	}

	strftime(date, 30,"%a, %d %b %Y %H:%M:%S GMT" , gmtime(&curtime));
	off_t offset = h->req_RangeStart;
        int sendfh = open(path, O_RDONLY);
	if( sendfh < 0 ) {
		printf("Error opening %s\n", result[2]);
		goto error;
	}
	off_t size = lseek(sendfh, 0, SEEK_END);
	lseek(sendfh, 0, SEEK_SET);

	if( h->reqflags & FLAG_RANGE )
	{
		if( (h->req_RangeStart > h->req_RangeEnd) || (h->req_RangeStart < 0) )
		{
			syslog(LOG_NOTICE, "Specified range was invalid!");
			Send400(h);
			close(sendfh);
			goto error;
		}
		if( h->req_RangeEnd > size )
		{
			syslog(LOG_NOTICE, "Specified range was outside file boundaries!");
			Send416(h);
			close(sendfh);
			goto error;
		}

		sprintf(hdr_buf, "HTTP/1.1 206 OK\r\n"
				 "Content-Type: %s\r\n", mime);
		strcpy(header, hdr_buf);
		if( h->req_RangeEnd && (h->req_RangeEnd < size) )
		{
			total = h->req_RangeEnd - h->req_RangeStart + 1;
			sprintf(hdr_buf, "Content-Length: %llu\r\n"
					 "Content-Range: bytes %lld-%lld/%llu\r\n",
					 total, h->req_RangeStart, h->req_RangeEnd, size);
		}
		else
		{
			h->req_RangeEnd = size;
			total = size - h->req_RangeStart;
			sprintf(hdr_buf, "Content-Length: %llu\r\n"
					 "Content-Range: bytes %lld-%llu/%llu\r\n",
					 total, h->req_RangeStart, size-1, size);
		}
	}
	else
	{
		h->req_RangeEnd = size;
		total = size;
		sprintf(hdr_buf, "%s 200 OK\r\n"
				 "Content-Type: %s\r\n"
				 "Content-Length: %llu\r\n",
				 "HTTP/1.1", mime, total);
				 //h->HttpVer, mime, total);
	}
	strcat(header, hdr_buf);

	if( h->reqflags & FLAG_XFERSTREAMING )
	{
		strcat(header, "transferMode.dlna.org: Streaming\r\n");
	}
	else if( h->reqflags & FLAG_XFERBACKGROUND )
	{
		if( strncmp(mime, "imag", 4) == 0 )
			strcat(header, "transferMode.dlna.org: Background\r\n");
	}
	else //if( h->reqflags & FLAG_XFERINTERACTIVE )
	{
		if( (strncmp(mime, "vide", 4) == 0) ||
		    (strncmp(mime, "audi", 4) == 0) )
			strcat(header, "transferMode.dlna.org: Streaming\r\n");
		else
			strcat(header, "transferMode.dlna.org: Interactive\r\n");
	}

	sprintf(hdr_buf, "Accept-Ranges: bytes\r\n"
			 "Connection: close\r\n"
			 "Date: %s\r\n"
			 "EXT:\r\n"
			 "contentFeatures.dlna.org: DLNA.ORG_PN=%s\r\n"
			 "Server: RAIDiator/4.1, UPnP/1.0, MiniDLNA/1.0\r\n\r\n",
			 date, dlna);
	strcat(header, hdr_buf);

	int n;
	n = send(h->socket, header, strlen(header), 0);
	if(n<0)
	{
		syslog(LOG_ERR, "send(res_buf): %m");
	} 
	else if(n < h->res_buflen)
	{
		/* TODO : handle correctly this case */
		syslog(LOG_ERR, "send(res_buf): %d bytes sent (out of %d)",
						n, h->res_buflen);
	}

	if( h->req_command == EHead )
	{
		close(sendfh);
	}
	else if( sendfh > 0 )
	{
          while( offset < h->req_RangeEnd ) {
            int ret = sendfile(h->socket, sendfh, &offset, (h->req_RangeEnd - offset + 1));
            if( ret == -1 ) {
                printf("sendfile error :: error no. %d [%s]\n", errno, strerror(errno));
                if( errno == 32 || errno == 9 || errno == 54 || errno == 104 )
                        break;
            }
            else {
                printf("sent %d bytes to %d. offset is now %d.\n", ret, h->socket, (int)offset);
            }
	  }
          close(sendfh);
        }
	error:
	sqlite3_free_table(result);
}
