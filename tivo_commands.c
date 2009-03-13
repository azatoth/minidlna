#include "config.h"
#ifdef TIVO_SUPPORT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tivo_utils.h"
#include "upnpglobalvars.h"
#include "upnphttp.h"
#include "upnpsoap.h"
#include "utils.h"
#include "sql.h"
#include "log.h"

void
SendRootContainer(struct upnphttp * h)
{
	char * resp;
	int len;

	len = asprintf(&resp, "<?xml version='1.0' encoding='UTF-8' ?>\n"
			"<TiVoContainer>"
			 "<Details>"
			  "<ContentType>x-container/tivo-server</ContentType>"
			  "<SourceFormat>x-container/folder</SourceFormat>"
			  "<TotalDuration>0</TotalDuration>"
			  "<TotalItems>2</TotalItems>"
			  "<Title>%s</Title>"
			 "</Details>"
			 "<ItemStart>0</ItemStart>"
			 "<ItemCount>2</ItemCount>"
			 "<Item>"
			  "<Details>"
			   "<ContentType>x-container/tivo-photos</ContentType>"
			   "<SourceFormat>x-container/folder</SourceFormat>"
			   "<Title>Pictures on %s</Title>"
			  "</Details>"
			  "<Links>"
			   "<Content>"
			    "<Url>/TiVoConnect?Command=QueryContainer&amp;Container=3</Url>"
			   "</Content>"
			  "</Links>"
			 "</Item>"
			 "<Item>"
			  "<Details>"
			   "<ContentType>x-container/tivo-music</ContentType>"
			   "<SourceFormat>x-container/folder</SourceFormat>"
			   "<Title>Music on %s</Title>"
			  "</Details>"
			  "<Links>"
			   "<Content>"
			    "<Url>/TiVoConnect?Command=QueryContainer&amp;Container=1</Url>"
			   "</Content>"
			  "</Links>"
			 "</Item>"
			"</TiVoContainer>", friendly_name, friendly_name, friendly_name);
	BuildResp_upnphttp(h, resp, len);
	SendResp_upnphttp(h);
}

int callback(void *args, int argc, char **argv, char **azColName)
{
	struct Response *passed_args = (struct Response *)args;
	char *id = argv[1], *class = argv[4], *detailID = argv[5], *size = argv[9], *title = argv[10], *duration = argv[11],
             *bitrate = argv[12], *sampleFrequency = argv[13], *artist = argv[14], *album = argv[15], *genre = argv[16],
             *comment = argv[17], *date = argv[20], *resolution = argv[21], *mime = argv[25];
	char str_buf[4096];
	char **result;
	int is_audio = 0;
	int ret;

	//passed_args->total++;
	//if( (passed_args->requested > -100) && (passed_args->returned >= passed_args->requested) )
	//	return 0;

	if( strncmp(class, "item", 4) == 0 )
	{
		if( strcmp(mime, "audio/mpeg") == 0 )
		{
			if( passed_args->start > passed_args->total )
				return 0;
			passed_args->total++;
			if( (passed_args->requested > -100) && (passed_args->returned >= passed_args->requested) )
				return 0;
			passed_args->returned++;
			sprintf(str_buf, "<Item><Details>"
			                 "<ContentType>audio/*</ContentType>"
			                 "<SourceFormat>audio/mpeg</SourceFormat>"
			                 "<SourceSize>%s</SourceSize>"
			                 "<SongTitle>%s</SongTitle>", size, title);
			strcat(passed_args->resp, str_buf);
			if( date )
			{
				sprintf(str_buf, "<AlbumYear>%.*s</AlbumYear>", 4, date);
				strcat(passed_args->resp, str_buf);
			}
			is_audio = 1;
		}
		else if( strcmp(mime, "image/jpeg") == 0 )
		{
			if( passed_args->start > passed_args->total )
				return 0;
			passed_args->total++;
			if( (passed_args->requested > -100) && (passed_args->returned >= passed_args->requested) )
				return 0;
			passed_args->returned++;
			sprintf(str_buf, "<Item><Details>"
			                 "<ContentType>image/*</ContentType>"
			                 "<SourceFormat>image/jpeg</SourceFormat>"
			                 "<SourceSize>%s</SourceSize>", size);
			strcat(passed_args->resp, str_buf);
			if( date )
			{
				struct tm tm;
				memset(&tm, 0, sizeof(tm));
				strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
				sprintf(str_buf, "<CaptureDate>0x%X</CaptureDate>", (unsigned int)mktime(&tm));
				strcat(passed_args->resp, str_buf);
			}
			if( comment ) {
				sprintf(str_buf, "<Caption>%s</Caption>", comment);
				strcat(passed_args->resp, str_buf);
			}
		}
		else
		{
			return 0;
		}
		sprintf(str_buf, "<Title>%s</Title>", title);
		strcat(passed_args->resp, str_buf);
		if( artist ) {
			sprintf(str_buf, "<ArtistName>%s</ArtistName>", artist);
			strcat(passed_args->resp, str_buf);
		}
		if( album ) {
			sprintf(str_buf, "<AlbumTitle>%s</AlbumTitle>", album);
			strcat(passed_args->resp, str_buf);
		}
		if( genre ) {
			sprintf(str_buf, "<MusicGenre>%s</MusicGenre>", genre);
			strcat(passed_args->resp, str_buf);
		}
		if( resolution ) {
			sprintf(str_buf, "<SourceWidth>%.*s</SourceWidth>"
			                 "<SourceHeight>%s</SourceHeight>",
			        (index(resolution, 'x')-resolution), resolution, (rindex(resolution, 'x')+1));
			strcat(passed_args->resp, str_buf);
		}
		if( duration ) {
			sprintf(str_buf, "<Duration>%d</Duration>",
			        atoi(rindex(duration, '.')+1) + (1000*atoi(rindex(duration, ':')+1)) + (60000*atoi(rindex(duration, ':')-2)) + (3600000*atoi(duration)));
			strcat(passed_args->resp, str_buf);
		}
		if( bitrate ) {
			sprintf(str_buf, "<SourceBitRate>%s</SourceBitRate>", bitrate);
			strcat(passed_args->resp, str_buf);
		}
		if( sampleFrequency ) {
			sprintf(str_buf, "<SourceSampleRate>%s</SourceSampleRate>", sampleFrequency);
			strcat(passed_args->resp, str_buf);
		}
		sprintf(str_buf, "</Details><Links><Content><Url>/%s/%s.dat</Url>%s</Content></Links>",
		        is_audio?"MediaItems":"Resized", detailID, is_audio?"<AcceptsParams>No</AcceptsParams>":"");
	}
	else if( strncmp(class, "container", 9) == 0 )
	{
		if( passed_args->start > passed_args->total )
			return 0;
		passed_args->total++;
		if( (passed_args->requested > -100) && (passed_args->returned >= passed_args->requested) )
			return 0;
		passed_args->returned++;
		/* Determine the number of children */
		sprintf(str_buf, "SELECT count(ID) from OBJECTS where PARENT_ID = '%s';", id);
		ret = sql_get_table(db, str_buf, &result, NULL, NULL);
		strcat(passed_args->resp, "<Item><Details>"
		                          "<ContentType>x-container/folder</ContentType>"
		                          "<SourceFormat>x-container/folder</SourceFormat>");
		sprintf(str_buf, "<Title>%s</Title>"
		                 "<TotalItems>%s</TotalItems>"
		                 "</Details>", title, result[1]);
		strcat(passed_args->resp, str_buf);

		sprintf(str_buf, "<Links><Content>"
		                 "<Url>/TiVoConnect?Command=QueryContainer&amp;Container=%s</Url>"
		                 "</Content></Links>", id);
		sqlite3_free_table(result);
	}
	strcat(passed_args->resp, str_buf);
	strcat(passed_args->resp, "</Item>");

	return 0;
}

void
SendContainer(struct upnphttp * h, const char * objectID, int itemStart, int itemCount, char * anchorItem, int anchorOffset)
{
	char * resp = malloc(1048576);
	char * items = malloc(1048576);
	char *sql;
	char *zErrMsg = NULL;
	char **result;
	char *title;
	char what[10];
	struct Response args;
	int i, ret;
	*items = '\0';
	memset(&args, 0, sizeof(args));

	args.total = itemStart;
	args.resp = items;
	args.requested = itemCount;

	if( strlen(objectID) == 1 )
	{
		if( *objectID == '1' )
		{
			asprintf(&title, "Music on %s", friendly_name);
		}
		else if( *objectID == '3' )
		{
			asprintf(&title, "Pictures on %s", friendly_name);
		}
	}
	else
	{
		sql = sqlite3_mprintf("SELECT NAME from OBJECTS where OBJECT_ID = '%s'", objectID);
		if( (sql_get_table(db, sql, &result, &ret, NULL) == SQLITE_OK) && ret )
		{
			title = strdup(result[1]);
		}
		else
		{
			title = strdup("UNKNOWN");
		}
		sqlite3_free(sql);
		sqlite3_free_table(result);
	}

	if( anchorItem )
	{
		if( strstr(anchorItem, "QueryContainer") )
		{
			strcpy(what, "OBJECT_ID");
			anchorItem = rindex(anchorItem, '=')+1;
		}
		else
		{
			strcpy(what, "DETAIL_ID");
		}
		sql = sqlite3_mprintf("SELECT %s from OBJECTS where PARENT_ID = '%s'"
		                      " order by CLASS, NAME %s", what, objectID, itemCount<0?"DESC":"ASC");
		if( itemCount < 0 )
			args.requested *= -1;
		DPRINTF(E_DEBUG, L_TIVO, "%s\n", sql);
		if( (sql_get_table(db, sql, &result, &ret, NULL) == SQLITE_OK) && ret )
		{
			for( i=1; i<=ret; i++ )
			{
				if( strcmp(anchorItem, result[i]) == 0 )
				{
					if( itemCount < 0 )
						itemStart = ret - i + itemCount;
					else
						itemStart += i;
					break;
				}
			}
			sqlite3_free_table(result);
		}
		sqlite3_free(sql);
	}
	args.start = itemStart+anchorOffset;
	sql = sqlite3_mprintf("SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
			      " where PARENT_ID = '%s' order by CLASS, NAME", objectID);
	ret = sqlite3_exec(db, sql, callback, (void *) &args, &zErrMsg);
	sqlite3_free(sql);
	if( ret != SQLITE_OK )
	{
		DPRINTF(E_ERROR, L_HTTP, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	}

	sprintf(resp,   "<?xml version='1.0' encoding='UTF-8' ?>\n"
			"<TiVoContainer>"
			 "<Details>"
			  "<ContentType>x-container/tivo-%s</ContentType>"
			  "<SourceFormat>x-container/folder</SourceFormat>"
			  "<TotalItems>%d</TotalItems>"
			  "<Title>%s</Title>"
			 "</Details>"
			 "<ItemStart>%d</ItemStart>"
			 "<ItemCount>%d</ItemCount>"
	                 "%s" /* the actual items xml */
	                 "</TiVoContainer>",
	                (objectID[0]=='1' ? "music":"photos"),
	                args.total+itemStart+anchorOffset,
	                title, itemStart+anchorOffset,
	                args.returned, items);
	free(title);
	free(items);
	BuildResp_upnphttp(h, resp, strlen(resp));
	free(resp);
	SendResp_upnphttp(h);
}

void
ProcessTiVoCommand(struct upnphttp * h, const char * orig_path)
{
	char *path;
	char *key, *val;
	char *saveptr, *item;
	char *command = NULL, *container = NULL, *anchorItem = NULL;
	int itemStart=0, itemCount=-100, anchorOffset=0;

	path = strdup(orig_path);
	DPRINTF(E_DEBUG, L_GENERAL, "Processing TiVo command %s\n", path);

	item = strtok_r( path, "&", &saveptr );
	while( item != NULL )
	{
		if( strlen(item) == 0 )
		{
			item = strtok_r( NULL, "&", &saveptr );
			continue;
		}
		decodeString(item, 1);
		val = item;
		key = strsep(&val, "=");
		decodeString(val, 1);
		DPRINTF(E_DEBUG, L_GENERAL, "%s: %s\n", key, val);
		if( strcasecmp(key, "Command") == 0 )
		{
			command = val;
		}
		else if( strcasecmp(key, "Container") == 0 )
		{
			container = val;
		}
		else if( strcasecmp(key, "ItemStart") == 0 )
		{
			itemStart = atoi(val);
		}
		else if( strcasecmp(key, "ItemCount") == 0 )
		{
			itemCount = atoi(val);
		}
		else if( strcasecmp(key, "AnchorItem") == 0 )
		{
			anchorItem = basename(val);
		}
		else if( strcasecmp(key, "AnchorOffset") == 0 )
		{
			anchorOffset = atoi(val);
		}
		else
		{
			DPRINTF(E_DEBUG, L_GENERAL, "Unhandled parameter [%s]\n", key);
		}
		item = strtok_r( NULL, "&", &saveptr );
	}
	if( anchorItem )
	{
		strip_ext(anchorItem);
	}

	if( command && strcmp(command, "QueryContainer") == 0 )
	{
		if( !container || (strcmp(container, "/") == 0) )
		{
			SendRootContainer(h);
		}
		else
		{
			SendContainer(h, container, itemStart, itemCount, anchorItem, anchorOffset);
		}
	}
	free(path);
	CloseSocket_upnphttp(h);
}

void
ProcessTiVoRequest(struct upnphttp * h, const char * orig_path)
{
	char *path;
	char *key, *val;
	char *saveptr, *item;
	char *command = NULL, *container = NULL;
	int itemStart=0, itemCount=0;

	path = decodeString(orig_path, 0);
	DPRINTF(E_DEBUG, L_GENERAL, "Processing TiVo request %s\n", path);

	item = strtok_r( path, "&", &saveptr );
	while( item != NULL )
	{
		if( strlen( item ) == 0 )
		{
			item = strtok_r( NULL, "&", &saveptr );
			continue;
		}
		val = item;
		key = strsep(&val, "=");
		DPRINTF(E_DEBUG, L_GENERAL, "%s: %s\n", key, val);
		if( strcasecmp(key, "width") == 0 )
		{
			command = val;
		}
		else if( strcasecmp(key, "height") == 0 )
		{
			container = val;
		}
		else if( strcasecmp(key, "rotation") == 0 )
		{
			itemStart = atoi(val);
		}
		else if( strcasecmp(key, "pixelshape") == 0 )
		{
			itemCount = atoi(val);
		}
		item = strtok_r( NULL, "&", &saveptr );
	}

	CloseSocket_upnphttp(h);
}
#endif // TIVO_SUPPORT
