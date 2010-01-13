/* MiniDLNA project
 * http://minidlna.sourceforge.net/
 * (c) 2008-2009 Justin Maggard
 *
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution 
 *
 * Portions of the code from the MiniUPnP Project
 * (c) Thomas Bernard licensed under BSD revised license
 * detailed in the LICENSE.miniupnpd file provided within
 * the distribution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>

#include "config.h"
#include "upnpglobalvars.h"
#include "upnphttp.h"
#include "upnpsoap.h"
#include "upnpreplyparse.h"
#include "getifaddr.h"

#include "scanner.h"
#include "utils.h"
#include "sql.h"
#include "log.h"

static void
BuildSendAndCloseSoapResp(struct upnphttp * h,
                          const char * body, int bodylen)
{
	static const char beforebody[] =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body>";

	static const char afterbody[] =
		"</s:Body>"
		"</s:Envelope>\r\n";

	BuildHeader_upnphttp(h, 200, "OK",  sizeof(beforebody) - 1
		+ sizeof(afterbody) - 1 + bodylen );

	memcpy(h->res_buf + h->res_buflen, beforebody, sizeof(beforebody) - 1);
	h->res_buflen += sizeof(beforebody) - 1;

	memcpy(h->res_buf + h->res_buflen, body, bodylen);
	h->res_buflen += bodylen;

	memcpy(h->res_buf + h->res_buflen, afterbody, sizeof(afterbody) - 1);
	h->res_buflen += sizeof(afterbody) - 1;

	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

static void
GetSystemUpdateID(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<Id>%d</Id>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		updateID, action);
	BuildSendAndCloseSoapResp(h, body, bodylen);
}

static void
IsAuthorizedValidated(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<Result>%d</Result>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1",
		1, action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
}

static void
GetProtocolInfo(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<Source>"
		RESOURCE_PROTOCOL_INFO_VALUES
		"</Source>"
		"<Sink></Sink>"
		"</u:%sResponse>";

	char * body;
	int bodylen;

	bodylen = asprintf(&body, resp,
		action, "urn:schemas-upnp-org:service:ConnectionManager:1",
		action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
	free(body);
}

static void
GetSortCapabilities(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<SortCaps>"
                  "dc:title,"
                  "dc:date,"
		  "upnp:class,"
                  "upnp:originalTrackNumber"
		"</SortCaps>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
}

static void
GetSearchCapabilities(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<SearchCaps>dc:title,dc:creator,upnp:class,upnp:artist,upnp:album,@refID</SearchCaps>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
}

static void
GetCurrentConnectionIDs(struct upnphttp * h, const char * action)
{
	/* TODO: Use real data. - JM */
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<ConnectionIDs>0</ConnectionIDs>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ConnectionManager:1",
		action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
}

static void
GetCurrentConnectionInfo(struct upnphttp * h, const char * action)
{
	/* TODO: Use real data. - JM */
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<RcsID>-1</RcsID>"
		"<AVTransportID>-1</AVTransportID>"
		"<ProtocolInfo></ProtocolInfo>"
		"<PeerConnectionManager></PeerConnectionManager>"
		"<PeerConnectionID>-1</PeerConnectionID>"
		"<Direction>Output</Direction>"
		"<Status>Unknown</Status>"
		"</u:%sResponse>";

	char body[sizeof(resp)+128];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ConnectionManager:1",
		action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
}

static void
mime_to_ext(const char * mime, char * buf)
{
	switch( *mime )
	{
		/* Audio extensions */
		case 'a':
			if( strcmp(mime+6, "mpeg") == 0 )
				strcpy(buf, "mp3");
			else if( strcmp(mime+6, "mp4") == 0 )
				strcpy(buf, "m4a");
			else if( strcmp(mime+6, "x-ms-wma") == 0 )
				strcpy(buf, "wma");
			else if( strcmp(mime+6, "x-flac") == 0 )
				strcpy(buf, "flac");
			else if( strcmp(mime+6, "flac") == 0 )
				strcpy(buf, "flac");
			else if( strcmp(mime+6, "x-wav") == 0 )
				strcpy(buf, "wav");
			else
				strcpy(buf, "dat");
			break;
		case 'v':
			if( strcmp(mime+6, "avi") == 0 )
				strcpy(buf, "avi");
			else if( strcmp(mime+6, "divx") == 0 )
				strcpy(buf, "avi");
			else if( strcmp(mime+6, "x-msvideo") == 0 )
				strcpy(buf, "avi");
			else if( strcmp(mime+6, "mpeg") == 0 )
				strcpy(buf, "mpg");
			else if( strcmp(mime+6, "mp4") == 0 )
				strcpy(buf, "mp4");
			else if( strcmp(mime+6, "x-ms-wmv") == 0 )
				strcpy(buf, "wmv");
			else if( strcmp(mime+6, "x-matroska") == 0 )
				strcpy(buf, "mkv");
			else if( strcmp(mime+6, "x-mkv") == 0 )
				strcpy(buf, "mkv");
			else if( strcmp(mime+6, "x-flv") == 0 )
				strcpy(buf, "flv");
			else if( strcmp(mime+6, "vnd.dlna.mpeg-tts") == 0 )
				strcpy(buf, "mpg");
			else if( strcmp(mime+6, "x-tivo-mpeg") == 0 )
				strcpy(buf, "TiVo");
			else
				strcpy(buf, "dat");
			break;
		case 'i':
			if( strcmp(mime+6, "jpeg") == 0 )
				strcpy(buf, "jpg");
			else if( strcmp(mime+6, "png") == 0 )
				strcpy(buf, "png");
			else
				strcpy(buf, "dat");
			break;
		default:
			strcpy(buf, "dat");
			break;
	}
}

#define FILTER_CHILDCOUNT                        0x00000001
#define FILTER_DC_CREATOR                        0x00000002
#define FILTER_DC_DATE                           0x00000004
#define FILTER_DC_DESCRIPTION                    0x00000008
#define FILTER_DLNA_NAMESPACE                    0x00000010
#define FILTER_REFID                             0x00000020
#define FILTER_RES                               0x00000040
#define FILTER_RES_BITRATE                       0x00000080
#define FILTER_RES_DURATION                      0x00000100
#define FILTER_RES_NRAUDIOCHANNELS               0x00000200
#define FILTER_RES_RESOLUTION                    0x00000400
#define FILTER_RES_SAMPLEFREQUENCY               0x00000800
#define FILTER_RES_SIZE                          0x00001000
#define FILTER_UPNP_ALBUM                        0x00002000
#define FILTER_UPNP_ALBUMARTURI                  0x00004000
#define FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID   0x00008000
#define FILTER_UPNP_ARTIST                       0x00010000
#define FILTER_UPNP_GENRE                        0x00020000
#define FILTER_UPNP_ORIGINALTRACKNUMBER          0x00040000
#define FILTER_UPNP_SEARCHCLASS                  0x00080000

static u_int32_t
set_filter_flags(char * filter)
{
	char *item, *saveptr = NULL;
	u_int32_t flags = 0;

	if( !filter || (strlen(filter) <= 1) )
		return 0xFFFFFFFF;
	item = strtok_r(filter, ",", &saveptr);
	while( item != NULL )
	{
		if( saveptr )
			*(item-1) = ',';
		if( strcmp(item, "@childCount") == 0 )
		{
			flags |= FILTER_CHILDCOUNT;
		}
		else if( strcmp(item, "dc:creator") == 0 )
		{
			flags |= FILTER_DC_CREATOR;
		}
		else if( strcmp(item, "dc:date") == 0 )
		{
			flags |= FILTER_DC_DATE;
		}
		else if( strcmp(item, "dc:description") == 0 )
		{
			flags |= FILTER_DC_DESCRIPTION;
		}
		else if( strcmp(item, "dlna") == 0 )
		{
			flags |= FILTER_DLNA_NAMESPACE;
		}
		else if( strcmp(item, "@refID") == 0 )
		{
			flags |= FILTER_REFID;
		}
		else if( strcmp(item, "upnp:album") == 0 )
		{
			flags |= FILTER_UPNP_ALBUM;
		}
		else if( strcmp(item, "upnp:albumArtURI") == 0 )
		{
			flags |= FILTER_UPNP_ALBUMARTURI;
		}
		else if( strcmp(item, "upnp:albumArtURI@dlna:profileID") == 0 )
		{
			flags |= FILTER_UPNP_ALBUMARTURI;
			flags |= FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID;
		}
		else if( strcmp(item, "upnp:artist") == 0 )
		{
			flags |= FILTER_UPNP_ARTIST;
		}
		else if( strcmp(item, "upnp:genre") == 0 )
		{
			flags |= FILTER_UPNP_GENRE;
		}
		else if( strcmp(item, "upnp:originalTrackNumber") == 0 )
		{
			flags |= FILTER_UPNP_ORIGINALTRACKNUMBER;
		}
		else if( strcmp(item, "upnp:searchClass") == 0 )
		{
			flags |= FILTER_UPNP_SEARCHCLASS;
		}
		else if( strcmp(item, "res") == 0 )
		{
			flags |= FILTER_RES;
		}
		else if( (strcmp(item, "res@bitrate") == 0) ||
		         (strcmp(item, "@bitrate") == 0) ||
		         ((strcmp(item, "bitrate") == 0) && (flags & FILTER_RES)) )
		{
			flags |= FILTER_RES;
			flags |= FILTER_RES_BITRATE;
		}
		else if( (strcmp(item, "res@duration") == 0) ||
		         (strcmp(item, "@duration") == 0) ||
		         ((strcmp(item, "duration") == 0) && (flags & FILTER_RES)) )
		{
			flags |= FILTER_RES;
			flags |= FILTER_RES_DURATION;
		}
		else if( (strcmp(item, "res@nrAudioChannels") == 0) ||
		         (strcmp(item, "@nrAudioChannels") == 0) ||
		         ((strcmp(item, "nrAudioChannels") == 0) && (flags & FILTER_RES)) )
		{
			flags |= FILTER_RES;
			flags |= FILTER_RES_NRAUDIOCHANNELS;
		}
		else if( (strcmp(item, "res@resolution") == 0) ||
		         (strcmp(item, "@resolution") == 0) ||
		         ((strcmp(item, "resolution") == 0) && (flags & FILTER_RES)) )
		{
			flags |= FILTER_RES;
			flags |= FILTER_RES_RESOLUTION;
		}
		else if( (strcmp(item, "res@sampleFrequency") == 0) ||
		         (strcmp(item, "@sampleFrequency") == 0) ||
		         ((strcmp(item, "sampleFrequency") == 0) && (flags & FILTER_RES)) )
		{
			flags |= FILTER_RES;
			flags |= FILTER_RES_SAMPLEFREQUENCY;
		}
		else if( (strcmp(item, "res@size") == 0) ||
		         (strcmp(item, "@size") == 0) ||
		         (strcmp(item, "size") == 0) )
		{
			flags |= FILTER_RES;
			flags |= FILTER_RES_SIZE;
		}
		item = strtok_r(NULL, ",", &saveptr);
	}

	return flags;
}

char *
parse_sort_criteria(char * sortCriteria, int * error)
{
	char *order = NULL;
	char *item, *saveptr;
	int i, ret, reverse, title_sorted = 0;
	*error = 0;

	if( !sortCriteria )
		return NULL;

	if( (item = strtok_r(sortCriteria, ",", &saveptr)) )
	{
		order = malloc(4096);
		strcpy(order, "order by ");
	}
	for( i=0; item != NULL; i++ )
	{
		reverse=0;
		if( i )
			strcat(order, ", ");
		if( *item == '+' )
		{
			item++;
		}
		else if( *item == '-' )
		{
			reverse = 1;
			item++;
		}
		if( strcasecmp(item, "upnp:class") == 0 )
		{
			strcat(order, "o.CLASS");
		}
		else if( strcasecmp(item, "dc:title") == 0 )
		{
			strcat(order, "d.TITLE");
			title_sorted = 1;
		}
		else if( strcasecmp(item, "dc:date") == 0 )
		{
			strcat(order, "d.DATE");
		}
		else if( strcasecmp(item, "upnp:originalTrackNumber") == 0 )
		{
			strcat(order, "d.DISC, d.TRACK");
		}
		else
		{
			printf("Unhandled SortCriteria [%s]\n", item);
			*error = 1;
			if( i )
			{
				ret = strlen(order);
				order[ret-2] = '\0';
			}
			i--;
			goto unhandled_order;
		}

		if( reverse )
			strcat(order, " DESC");
		unhandled_order:
		item = strtok_r(NULL, ",", &saveptr);
	}
	if( i <= 0 )
	{
		free(order);
		return NULL;
	}
	/* Add a "tiebreaker" sort order */
	if( !title_sorted )
		strcat(order, ", TITLE ASC");

	return order;
}

static void add_resized_res(int srcw, int srch, int reqw, int reqh, char *dlna_pn, char *detailID, struct Response *passed_args)
{
	int ret;
	int dstw = reqw;
	int dsth = reqh;
	char str_buf[256];


	if( passed_args->flags & FLAG_NO_RESIZE )
	{
		return;
	}

	ret = sprintf(str_buf, "&lt;res ");
	memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
	passed_args->size += ret;
	if( passed_args->filter & FILTER_RES_RESOLUTION )
	{
		dstw = reqw;
		dsth = ((((reqw<<10)/srcw)*srch)>>10);
		if( dsth > reqh ) {
			dsth = reqh;
			dstw = (((reqh<<10)/srch) * srcw>>10);
		}
		ret = sprintf(str_buf, "resolution=\"%dx%d\" ", dstw, dsth);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
	}
	ret = sprintf(str_buf, "protocolInfo=\"http-get:*:image/jpeg:DLNA.ORG_PN=%s;DLNA.ORG_CI=1\"&gt;"
	                       "http://%s:%d/Resized/%s.jpg?width=%d,height=%d"
	                       "&lt;/res&gt;",
	                       dlna_pn, lan_addr[0].str, runtime_vars.port,
	                       detailID, dstw, dsth);
	memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
	passed_args->size += ret;
}

#define SELECT_COLUMNS "SELECT o.OBJECT_ID, o.PARENT_ID, o.REF_ID, o.DETAIL_ID, o.CLASS," \
                       " d.SIZE, d.TITLE, d.DURATION, d.BITRATE, d.SAMPLERATE, d.ARTIST," \
                       " d.ALBUM, d.GENRE, d.COMMENT, d.CHANNELS, d.TRACK, d.DATE, d.RESOLUTION," \
                       " d.THUMBNAIL, d.CREATOR, d.DLNA_PN, d.MIME, d.ALBUM_ART, d.DISC "

static int
callback(void *args, int argc, char **argv, char **azColName)
{
	struct Response *passed_args = (struct Response *)args;
	char *id = argv[0], *parent = argv[1], *refID = argv[2], *detailID = argv[3], *class = argv[4], *size = argv[5], *title = argv[6],
	     *duration = argv[7], *bitrate = argv[8], *sampleFrequency = argv[9], *artist = argv[10], *album = argv[11],
	     *genre = argv[12], *comment = argv[13], *nrAudioChannels = argv[14], *track = argv[15], *date = argv[16], *resolution = argv[17],
	     *tn = argv[18], *creator = argv[19], *dlna_pn = argv[20], *mime = argv[21], *album_art = argv[22];
	char dlna_buf[96];
	char ext[5];
	char str_buf[512];
	int children, ret = 0;

	/* Make sure we have at least 4KB left of allocated memory to finish the response. */
	if( passed_args->size > (passed_args->alloced - 4096) )
	{
#if MAX_RESPONSE_SIZE > 0
		if( (passed_args->alloced+1048576) <= MAX_RESPONSE_SIZE )
		{
#endif
			passed_args->resp = realloc(passed_args->resp, (passed_args->alloced+1048576));
			if( passed_args->resp )
			{
				passed_args->alloced += 1048576;
				DPRINTF(E_DEBUG, L_HTTP, "HUGE RESPONSE ALERT: UPnP SOAP response had to be enlarged to %d. [%d results so far]\n", passed_args->alloced, passed_args->returned);
			}
			else
			{
				DPRINTF(E_ERROR, L_HTTP, "UPnP SOAP response was too big, and realloc failed!\n");
				return -1;
			}
#if MAX_RESPONSE_SIZE > 0
		}
		else
		{
			DPRINTF(E_ERROR, L_HTTP, "UPnP SOAP response cut short, to not exceed the max response size [%lld]!\n", (long long int)MAX_RESPONSE_SIZE);
			return -1;
		}
#endif
	}
	passed_args->returned++;

	if( dlna_pn )
		sprintf(dlna_buf, "DLNA.ORG_PN=%s", dlna_pn);
	else if( passed_args->flags & FLAG_DLNA )
		strcpy(dlna_buf, dlna_no_conv);
	else
		strcpy(dlna_buf, "*");

	if( strncmp(class, "item", 4) == 0 )
	{
		/* We may need special handling for certain MIME types */
		if( *mime == 'v' )
		{
			if( passed_args->flags & FLAG_MIME_AVI_DIVX )
			{
				if( strcmp(mime, "video/x-msvideo") == 0 )
				{
					if( creator )
						strcpy(mime+6, "divx");
					else
						strcpy(mime+6, "avi");
				}
			}
			else if( passed_args->flags & FLAG_MIME_AVI_AVI )
			{
				if( strcmp(mime, "video/x-msvideo") == 0 )
				{
					strcpy(mime+6, "avi");
				}
			}
			if( !(passed_args->flags & FLAG_DLNA) )
			{
				if( strcmp(mime+6, "vnd.dlna.mpeg-tts") == 0 )
				{
					strcpy(mime+6, "mpeg");
				}
			}
			/* From what I read, Samsung TV's expect a [wrong] MIME type of x-mkv. */
			if( passed_args->client == ESamsungTV )
			{
				if( strcmp(mime+6, "x-matroska") == 0 )
				{
					strcpy(mime+8, "mkv");
				}
			}
		}
		else if( *mime == 'a' )
		{
			if( strcmp(mime+6, "x-flac") == 0 )
			{
				if( passed_args->flags & FLAG_MIME_FLAC_FLAC )
				{
					strcpy(mime+6, "flac");
				}
			}
		}

		ret = snprintf(str_buf, 512, "&lt;item id=\"%s\" parentID=\"%s\" restricted=\"1\"", id, parent);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( refID && (passed_args->filter & FILTER_REFID) ) {
			ret = sprintf(str_buf, " refID=\"%s\"", refID);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		ret = snprintf(str_buf, 512, "&gt;"
		                             "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
		                             "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;",
		                             title, class);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( comment && (passed_args->filter & FILTER_DC_DESCRIPTION) ) {
			ret = snprintf(str_buf, 512, "&lt;dc:description&gt;%.384s&lt;/dc:description&gt;", comment);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( creator && (passed_args->filter & FILTER_DC_CREATOR) ) {
			ret = snprintf(str_buf, 512, "&lt;dc:creator&gt;%s&lt;/dc:creator&gt;", creator);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( date && (passed_args->filter & FILTER_DC_DATE) ) {
			ret = snprintf(str_buf, 512, "&lt;dc:date&gt;%s&lt;/dc:date&gt;", date);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( artist && (passed_args->filter & FILTER_UPNP_ARTIST) ) {
			ret = snprintf(str_buf, 512, "&lt;upnp:artist&gt;%s&lt;/upnp:artist&gt;", artist);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( album && (passed_args->filter & FILTER_UPNP_ALBUM) ) {
			ret = snprintf(str_buf, 512, "&lt;upnp:album&gt;%s&lt;/upnp:album&gt;", album);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( genre && (passed_args->filter & FILTER_UPNP_GENRE) ) {
			ret = snprintf(str_buf, 512, "&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;", genre);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( strncmp(id, MUSIC_PLIST_ID, strlen(MUSIC_PLIST_ID)) == 0 ) {
			track = strrchr(id, '$')+1;
		}
		if( track && atoi(track) && (passed_args->filter & FILTER_UPNP_ORIGINALTRACKNUMBER) ) {
			ret = sprintf(str_buf, "&lt;upnp:originalTrackNumber&gt;%s&lt;/upnp:originalTrackNumber&gt;", track);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( album_art && atoi(album_art) )
		{
			/* Video and audio album art is handled differently */
			if( *mime == 'v' && (passed_args->filter & FILTER_RES) && (passed_args->client != EXbox) ) {
				ret = sprintf(str_buf, "&lt;res protocolInfo=\"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN\"&gt;"
				                       "http://%s:%d/AlbumArt/%s-%s.jpg"
				                       "&lt;/res&gt;",
				                       lan_addr[0].str, runtime_vars.port, album_art, detailID);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			else if( passed_args->filter & FILTER_UPNP_ALBUMARTURI ) {
				ret = sprintf(str_buf, "&lt;upnp:albumArtURI ");
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
				if( passed_args->filter & FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID ) {
					ret = sprintf(str_buf, "dlna:profileID=\"%s\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"", "JPEG_TN");
					memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
					passed_args->size += ret;
				}
				ret = sprintf(str_buf, "&gt;http://%s:%d/AlbumArt/%s-%s.jpg&lt;/upnp:albumArtURI&gt;",
						 lan_addr[0].str, runtime_vars.port, album_art, detailID);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
		}
		if( passed_args->filter & FILTER_RES ) {
			mime_to_ext(mime, ext);
			if( (passed_args->client == EFreeBox) && tn && atoi(tn) ) {
				ret = sprintf(str_buf, "&lt;res protocolInfo=\"http-get:*:%s:%s\"&gt;"
				                       "http://%s:%d/Thumbnails/%s.jpg"
				                       "&lt;/res&gt;",
				                       mime, "DLNA.ORG_PN=JPEG_TN", lan_addr[0].str, runtime_vars.port, detailID);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			ret = sprintf(str_buf, "&lt;res ");
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
			if( size && (passed_args->filter & FILTER_RES_SIZE) ) {
				ret = sprintf(str_buf, "size=\"%s\" ", size);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			if( duration && (passed_args->filter & FILTER_RES_DURATION) ) {
				ret = sprintf(str_buf, "duration=\"%s\" ", duration);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			if( bitrate && (passed_args->filter & FILTER_RES_BITRATE) ) {
				if( passed_args->client == EXbox )
					ret = sprintf(str_buf, "bitrate=\"%d\" ", atoi(bitrate)/1024);
				else
					ret = sprintf(str_buf, "bitrate=\"%s\" ", bitrate);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			if( sampleFrequency && (passed_args->filter & FILTER_RES_SAMPLEFREQUENCY) ) {
				ret = sprintf(str_buf, "sampleFrequency=\"%s\" ", sampleFrequency);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			if( nrAudioChannels && (passed_args->filter & FILTER_RES_NRAUDIOCHANNELS) ) {
				ret = sprintf(str_buf, "nrAudioChannels=\"%s\" ", nrAudioChannels);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			if( resolution && (passed_args->filter & FILTER_RES_RESOLUTION) ) {
				ret = sprintf(str_buf, "resolution=\"%s\" ", resolution);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			ret = sprintf(str_buf, "protocolInfo=\"http-get:*:%s:%s\"&gt;"
			                       "http://%s:%d/MediaItems/%s.%s"
			                       "&lt;/res&gt;",
			                       mime, dlna_buf, lan_addr[0].str, runtime_vars.port, detailID, ext);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
			if( (*mime == 'i') && (passed_args->client != EFreeBox) ) {
#if 1 //JPEG_RESIZE
				int srcw = atoi(strsep(&resolution, "x"));
				int srch = atoi(resolution);
				if( !dlna_pn ) {
					add_resized_res(srcw, srch, 4096, 4096, "JPEG_LRG", detailID, passed_args);
				}
				if( !dlna_pn || !strncmp(dlna_pn, "JPEG_L", 6) || !strncmp(dlna_pn, "JPEG_M", 6) ) {
					add_resized_res(srcw, srch, 640, 480, "JPEG_SM", detailID, passed_args);
				}
#endif
				if( tn && atoi(tn) ) {
					ret = sprintf(str_buf, "&lt;res protocolInfo=\"http-get:*:%s:%s\"&gt;"
					                       "http://%s:%d/Thumbnails/%s.jpg"
					                       "&lt;/res&gt;",
					                       mime, "DLNA.ORG_PN=JPEG_TN", lan_addr[0].str, runtime_vars.port, detailID);
					memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
					passed_args->size += ret;
				}
			}
		}
		ret = sprintf(str_buf, "&lt;/item&gt;");
	}
	else if( strncmp(class, "container", 9) == 0 )
	{
		ret = sprintf(str_buf, "&lt;container id=\"%s\" parentID=\"%s\" restricted=\"1\" ", id, parent);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( passed_args->filter & FILTER_CHILDCOUNT )
		{
			ret = sql_get_int_field(db, "SELECT count(*) from OBJECTS where PARENT_ID = '%s';", id);
			children = (ret > 0) ? ret : 0;
			ret = sprintf(str_buf, "childCount=\"%d\"", children);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		/* If the client calls for BrowseMetadata on root, we have to include our "upnp:searchClass"'s, unless they're filtered out */
		if( (passed_args->requested == 1) && (strcmp(id, "0") == 0) )
		{
			if( passed_args->filter & FILTER_UPNP_SEARCHCLASS )
			{
				ret = sprintf(str_buf, "&gt;"
				                       "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.audioItem&lt;/upnp:searchClass&gt;"
				                       "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.imageItem&lt;/upnp:searchClass&gt;"
				                       "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.videoItem&lt;/upnp:searchClass");
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
		}
		ret = snprintf(str_buf, 512, "&gt;"
		                             "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
		                             "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;",
		                             title, class);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( creator && (passed_args->filter & FILTER_DC_CREATOR) ) {
			ret = snprintf(str_buf, 512, "&lt;dc:creator&gt;%s&lt;/dc:creator&gt;", creator);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( genre && (passed_args->filter & FILTER_UPNP_GENRE) ) {
			ret = snprintf(str_buf, 512, "&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;", genre);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( artist && (passed_args->filter & FILTER_UPNP_ARTIST) ) {
			ret = snprintf(str_buf, 512, "&lt;upnp:artist&gt;%s&lt;/upnp:artist&gt;", artist);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( album_art && atoi(album_art) && (passed_args->filter & FILTER_UPNP_ALBUMARTURI) ) {
			ret = sprintf(str_buf, "&lt;upnp:albumArtURI ");
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
			if( passed_args->filter & FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID ) {
				ret = sprintf(str_buf, "dlna:profileID=\"%s\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"", "JPEG_TN");
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			ret = sprintf(str_buf, "&gt;http://%s:%d/AlbumArt/%s-%s.jpg&lt;/upnp:albumArtURI&gt;",
					 lan_addr[0].str, runtime_vars.port, album_art, detailID);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		ret = sprintf(str_buf, "&lt;/container&gt;");
	}
	memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
	passed_args->size += ret;

	return 0;
}

static void
BrowseContentDirectory(struct upnphttp * h, const char * action)
{
	static const char resp0[] =
			"<u:BrowseResponse "
			"xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
			"<Result>"
			"&lt;DIDL-Lite"
			CONTENT_DIRECTORY_SCHEMAS;

	char *resp = malloc(1048576);
	char str_buf[512];
	char *zErrMsg = 0;
	char *sql, *ptr;
	int ret;
	struct Response args;
	int totalMatches;
	struct NameValueParserData data;
	*resp = '\0';

	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	char * ObjectId = GetValueFromNameValueList(&data, "ObjectID");
	char * Filter = GetValueFromNameValueList(&data, "Filter");
	char * BrowseFlag = GetValueFromNameValueList(&data, "BrowseFlag");
	char * SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");
	char * orderBy = NULL;
	int RequestedCount = 0;
	int StartingIndex = 0;
	if( (ptr = GetValueFromNameValueList(&data, "RequestedCount")) )
		RequestedCount = atoi(ptr);
	if( !RequestedCount )
		RequestedCount = -1;
	if( (ptr = GetValueFromNameValueList(&data, "StartingIndex")) )
		StartingIndex = atoi(ptr);
	if( !BrowseFlag || (strcmp(BrowseFlag, "BrowseDirectChildren") && strcmp(BrowseFlag, "BrowseMetadata")) )
	{
		SoapError(h, 402, "Invalid Args");
		if( h->req_client == EXbox )
			ObjectId = malloc(1);
		goto browse_error;
	}
	if( !ObjectId )
	{
		if( !(ObjectId = GetValueFromNameValueList(&data, "ContainerID")) )
		{
			SoapError(h, 701, "No such object error");
			if( h->req_client == EXbox )
				ObjectId = malloc(1);
			goto browse_error;
		}
	}
	memset(&args, 0, sizeof(args));

	args.alloced = 1048576;
	args.resp = resp;
	args.size = sprintf(resp, "%s", resp0);
	/* See if we need to include DLNA namespace reference */
	args.filter = set_filter_flags(Filter);
	if( args.filter & FILTER_DLNA_NAMESPACE )
	{
		ret = sprintf(str_buf, DLNA_NAMESPACE);
		memcpy(resp+args.size, &str_buf, ret+1);
		args.size += ret;
	}
	ret = sprintf(str_buf, "&gt;\n");
	memcpy(resp+args.size, &str_buf, ret+1);
	args.size += ret;

	args.returned = 0;
	args.requested = RequestedCount;
	args.client = h->req_client;
	args.flags = h->reqflags;
	if( h->req_client == EXbox )
	{
		if( strcmp(ObjectId, "16") == 0 )
			ObjectId = strdup(IMAGE_DIR_ID);
		else if( strcmp(ObjectId, "15") == 0 )
			ObjectId = strdup(VIDEO_DIR_ID);
		else
			ObjectId = strdup(ObjectId);
	}
	DPRINTF(E_DEBUG, L_HTTP, "Browsing ContentDirectory:\n"
	                         " * ObjectID: %s\n"
	                         " * Count: %d\n"
	                         " * StartingIndex: %d\n"
	                         " * BrowseFlag: %s\n"
	                         " * Filter: %s\n"
	                         " * SortCriteria: %s\n",
				ObjectId, RequestedCount, StartingIndex,
	                        BrowseFlag, Filter, SortCriteria);

	if( strcmp(BrowseFlag+6, "Metadata") == 0 )
	{
		args.requested = 1;
		sql = sqlite3_mprintf( SELECT_COLUMNS
		                      "from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
		                      " where OBJECT_ID = '%s';"
		                      , ObjectId);
		ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
		totalMatches = args.returned;
	}
	else
	{
		ret = sql_get_int_field(db, "SELECT count(*) from OBJECTS where PARENT_ID = '%s'", ObjectId);
		totalMatches = (ret > 0) ? ret : 0;
		ret = 0;
		if( SortCriteria )
		{
#ifdef __sparc__ /* Sorting takes too long on slow processors with very large containers */
			if( totalMatches < 10000 )
#endif
			orderBy = parse_sort_criteria(SortCriteria, &ret);
		}
		else
		{
			if( strncmp(ObjectId, MUSIC_PLIST_ID, strlen(MUSIC_PLIST_ID)) == 0 )
			{
				if( strcmp(ObjectId, MUSIC_PLIST_ID) == 0 )
					asprintf(&orderBy, "order by d.TITLE");
				else
					asprintf(&orderBy, "order by length(OBJECT_ID), OBJECT_ID");
			}
		}
		/* If it's a DLNA client, return an error for bad sort criteria */
		if( (args.flags & FLAG_DLNA) && ret )
		{
			SoapError(h, 709, "Unsupported or invalid sort criteria");
			goto browse_error;
		}

		sql = sqlite3_mprintf( SELECT_COLUMNS
		                      "from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
				      " where PARENT_ID = '%s' %s limit %d, %d;",
				      ObjectId, orderBy, StartingIndex, RequestedCount);
		DPRINTF(E_DEBUG, L_HTTP, "Browse SQL: %s\n", sql);
		ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	}
	sqlite3_free(sql);
	if( (ret != SQLITE_OK) && (zErrMsg != NULL) )
	{
		DPRINTF(E_WARN, L_HTTP, "SQL error: %s\nBAD SQL: %s\n", zErrMsg, sql);
		sqlite3_free(zErrMsg);
	}
	/* Does the object even exist? */
	if( !totalMatches )
	{
		ret = sql_get_int_field(db, "SELECT count(*) from OBJECTS where OBJECT_ID = '%s'", ObjectId);
		if( ret <= 0 )
		{
			SoapError(h, 701, "No such object error");
			goto browse_error;
		}
	}
	ret = snprintf(str_buf, sizeof(str_buf), "&lt;/DIDL-Lite&gt;</Result>\n"
	                                         "<NumberReturned>%u</NumberReturned>\n"
	                                         "<TotalMatches>%u</TotalMatches>\n"
	                                         "<UpdateID>%u</UpdateID>"
	                                         "</u:BrowseResponse>",
	                                         args.returned, totalMatches, updateID);
	memcpy(resp+args.size, &str_buf, ret+1);
	args.size += ret;
	BuildSendAndCloseSoapResp(h, resp, args.size);
browse_error:
	ClearNameValueList(&data);
	if( orderBy )
		free(orderBy);
	free(resp);
	if( h->req_client == EXbox )
	{
		free(ObjectId);
	}
}

static void
SearchContentDirectory(struct upnphttp * h, const char * action)
{
	static const char resp0[] =
			"<u:SearchResponse "
			"xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
			"<Result>"
			"&lt;DIDL-Lite"
			CONTENT_DIRECTORY_SCHEMAS;

	char *resp = malloc(1048576);
	char *zErrMsg = 0;
	char *sql, *ptr;
	char **result;
	char str_buf[4096];
	int ret;
	struct Response args;
	int totalMatches = 0;
	*resp = '\0';

	struct NameValueParserData data;
	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	char * ContainerID = GetValueFromNameValueList(&data, "ContainerID");
	char * Filter = GetValueFromNameValueList(&data, "Filter");
	char * SearchCriteria = GetValueFromNameValueList(&data, "SearchCriteria");
	char * SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");
	char * newSearchCriteria = NULL;
	char * orderBy = NULL;
	char groupBy[] = "group by DETAIL_ID";
	int RequestedCount = 0;
	int StartingIndex = 0;
	if( (ptr = GetValueFromNameValueList(&data, "RequestedCount")) )
		RequestedCount = atoi(ptr);
	if( !RequestedCount )
		RequestedCount = -1;
	if( (ptr = GetValueFromNameValueList(&data, "StartingIndex")) )
		StartingIndex = atoi(ptr);
	if( !ContainerID )
	{
		SoapError(h, 701, "No such object error");
		if( h->req_client == EXbox )
			ContainerID = malloc(1);
		goto search_error;
	}
	memset(&args, 0, sizeof(args));

	args.alloced = 1048576;
	args.resp = resp;
	args.size = sprintf(resp, "%s", resp0);
	/* See if we need to include DLNA namespace reference */
	args.filter = set_filter_flags(Filter);
	if( args.filter & FILTER_DLNA_NAMESPACE )
	{
		ret = sprintf(str_buf, DLNA_NAMESPACE);
		memcpy(resp+args.size, &str_buf, ret+1);
		args.size += ret;
	}
	ret = sprintf(str_buf, "&gt;\n");
	memcpy(resp+args.size, &str_buf, ret+1);
	args.size += ret;

	args.returned = 0;
	args.requested = RequestedCount;
	args.client = h->req_client;
	args.flags = h->reqflags;
	if( h->req_client == EXbox )
	{
		if( strcmp(ContainerID, "4") == 0 )
			ContainerID = strdup("1$4");
		else if( strcmp(ContainerID, "5") == 0 )
			ContainerID = strdup("1$5");
		else if( strcmp(ContainerID, "6") == 0 )
			ContainerID = strdup("1$6");
		else if( strcmp(ContainerID, "7") == 0 )
			ContainerID = strdup("1$7");
		else if( strcmp(ContainerID, "F") == 0 )
			ContainerID = strdup(MUSIC_PLIST_ID);
		else
			ContainerID = strdup(ContainerID);
		#if 0 // Looks like the 360 already does this
		/* Sort by track number for some containers */
		if( orderBy &&
		    ((strncmp(ContainerID, "1$5", 3) == 0) ||
		     (strncmp(ContainerID, "1$6", 3) == 0) ||
		     (strncmp(ContainerID, "1$7", 3) == 0)) )
		{
			DPRINTF(E_DEBUG, L_HTTP, "Old sort order: %s\n", orderBy);
			sprintf(str_buf, "d.TRACK, ");
			memmove(orderBy+18, orderBy+9, strlen(orderBy)+1);
			memmove(orderBy+9, &str_buf, 9);
			DPRINTF(E_DEBUG, L_HTTP, "New sort order: %s\n", orderBy);
		}
		#endif
	}
	DPRINTF(E_DEBUG, L_HTTP, "Searching ContentDirectory:\n"
	                         " * ObjectID: %s\n"
	                         " * Count: %d\n"
	                         " * StartingIndex: %d\n"
	                         " * SearchCriteria: %s\n"
	                         " * Filter: %s\n"
	                         " * SortCriteria: %s\n",
				ContainerID, RequestedCount, StartingIndex,
	                        SearchCriteria, Filter, SortCriteria);

	if( strcmp(ContainerID, "0") == 0 )
		*ContainerID = '*';
	else if( strcmp(ContainerID, "1$4") == 0 )
		groupBy[0] = '\0';
	if( !SearchCriteria )
	{
		asprintf(&newSearchCriteria, "1 = 1");
		SearchCriteria = newSearchCriteria;
	}
	else
	{
		SearchCriteria = modifyString(SearchCriteria, "&quot;", "\"", 0);
		SearchCriteria = modifyString(SearchCriteria, "&apos;", "'", 0);
		SearchCriteria = modifyString(SearchCriteria, "object.", "", 0);
		SearchCriteria = modifyString(SearchCriteria, "derivedfrom", "like", 1);
		SearchCriteria = modifyString(SearchCriteria, "contains", "like", 2);
		SearchCriteria = modifyString(SearchCriteria, "dc:title", "d.TITLE", 0);
		SearchCriteria = modifyString(SearchCriteria, "dc:creator", "d.CREATOR", 0);
		SearchCriteria = modifyString(SearchCriteria, "upnp:class", "o.CLASS", 0);
		SearchCriteria = modifyString(SearchCriteria, "upnp:artist", "d.ARTIST", 0);
		SearchCriteria = modifyString(SearchCriteria, "upnp:album", "d.ALBUM", 0);
		SearchCriteria = modifyString(SearchCriteria, "exists true", "is not NULL", 0);
		SearchCriteria = modifyString(SearchCriteria, "exists false", "is NULL", 0);
		SearchCriteria = modifyString(SearchCriteria, "@refID", "REF_ID", 0);
		if( strstr(SearchCriteria, "@id") )
		{
			newSearchCriteria = modifyString(strdup(SearchCriteria), "@id", "OBJECT_ID", 0);
			SearchCriteria = newSearchCriteria;
		}
		if( strstr(SearchCriteria, "res is ") )
		{
			if( newSearchCriteria )
				newSearchCriteria = modifyString(newSearchCriteria, "res is ", "MIME is ", 0);
			else
				newSearchCriteria = modifyString(strdup(SearchCriteria), "res is ", "MIME is ", 0);
			SearchCriteria = newSearchCriteria;
		}
		#if 0 // Does 360 need this?
		if( strstr(SearchCriteria, "&amp;") )
		{
			if( newSearchCriteria )
				newSearchCriteria = modifyString(newSearchCriteria, "&amp;", "&amp;amp;", 0);
			else
				newSearchCriteria = modifyString(strdup(SearchCriteria), "&amp;", "&amp;amp;", 0);
			SearchCriteria = newSearchCriteria;
		}
		#endif
	}
	DPRINTF(E_DEBUG, L_HTTP, "Translated SearchCriteria: %s\n", SearchCriteria);

	sprintf(str_buf, "SELECT (select count(distinct DETAIL_ID) from OBJECTS o left join DETAILS d on (o.DETAIL_ID = d.ID)"
	                 " where (OBJECT_ID glob '%s$*') and (%s))"
	                 " + "
	                 "(select count(*) from OBJECTS o left join DETAILS d on (o.DETAIL_ID = d.ID)"
	                 " where (OBJECT_ID = '%s') and (%s))",
	                 ContainerID, SearchCriteria, ContainerID, SearchCriteria);
	//DEBUG DPRINTF(E_DEBUG, L_HTTP, "Count SQL: %s\n", sql);
	ret = sql_get_table(db, str_buf, &result, NULL, NULL);
	if( ret == SQLITE_OK )
	{
		totalMatches = atoi(result[1]);
		sqlite3_free_table(result);
	}
	else
	{
		/* Must be invalid SQL, so most likely bad or unhandled search criteria. */
		SoapError(h, 708, "Unsupported or invalid search criteria");
		goto search_error;
	}
	/* Does the object even exist? */
	if( !totalMatches )
	{
		ret = sql_get_int_field(db, "SELECT count(*) from OBJECTS where OBJECT_ID = '%q'",
		                        !strcmp(ContainerID, "*")?"0":ContainerID);
		if( ret <= 0 )
		{
			SoapError(h, 710, "No such container");
			goto search_error;
		}
	}
#ifdef __sparc__ /* Sorting takes too long on slow processors with very large containers */
	ret = 0;
	if( totalMatches < 10000 )
#endif
		orderBy = parse_sort_criteria(SortCriteria, &ret);
	/* If it's a DLNA client, return an error for bad sort criteria */
	if( (args.flags & FLAG_DLNA) && ret )
	{
		SoapError(h, 709, "Unsupported or invalid sort criteria");
		goto search_error;
	}

	sql = sqlite3_mprintf( SELECT_COLUMNS
	                      "from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
	                      " where OBJECT_ID glob '%s$*' and (%s) %s "
	                      "%z %s"
	                      " limit %d, %d",
	                      ContainerID, SearchCriteria, groupBy,
	                      (*ContainerID == '*') ? NULL :
                              sqlite3_mprintf("UNION ALL " SELECT_COLUMNS
	                                      "from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
	                                      " where OBJECT_ID = '%s' and (%s) ", ContainerID, SearchCriteria),
	                      orderBy, StartingIndex, RequestedCount);
	DPRINTF(E_DEBUG, L_HTTP, "Search SQL: %s\n", sql);
	ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	if( (ret != SQLITE_OK) && (zErrMsg != NULL) )
	{
		DPRINTF(E_WARN, L_HTTP, "SQL error: %s\nBAD SQL: %s\n", zErrMsg, sql);
		sqlite3_free(zErrMsg);
	}
	sqlite3_free(sql);
	strcat(resp, str_buf);
	ret = snprintf(str_buf, sizeof(str_buf), "&lt;/DIDL-Lite&gt;</Result>\n"
	                                         "<NumberReturned>%u</NumberReturned>\n"
	                                         "<TotalMatches>%u</TotalMatches>\n"
	                                         "<UpdateID>%u</UpdateID>"
	                                         "</u:SearchResponse>",
	                                         args.returned, totalMatches, updateID);
	memcpy(resp+args.size, &str_buf, ret+1);
	args.size += ret;
	BuildSendAndCloseSoapResp(h, resp, args.size);
search_error:
	ClearNameValueList(&data);
	if( orderBy )
		free(orderBy);
	if( newSearchCriteria )
		free(newSearchCriteria);
	free(resp);
	if( h->req_client == EXbox )
	{
		free(ContainerID);
	}
}

/*
If a control point calls QueryStateVariable on a state variable that is not
buffered in memory within (or otherwise available from) the service,
the service must return a SOAP fault with an errorCode of 404 Invalid Var.

QueryStateVariable remains useful as a limited test tool but may not be
part of some future versions of UPnP.
*/
static void
QueryStateVariable(struct upnphttp * h, const char * action)
{
	static const char resp[] =
        "<u:%sResponse "
        "xmlns:u=\"%s\">"
		"<return>%s</return>"
        "</u:%sResponse>";

	char body[512];
	int bodylen;
	struct NameValueParserData data;
	const char * var_name;

	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	/*var_name = GetValueFromNameValueList(&data, "QueryStateVariable"); */
	/*var_name = GetValueFromNameValueListIgnoreNS(&data, "varName");*/
	var_name = GetValueFromNameValueList(&data, "varName");

	DPRINTF(E_INFO, L_HTTP, "QueryStateVariable(%.40s)\n", var_name);

	if(!var_name)
	{
		SoapError(h, 402, "Invalid Args");
	}
	else if(strcmp(var_name, "ConnectionStatus") == 0)
	{	
		bodylen = snprintf(body, sizeof(body), resp,
                           action, "urn:schemas-upnp-org:control-1-0",
		                   "Connected", action);
		BuildSendAndCloseSoapResp(h, body, bodylen);
	}
	else
	{
		DPRINTF(E_WARN, L_HTTP, "%s: Unknown: %s\n", action, var_name?var_name:"");
		SoapError(h, 404, "Invalid Var");
	}

	ClearNameValueList(&data);	
}

static const struct 
{
	const char * methodName; 
	void (*methodImpl)(struct upnphttp *, const char *);
}
soapMethods[] =
{
	{ "QueryStateVariable", QueryStateVariable},
	{ "Browse", BrowseContentDirectory},
	{ "Search", SearchContentDirectory},
	{ "GetSearchCapabilities", GetSearchCapabilities},
	{ "GetSortCapabilities", GetSortCapabilities},
	{ "GetSystemUpdateID", GetSystemUpdateID},
	{ "GetProtocolInfo", GetProtocolInfo},
	{ "GetCurrentConnectionIDs", GetCurrentConnectionIDs},
	{ "GetCurrentConnectionInfo", GetCurrentConnectionInfo},
	{ "IsAuthorized", IsAuthorizedValidated},
	{ "IsValidated", IsAuthorizedValidated},
	{ 0, 0 }
};

void
ExecuteSoapAction(struct upnphttp * h, const char * action, int n)
{
	char * p;
	char * p2;
	int i, len, methodlen;

	i = 0;
	p = strchr(action, '#');

	if(p)
	{
		p++;
		p2 = strchr(p, '"');
		if(p2)
			methodlen = p2 - p;
		else
			methodlen = n - (p - action);
		DPRINTF(E_DEBUG, L_HTTP, "SoapMethod: %.*s\n", methodlen, p);
		while(soapMethods[i].methodName)
		{
			len = strlen(soapMethods[i].methodName);
			if(strncmp(p, soapMethods[i].methodName, len) == 0)
			{
				soapMethods[i].methodImpl(h, soapMethods[i].methodName);
				return;
			}
			i++;
		}

		DPRINTF(E_WARN, L_HTTP, "SoapMethod: Unknown: %.*s\n", methodlen, p);
	}

	SoapError(h, 401, "Invalid Action");
}

/* Standard Errors:
 *
 * errorCode errorDescription Description
 * --------	---------------- -----------
 * 401 		Invalid Action 	No action by that name at this service.
 * 402 		Invalid Args 	Could be any of the following: not enough in args,
 * 							too many in args, no in arg by that name, 
 * 							one or more in args are of the wrong data type.
 * 403 		Out of Sync 	Out of synchronization.
 * 501 		Action Failed 	May be returned in current state of service
 * 							prevents invoking that action.
 * 600-699 	TBD 			Common action errors. Defined by UPnP Forum
 * 							Technical Committee.
 * 700-799 	TBD 			Action-specific errors for standard actions.
 * 							Defined by UPnP Forum working committee.
 * 800-899 	TBD 			Action-specific errors for non-standard actions. 
 * 							Defined by UPnP vendor.
*/
void
SoapError(struct upnphttp * h, int errCode, const char * errDesc)
{
	static const char resp[] = 
		"<s:Envelope "
		"xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body>"
		"<s:Fault>"
		"<faultcode>s:Client</faultcode>"
		"<faultstring>UPnPError</faultstring>"
		"<detail>"
		"<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
		"<errorCode>%d</errorCode>"
		"<errorDescription>%s</errorDescription>"
		"</UPnPError>"
		"</detail>"
		"</s:Fault>"
		"</s:Body>"
		"</s:Envelope>";

	char body[2048];
	int bodylen;

	DPRINTF(E_WARN, L_HTTP, "Returning UPnPError %d: %s\n", errCode, errDesc);
	bodylen = snprintf(body, sizeof(body), resp, errCode, errDesc);
	BuildResp2_upnphttp(h, 500, "Internal Server Error", body, bodylen);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

