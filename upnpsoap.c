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

#include "utils.h"
#include "sql.h"
#include "log.h"

static void
BuildSendAndCloseSoapResp(struct upnphttp * h,
                          const char * body, int bodylen)
{
	static const char beforebody[] =
		"<?xml version=\"1.0\"?>\r\n"
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
}

static void
GetSortCapabilities(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<SortCaps></SortCaps>"
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
		"<ProtocolInfo>"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN,"
		"</ProtocolInfo>"
		"<PeerConnectionManager>0</PeerConnectionManager>"
		"<PeerConnectionID>-1</PeerConnectionID>"
		"<Direction>0</Direction>"
		"<Status>0</Status>"
		"</u:%sResponse>";

	char body[sizeof(resp)+128];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ConnectionManager:1",
		action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
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
	u_int32_t flags = 0;

	if( !filter || (strlen(filter) <= 1) )
		return 0xFFFFFFFF;
	if( strstr(filter, "@childCount") )
		flags |= FILTER_CHILDCOUNT;
	if( strstr(filter, "dc:creator") )
		flags |= FILTER_DC_CREATOR;
	if( strstr(filter, "dc:date") )
		flags |= FILTER_DC_DATE;
	if( strstr(filter, "dc:description") )
		flags |= FILTER_DC_DESCRIPTION;
	if( strstr(filter, "dlna") )
		flags |= FILTER_DLNA_NAMESPACE;
	if( strstr(filter, "@refID") )
		flags |= FILTER_REFID;
	if( strstr(filter, "res") )
		flags |= FILTER_RES;
	if( strstr(filter, "res@bitrate") )
		flags |= FILTER_RES_BITRATE;
	if( strstr(filter, "res@duration") )
		flags |= FILTER_RES_DURATION;
	if( strstr(filter, "res@nrAudioChannels") )
		flags |= FILTER_RES_NRAUDIOCHANNELS;
	if( strstr(filter, "res@resolution") )
		flags |= FILTER_RES_RESOLUTION;
	if( strstr(filter, "res@sampleFrequency") )
		flags |= FILTER_RES_SAMPLEFREQUENCY;
	if( strstr(filter, "res@size") )
		flags |= FILTER_RES_SIZE;
	if( strstr(filter, "upnp:album") )
		flags |= FILTER_UPNP_ALBUM;
	if( strstr(filter, "upnp:albumArtURI") )
		flags |= FILTER_UPNP_ALBUMARTURI;
	if( strstr(filter, "upnp:albumArtURI@dlna:profileID") )
		flags |= FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID;
	if( strstr(filter, "upnp:artist") )
		flags |= FILTER_UPNP_ARTIST;
	if( strstr(filter, "upnp:genre") )
		flags |= FILTER_UPNP_GENRE;
	if( strstr(filter, "upnp:originalTrackNumber") )
		flags |= FILTER_UPNP_ORIGINALTRACKNUMBER;
	if( strstr(filter, "upnp:searchClass") )
		flags |= FILTER_UPNP_SEARCHCLASS;
	
	return flags;
}

static int
callback(void *args, int argc, char **argv, char **azColName)
{
	struct Response *passed_args = (struct Response *)args;
	char *id = argv[1], *parent = argv[2], *refID = argv[3], *class = argv[4], *detailID = argv[5], *size = argv[9], *title = argv[10],
	     *duration = argv[11], *bitrate = argv[12], *sampleFrequency = argv[13], *artist = argv[14], *album = argv[15],
	     *genre = argv[16], *comment = argv[17], *nrAudioChannels = argv[18], *track = argv[19], *date = argv[20], *resolution = argv[21],
	     *tn = argv[22], *creator = argv[23], *dlna_pn = argv[24], *mime = argv[25], *album_art = argv[26], *art_dlna_pn = argv[27];
	char dlna_buf[64];
	char str_buf[512];
	char **result;
	int children, ret = 0;
	static int warned = 0;

	passed_args->total++;

	if( passed_args->requested && (passed_args->returned >= passed_args->requested) )
		return 0;
	/* Make sure we have at least 4KB left of allocated memory to finish the response. */
	if( passed_args->size > 1044480 && !warned )
	{
		DPRINTF(E_ERROR, L_HTTP, "UPnP SOAP response is getting too big! [%d returned]\n", passed_args->returned);
		warned = 1;
		return 0;
	}
	passed_args->returned++;

	if( dlna_pn )
		sprintf(dlna_buf, "DLNA.ORG_PN=%s", dlna_pn);
	else
		strcpy(dlna_buf, "*");

	if( strncmp(class, "item", 4) == 0 )
	{
		if( passed_args->client == EXbox )
		{
			if( strcmp(mime, "video/divx") == 0 )
			{
				mime[6] = 'a';
				mime[7] = 'v';
				mime[8] = 'i';
				mime[9] = '\0';
			}
		}
		ret = sprintf(str_buf, "&lt;item id=\"%s\" parentID=\"%s\" restricted=\"1\"", id, parent);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( refID && (passed_args->filter & FILTER_REFID) ) {
			ret = sprintf(str_buf, " refID=\"%s\"", refID);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		ret = sprintf(str_buf, "&gt;"
		                       "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
		                       "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;",
		                       title, class);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( comment && (passed_args->filter & FILTER_DC_DESCRIPTION) ) {
			ret = sprintf(str_buf, "&lt;dc:description&gt;%s&lt;/dc:description&gt;", comment);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( creator && (passed_args->filter & FILTER_DC_CREATOR) ) {
			ret = sprintf(str_buf, "&lt;dc:creator&gt;%s&lt;/dc:creator&gt;", creator);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( date && (passed_args->filter & FILTER_DC_DATE) ) {
			ret = sprintf(str_buf, "&lt;dc:date&gt;%s&lt;/dc:date&gt;", date);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( artist && (passed_args->filter & FILTER_UPNP_ARTIST) ) {
			ret = sprintf(str_buf, "&lt;upnp:artist&gt;%s&lt;/upnp:artist&gt;", artist);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( album && (passed_args->filter & FILTER_UPNP_ALBUM) ) {
			ret = sprintf(str_buf, "&lt;upnp:album&gt;%s&lt;/upnp:album&gt;", album);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( genre && (passed_args->filter & FILTER_UPNP_GENRE) ) {
			ret = sprintf(str_buf, "&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;", genre);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( track && atoi(track) && (passed_args->filter & FILTER_UPNP_ORIGINALTRACKNUMBER) ) {
			ret = sprintf(str_buf, "&lt;upnp:originalTrackNumber&gt;%s&lt;/upnp:originalTrackNumber&gt;", track);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( album_art && atoi(album_art) && (passed_args->filter & FILTER_UPNP_ALBUMARTURI) ) {
			ret = sprintf(str_buf, "&lt;upnp:albumArtURI ");
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
			if( passed_args->filter & FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID ) {
				ret = sprintf(str_buf, "dlna:profileID=\"%s\" xmlns:dlna=\"urn:schemas-dlnaorg:metadata-1-0/\"", art_dlna_pn);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			ret = sprintf(str_buf, "&gt;http://%s:%d/AlbumArt/%s.jpg&lt;/upnp:albumArtURI&gt;",
					 lan_addr[0].str, runtime_vars.port, album_art);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( passed_args->filter & FILTER_RES ) {
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
			                       "http://%s:%d/MediaItems/%s.dat"
			                       "&lt;/res&gt;",
			                       mime, dlna_buf, lan_addr[0].str, runtime_vars.port, detailID);
			#if 0 //JPEG_RESIZE
			if( dlna_pn && (strncmp(dlna_pn, "JPEG_LRG", 8) == 0) ) {
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
				ret = sprintf(str_buf, "&lt;res "
						 "protocolInfo=\"http-get:*:%s:%s\"&gt;"
							"http://%s:%d/Resized/%s"
						 "&lt;/res&gt;",
						 mime, "DLNA.ORG_PN=JPEG_SM", lan_addr[0].str, runtime_vars.port, id);
			}
			#endif
			if( tn && atoi(tn) && dlna_pn ) {
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
				ret = sprintf(str_buf, "&lt;res protocolInfo=\"http-get:*:%s:%s\"&gt;"
				                       "http://%s:%d/Thumbnails/%s.dat"
				                       "&lt;/res&gt;",
				                       mime, "DLNA.ORG_PN=JPEG_TN", lan_addr[0].str, runtime_vars.port, detailID);
			}
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
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
			sprintf(str_buf, "SELECT count(ID) from OBJECTS where PARENT_ID = '%s';", id);
			ret = sql_get_table(db, str_buf, &result, NULL, NULL);
			if( ret == SQLITE_OK ) {
				children = atoi(result[1]);
				sqlite3_free_table(result);
			}
			else {
				children = 0;
			}
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
		ret = sprintf(str_buf, "&gt;"
		                       "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
		                       "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;",
		                       title, class);
		memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
		passed_args->size += ret;
		if( creator && (passed_args->filter & FILTER_DC_CREATOR) ) {
			ret = sprintf(str_buf, "&lt;dc:creator&gt;%s&lt;/dc:creator&gt;", creator);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( genre && (passed_args->filter & FILTER_UPNP_GENRE) ) {
			ret = sprintf(str_buf, "&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;", genre);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( artist && (passed_args->filter & FILTER_UPNP_ARTIST) ) {
			ret = sprintf(str_buf, "&lt;upnp:artist&gt;%s&lt;/upnp:artist&gt;", artist);
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
		}
		if( album_art && atoi(album_art) && (passed_args->filter & FILTER_UPNP_ALBUMARTURI) ) {
			ret = sprintf(str_buf, "&lt;upnp:albumArtURI ");
			memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
			passed_args->size += ret;
			if( passed_args->filter & FILTER_UPNP_ALBUMARTURI_DLNA_PROFILEID ) {
				ret = sprintf(str_buf, "dlna:profileID=\"%s\" xmlns:dlna=\"urn:schemas-dlnaorg:metadata-1-0/\"", art_dlna_pn);
				memcpy(passed_args->resp+passed_args->size, &str_buf, ret+1);
				passed_args->size += ret;
			}
			ret = sprintf(str_buf, "&gt;http://%s:%d/AlbumArt/%s.jpg&lt;/upnp:albumArtURI&gt;",
					 lan_addr[0].str, runtime_vars.port, album_art);
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
	char *sql;
	int ret;
	struct Response args;
	struct NameValueParserData data;
	*resp = '\0';

	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	int RequestedCount = atoi( GetValueFromNameValueList(&data, "RequestedCount") );
	int StartingIndex = atoi( GetValueFromNameValueList(&data, "StartingIndex") );
	char * ObjectId = GetValueFromNameValueList(&data, "ObjectID");
	char * Filter = GetValueFromNameValueList(&data, "Filter");
	char * BrowseFlag = GetValueFromNameValueList(&data, "BrowseFlag");
	char * SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");
	if( !ObjectId )
		ObjectId = GetValueFromNameValueList(&data, "ContainerID");
	memset(&args, 0, sizeof(args));

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

	args.total = StartingIndex;
	args.returned = 0;
	args.requested = RequestedCount;
	args.client = h->req_client;
	if( h->req_client == EXbox )
	{
		if( strcmp(ObjectId, "16") == 0 )
			ObjectId = strdup("3$16");
		else if( strcmp(ObjectId, "15") == 0 )
			ObjectId = strdup("2$15");
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

	if( strcmp(BrowseFlag, "BrowseMetadata") == 0 )
	{
		args.requested = 1;
		sql = sqlite3_mprintf("SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID) where OBJECT_ID = '%s';", ObjectId);
		ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	}
	else
	{
		sql = sqlite3_mprintf("SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
				      " where PARENT_ID = '%s' order by d.TRACK, d.ARTIST, d.TITLE, o.NAME limit %d, -1;",
				      ObjectId, StartingIndex);
		ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	}
	sqlite3_free(sql);
	if( ret != SQLITE_OK )
	{
		DPRINTF(E_ERROR, L_HTTP, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	}
	ret = snprintf(str_buf, sizeof(str_buf), "&lt;/DIDL-Lite&gt;</Result>\n"
	                                         "<NumberReturned>%u</NumberReturned>\n"
	                                         "<TotalMatches>%u</TotalMatches>\n"
	                                         "<UpdateID>%u</UpdateID>"
	                                         "</u:BrowseResponse>",
	                                         args.returned, args.total, updateID);
	memcpy(resp+args.size, &str_buf, ret+1);
	args.size += ret;
	BuildSendAndCloseSoapResp(h, resp, args.size);
	ClearNameValueList(&data);
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
	char *sql;
	char str_buf[4096];
	int ret;
	struct Response args;
	*resp = '\0';

	struct NameValueParserData data;
	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	int RequestedCount = atoi( GetValueFromNameValueList(&data, "RequestedCount") );
	int StartingIndex = atoi( GetValueFromNameValueList(&data, "StartingIndex") );
	char * ContainerID = GetValueFromNameValueList(&data, "ContainerID");
	char * Filter = GetValueFromNameValueList(&data, "Filter");
	char * SearchCriteria = GetValueFromNameValueList(&data, "SearchCriteria");
	char * SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");
	char * newSearchCriteria = NULL;

	memset(&args, 0, sizeof(args));

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

	args.total = StartingIndex;
	args.returned = 0;
	args.requested = RequestedCount;
	args.client = h->req_client;
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
		else
			ContainerID = strdup(ContainerID);
	}
	DPRINTF(E_DEBUG, L_HTTP, "Browsing ContentDirectory:\n"
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
	if( !SearchCriteria )
	{
		asprintf(&newSearchCriteria, "1 = 1");
		SearchCriteria = newSearchCriteria;
	}
	else
	{
		SearchCriteria = modifyString(SearchCriteria, "&quot;", "\"", 0);
		SearchCriteria = modifyString(SearchCriteria, "&apos;", "'", 0);
		SearchCriteria = modifyString(SearchCriteria, "derivedfrom", "like", 1);
		SearchCriteria = modifyString(SearchCriteria, "contains", "like", 1);
		SearchCriteria = modifyString(SearchCriteria, "dc:title", "d.TITLE", 0);
		SearchCriteria = modifyString(SearchCriteria, "dc:creator", "d.CREATOR", 0);
		SearchCriteria = modifyString(SearchCriteria, "upnp:class", "o.CLASS", 0);
		SearchCriteria = modifyString(SearchCriteria, "upnp:artist", "d.ARTIST", 0);
		SearchCriteria = modifyString(SearchCriteria, "upnp:album", "d.ALBUM", 0);
		SearchCriteria = modifyString(SearchCriteria, "exists true", "is not NULL", 0);
		SearchCriteria = modifyString(SearchCriteria, "exists false", "is NULL", 0);
		SearchCriteria = modifyString(SearchCriteria, "@refID", "REF_ID", 0);
		SearchCriteria = modifyString(SearchCriteria, "object.", "", 0);
		#if 0
		if( strstr(SearchCriteria, "&amp;") )
		{
			newSearchCriteria = modifyString(strdup(SearchCriteria), "&amp;", "&amp;amp;", 0);
			SearchCriteria = newSearchCriteria;
		}
		#endif
	}
	DPRINTF(E_DEBUG, L_HTTP, "Translated SearchCriteria: %s\n", SearchCriteria);

	sql = sqlite3_mprintf("SELECT distinct * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
	                      " where OBJECT_ID glob '%s$*' and (%s) group by DETAIL_ID "
			      "%z"
	                      " order by d.TRACK, d.TITLE, o.NAME limit %d, -1;",
	                      ContainerID, SearchCriteria,
	                      (*ContainerID == '*') ? NULL :
                              sqlite3_mprintf("UNION ALL SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
	                                      " where OBJECT_ID = '%s' and (%s) ", ContainerID, SearchCriteria),
	                      StartingIndex);
	DPRINTF(E_DEBUG, L_HTTP, "Search SQL: %s\n", sql);
	ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	if( ret != SQLITE_OK )
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
	                                         args.returned, args.total, updateID);
	memcpy(resp+args.size, &str_buf, ret+1);
	args.size += ret;
	BuildSendAndCloseSoapResp(h, resp, args.size);
	ClearNameValueList(&data);
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
#if 0
	/* not useful */
	else if(strcmp(var_name, "ConnectionType") == 0)
	{	
		bodylen = snprintf(body, sizeof(body), resp, "IP_Routed");
		BuildSendAndCloseSoapResp(h, body, bodylen);
	}
	else if(strcmp(var_name, "LastConnectionError") == 0)
	{	
		bodylen = snprintf(body, sizeof(body), resp, "ERROR_NONE");
		BuildSendAndCloseSoapResp(h, body, bodylen);
	}
#endif
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

