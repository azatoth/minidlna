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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <syslog.h>
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

#include "metadata.h"
#include "sql.h"

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
GetStatusInfo(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<NewConnectionStatus>Connected</NewConnectionStatus>"
		"<NewLastConnectionError>ERROR_NONE</NewLastConnectionError>"
		"<NewUptime>%ld</NewUptime>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;
	time_t uptime;

	uptime = (time(NULL) - startup_time);
	bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:WANIPConnection:1",
		(long)uptime, action);	
	BuildSendAndCloseSoapResp(h, body, bodylen);
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
		1, action);	
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
			/*"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN;DLNA.ORG_OP=01;DLNA.ORG_CI=1,"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_OP=01,"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED;DLNA.ORG_OP=01,"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_OP=01,"
			"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_OP=01,"*/
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN,"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_OP=01,"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED,"
			"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_OP=01,"
			"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC,"
			"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01,"
			"http-get:*:audio/x-ms-wma:*,"
			"http-get:*:audio/wav:*,"
			"http-get:*:audio/mp4:*,"
			"http-get:*:audio/x-aiff:*,"
			"http-get:*:audio/x-flac:*,"
			"http-get:*:application/ogg:*,"
			"http-get:*:image/jpeg:*,"
			"http-get:*:image/gif:*,"
			"http-get:*:audio/x-mpegurl:*,"
			"http-get:*:video/mpeg:*,"
			"http-get:*:video/x-msvideo:*,"
			"http-get:*:video/avi:*,"
			"http-get:*:video/mpeg2:*,"
			"http-get:*:video/dvd:*,"
			"http-get:*:video/x-ms-wmv:*"
		"</Source>"
		"<Sink></Sink>"
		"</u:%sResponse>";


	char body[1536];
	int bodylen;

	bodylen = snprintf(body, sizeof(body), resp,
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
		"<ConnectionIDs>-1</ConnectionIDs>"
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

static int callback(void *args, int argc, char **argv, char **azColName)
{
	struct Response { char *resp; int returned; int requested; int total; char *filter; } *passed_args = (struct Response *)args;
	char *id = argv[1], *parent = argv[2], *refID = argv[3], *class = argv[4], *size = argv[9], *title = argv[10],
	     *duration = argv[11], *bitrate = argv[12], *sampleFrequency = argv[13], *artist = argv[14], *album = argv[15],
	     *genre = argv[16], *comment = argv[17], *nrAudioChannels = argv[18], *track = argv[19], *date = argv[20],
	     *resolution = argv[21], *tn = argv[22], *creator = argv[23], *dlna_pn = argv[24], *mime = argv[25];
	char dlna_buf[64];
	char str_buf[4096];
	char **result;
	int ret;

	passed_args->total++;

	if( passed_args->requested && (passed_args->returned >= passed_args->requested) )
		return 0;
	passed_args->returned++;

	if( dlna_pn )
		sprintf(dlna_buf, "DLNA.ORG_PN=%s", dlna_pn);
	else
		strcpy(dlna_buf, "*");

	if( strncmp(class, "item", 4) == 0 )
	{
		sprintf(str_buf, "&lt;item id=\"%s\" parentID=\"%s\" restricted=\"1\"", id, parent);
		strcat(passed_args->resp, str_buf);
		if( refID && (!passed_args->filter || strstr(passed_args->filter, "@refID")) ) {
			sprintf(str_buf, " refID=\"%s\"", refID);
			strcat(passed_args->resp, str_buf);
		}
		sprintf(str_buf, "&gt;"
				 "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
				 "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;",
				 title, class);
		strcat(passed_args->resp, str_buf);
		if( comment && (!passed_args->filter || strstr(passed_args->filter, "dc:description")) ) {
			sprintf(str_buf, "&lt;dc:description&gt;%s&lt;/dc:description&gt;", comment);
			strcat(passed_args->resp, str_buf);
		}
		if( creator && (!passed_args->filter || strstr(passed_args->filter, "dc:creator")) ) {
			sprintf(str_buf, "&lt;dc:creator&gt;%s&lt;/dc:creator&gt;", creator);
			strcat(passed_args->resp, str_buf);
		}
		if( date && (!passed_args->filter || strstr(passed_args->filter, "dc:date")) ) {
			sprintf(str_buf, "&lt;dc:date&gt;%s&lt;/dc:date&gt;", date);
			strcat(passed_args->resp, str_buf);
		}
		if( artist && (!passed_args->filter || strstr(passed_args->filter, "upnp:artist")) ) {
			sprintf(str_buf, "&lt;upnp:artist&gt;%s&lt;/upnp:artist&gt;", artist);
			strcat(passed_args->resp, str_buf);
		}
		if( album && (!passed_args->filter || strstr(passed_args->filter, "upnp:album")) ) {
			sprintf(str_buf, "&lt;upnp:album&gt;%s&lt;/upnp:album&gt;", album);
			strcat(passed_args->resp, str_buf);
		}
		if( genre && (!passed_args->filter || strstr(passed_args->filter, "upnp:genre")) ) {
			sprintf(str_buf, "&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;", genre);
			strcat(passed_args->resp, str_buf);
		}
		if( track && atoi(track) && (!passed_args->filter || strstr(passed_args->filter, "upnp:originalTrackNumber")) ) {
			sprintf(str_buf, "&lt;upnp:originalTrackNumber&gt;%s&lt;/upnp:originalTrackNumber&gt;", track);
			strcat(passed_args->resp, str_buf);
		}
		if( !passed_args->filter || strstr(passed_args->filter, "res") ) {
			strcat(passed_args->resp, "&lt;res ");
			if( size && (!passed_args->filter || strstr(passed_args->filter, "res@size")) ) {
				sprintf(str_buf, "size=\"%s\" ", size);
				strcat(passed_args->resp, str_buf);
			}
			if( duration && (!passed_args->filter || strstr(passed_args->filter, "res@duration")) ) {
				sprintf(str_buf, "duration=\"%s\" ", duration);
				strcat(passed_args->resp, str_buf);
			}
			if( bitrate && (!passed_args->filter || strstr(passed_args->filter, "res@bitrate")) ) {
				sprintf(str_buf, "bitrate=\"%s\" ", bitrate);
				strcat(passed_args->resp, str_buf);
			}
			if( sampleFrequency && (!passed_args->filter || strstr(passed_args->filter, "res@sampleFrequency")) ) {
				sprintf(str_buf, "sampleFrequency=\"%s\" ", sampleFrequency);
				strcat(passed_args->resp, str_buf);
			}
			if( nrAudioChannels && (!passed_args->filter || strstr(passed_args->filter, "res@nrAudioChannels")) ) {
				sprintf(str_buf, "nrAudioChannels=\"%s\" ", nrAudioChannels);
				strcat(passed_args->resp, str_buf);
			}
			if( resolution && (!passed_args->filter || strstr(passed_args->filter, "res@resolution")) ) {
				sprintf(str_buf, "resolution=\"%s\" ", resolution);
				strcat(passed_args->resp, str_buf);
			}
			sprintf(str_buf, "protocolInfo=\"http-get:*:%s:%s\"&gt;"
						"http://%s:5555/MediaItems/%s"
					 "&lt;/res&gt;",
					 mime, dlna_buf, lan_addr[0].str, id);
			#if 0 //JPEG_RESIZE
			if( dlna_pn && (strncmp(dlna_pn, "JPEG_LRG", 8) == 0) ) {
				strcat(passed_args->resp, str_buf);
				sprintf(str_buf, "&lt;res "
						 "protocolInfo=\"http-get:*:%s:%s\"&gt;"
							"http://%s:5555/Resized/%s"
						 "&lt;/res&gt;",
						 mime, "DLNA.ORG_PN=JPEG_SM", lan_addr[0].str, id);
			}
			#endif
			if( tn && atoi(tn) && dlna_pn ) {
				strcat(passed_args->resp, str_buf);
				strcat(passed_args->resp, "&lt;res ");
				sprintf(str_buf, "protocolInfo=\"http-get:*:%s:%s\"&gt;"
							"http://%s:5555/Thumbnails/%s"
						 "&lt;/res&gt;",
						 mime, "DLNA.ORG_PN=JPEG_TN", lan_addr[0].str, id);
			}
			strcat(passed_args->resp, str_buf);
		}
		strcpy(str_buf, "&lt;/item&gt;");
	}
	else if( strncmp(class, "container", 9) == 0 )
	{
		sprintf(str_buf, "SELECT count(*) from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID) where PARENT_ID = '%s' order by d.TRACK, d.TITLE, o.NAME;", id);
		ret = sqlite3_get_table(db, str_buf, &result, 0, 0, 0);
		sprintf(str_buf, "&lt;container id=\"%s\" parentID=\"%s\" restricted=\"1\" ", id, parent);
		strcat(passed_args->resp, str_buf);
		if( !passed_args->filter || strstr(passed_args->filter, "@childCount"))
		{
			sprintf(str_buf, "childCount=\"%s\"", result[1]);
			strcat(passed_args->resp, str_buf);
		}
		/* If the client calls for BrowseMetadata on root, we have to include our "upnp:searchClass"'s */
		if( (passed_args->requested == 1) && (strcmp(id, "0") == 0) )
		{
			strcat(passed_args->resp, "&gt;"
						  "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.audioItem&lt;/upnp:searchClass&gt;"
						  "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.imageItem&lt;/upnp:searchClass&gt;"
						  "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.videoItem&lt;/upnp:searchClass");
		}
		sprintf(str_buf, "&gt;"
				 "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
				 "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;"
				 "&lt;/container&gt;",
				 title, class);
		sqlite3_free_table(result);
	}
	strcat(passed_args->resp, str_buf);

	return 0;
}

static void
BrowseContentDirectory(struct upnphttp * h, const char * action)
{
	static const char resp0[] =
			"<u:BrowseResponse "
			"xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
			"<Result>"
			"&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"&gt;\n";
	static const char resp1[] = "&lt;/DIDL-Lite&gt;</Result>";
        static const char resp2[] = "<UpdateID>0</UpdateID></u:BrowseResponse>";

	char *resp = calloc(1, 1048576);
	char str_buf[4096];
	char *zErrMsg = 0;
	char *sql;
	int ret;
	struct Response { char *resp; int returned; int requested; int total; char *filter; } args;

	struct NameValueParserData data;
	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	int RequestedCount = atoi( GetValueFromNameValueList(&data, "RequestedCount") );
	int StartingIndex = atoi( GetValueFromNameValueList(&data, "StartingIndex") );
	char * ObjectId = GetValueFromNameValueList(&data, "ObjectID");
	char * Filter = GetValueFromNameValueList(&data, "Filter");
	char * BrowseFlag = GetValueFromNameValueList(&data, "BrowseFlag");
	char * SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");
	if( !ObjectId )
		ObjectId = GetValueFromNameValueList(&data, "ContainerID");

	memset(str_buf, '\0', sizeof(str_buf));
	memset(&args, 0, sizeof(args));
	strcpy(resp, resp0);

	args.total = StartingIndex;
	args.returned = 0;
	args.requested = RequestedCount;
	args.resp = NULL;
	args.filter = NULL;
	printf("Asked for ObjectID: %s\n", ObjectId);
	printf("Asked for Count: %d\n", RequestedCount);
	printf("Asked for StartingIndex: %d\n", StartingIndex);
	printf("Asked for BrowseFlag: %s\n", BrowseFlag);
	printf("Asked for Filter: %s\n", Filter);
	if( SortCriteria ) printf("Asked for SortCriteria: %s\n", SortCriteria);

	if( !Filter )
	{
		ClearNameValueList(&data);
		SoapError(h, 402, "Invalid Args");
		return;
	}
	if( strlen(Filter) > 1 )
		args.filter = Filter;

	args.resp = resp;
	if( strcmp(BrowseFlag, "BrowseMetadata") == 0 )
	{
		args.requested = 1;
		sql = sqlite3_mprintf("SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID) where OBJECT_ID = '%s';", ObjectId);
		ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	}
	else
	{
		sql = sqlite3_mprintf("SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
				      " where PARENT_ID = '%s' order by d.TRACK, d.TITLE, o.NAME limit %d, -1;",
				      ObjectId, StartingIndex);
		ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	}
	sqlite3_free(sql);
	if( ret != SQLITE_OK ){
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	}
	strcat(resp, resp1);
	sprintf(str_buf, "\n<NumberReturned>%u</NumberReturned>\n<TotalMatches>%u</TotalMatches>\n", args.returned, args.total);
	strcat(resp, str_buf);
	strcat(resp, resp2);
	BuildSendAndCloseSoapResp(h, resp, strlen(resp));
	ClearNameValueList(&data);
	free(resp);
}

static void
SearchContentDirectory(struct upnphttp * h, const char * action)
{
	static const char resp0[] =
			"<u:SearchResponse "
			"xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
			"<Result>"
			"&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"&gt;\n";
	static const char resp1[] = "&lt;/DIDL-Lite&gt;</Result>";
        static const char resp2[] = "<UpdateID>0</UpdateID></u:SearchResponse>";

	char *resp = calloc(1, 1048576);
	char *zErrMsg = 0;
	char sql_buf[4096];
	char str_buf[4096];
	int ret;
	struct Response { char *resp; int returned; int requested; int total; char *filter; } args;

	struct NameValueParserData data;
	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data);
	int RequestedCount = atoi( GetValueFromNameValueList(&data, "RequestedCount") );
	int StartingIndex = atoi( GetValueFromNameValueList(&data, "StartingIndex") );
	char * ContainerID = GetValueFromNameValueList(&data, "ContainerID");
	char * Filter = GetValueFromNameValueList(&data, "Filter");
	char * SearchCriteria = GetValueFromNameValueList(&data, "SearchCriteria");
	char * SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");

	memset(str_buf, '\0', sizeof(str_buf));
	memset(&args, 0, sizeof(args));

	args.total = 0;
	args.returned = 0;
	args.requested = RequestedCount;
	args.resp = NULL;
	args.filter = NULL;
	printf("Asked for ContainerID: %s\n", ContainerID);
	printf("Asked for Count: %d\n", RequestedCount);
	printf("Asked for StartingIndex: %d\n", StartingIndex);
	printf("Asked for SearchCriteria: %s\n", SearchCriteria);
	printf("Asked for Filter: %s\n", Filter);
	if( SortCriteria ) printf("Asked for SortCriteria: %s\n", SortCriteria);

	strcpy(resp, resp0);

	if( !Filter )
	{
		ClearNameValueList(&data);
		SoapError(h, 402, "Invalid Args");
		return;
	}
	if( strlen(Filter) > 1 )
		args.filter = Filter;
	if( strcmp(ContainerID, "0") == 0 )
		*ContainerID = '%';
	if( !SearchCriteria )
	{
		asprintf(&SearchCriteria, "1 = 1");
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
	}
	printf("Translated SearchCriteria: %s\n", SearchCriteria);

	args.resp = resp;
	sprintf(sql_buf, "SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
			 " where OBJECT_ID like '%s$%%' and (%s) order by d.TRACK, d.TITLE, o.NAME limit %d, -1;",
			 ContainerID, SearchCriteria, StartingIndex);
	printf("Search SQL: %s\n", sql_buf);
	ret = sqlite3_exec(db, sql_buf, callback, (void *) &args, &zErrMsg);
	if( ret != SQLITE_OK ){
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	}
	strcat(resp, resp1);
	sprintf(str_buf, "\n<NumberReturned>%u</NumberReturned>\n<TotalMatches>%u</TotalMatches>\n", args.returned, args.total);
	strcat(resp, str_buf);
	strcat(resp, resp2);
	BuildSendAndCloseSoapResp(h, resp, strlen(resp));
	ClearNameValueList(&data);
	free(resp);
}

static void
GetExternalIPAddress(struct upnphttp * h, const char * action)
{
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<NewExternalIPAddress>%s</NewExternalIPAddress>"
		"</u:%sResponse>";

	char body[512];
	int bodylen;
	char ext_ip_addr[INET_ADDRSTRLEN];

#ifndef MULTIPLE_EXTERNAL_IP
	if(use_ext_ip_addr)
	{
		strncpy(ext_ip_addr, use_ext_ip_addr, INET_ADDRSTRLEN);
	}
	else if(getifaddr(ext_if_name, ext_ip_addr, INET_ADDRSTRLEN) < 0)
	{
		syslog(LOG_ERR, "Failed to get ip address for interface %s",
			ext_if_name);
		strncpy(ext_ip_addr, "0.0.0.0", INET_ADDRSTRLEN);
	}
#else
	int i;
	strncpy(ext_ip_addr, "0.0.0.0", INET_ADDRSTRLEN);
	for(i = 0; i<n_lan_addr; i++)
	{
		if( (h->clientaddr.s_addr & lan_addr[i].mask.s_addr)
		   == (lan_addr[i].addr.s_addr & lan_addr[i].mask.s_addr))
		{
			strncpy(ext_ip_addr, lan_addr[i].ext_ip_str, INET_ADDRSTRLEN);
			break;
		}
	}
#endif
	bodylen = snprintf(body, sizeof(body), resp,
	              action, "urn:schemas-upnp-org:service:WANIPConnection:1",
				  ext_ip_addr, action);
	BuildSendAndCloseSoapResp(h, body, bodylen);
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

	/*syslog(LOG_INFO, "QueryStateVariable(%.40s)", var_name); */

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
	/* not usefull */
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
		syslog(LOG_NOTICE, "%s: Unknown: %s", action, var_name?var_name:"");
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
	{ "GetExternalIPAddress", GetExternalIPAddress},
	{ "QueryStateVariable", QueryStateVariable},
	{ "GetStatusInfo", GetStatusInfo},
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
		/*syslog(LOG_DEBUG, "SoapMethod: %.*s", methodlen, p);*/
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

		syslog(LOG_NOTICE, "SoapMethod: Unknown: %.*s", methodlen, p);
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

	syslog(LOG_INFO, "Returning UPnPError %d: %s", errCode, errDesc);
	bodylen = snprintf(body, sizeof(body), resp, errCode, errDesc);
	BuildResp2_upnphttp(h, 500, "Internal Server Error", body, bodylen);
	SendResp_upnphttp(h);
	CloseSocket_upnphttp(h);
}

