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

	passed_args->total++;
	if( passed_args->start >= passed_args->total )
		return 0;
	if( (passed_args->requested > -100) && (passed_args->returned >= passed_args->requested) )
		return 0;

	if( strncmp(class, "item", 4) == 0 )
	{
		if( strcmp(mime, "audio/mpeg") == 0 )
		{
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
	passed_args->returned++;

	return 0;
}

void
SendContainer(struct upnphttp * h, const char * objectID, int itemStart, int itemCount, char * anchorItem,
              int anchorOffset, int recurse, char * sortOrder, char * filter, unsigned long int randomSeed)
{
	char * resp = malloc(1048576);
	char * items = malloc(1048576);
	char *sql, *item, *saveptr;
	char *zErrMsg = NULL;
	char **result;
	char *title;
	char what[10], order[64], order2[64], myfilter[128];
	char *which;
	struct Response args;
	int i, ret;
	*items = '\0';
	order[0] = '\0';
	order2[0] = '\0';
	myfilter[0] = '\0';
	memset(&args, 0, sizeof(args));

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

	if( recurse )
		asprintf(&which, "OBJECT_ID glob '%s$*'", objectID);
	else
		asprintf(&which, "PARENT_ID = '%s'", objectID);

	if( sortOrder )
	{
		if( strcasestr(sortOrder, "Random") )
		{
			sprintf(order, "tivorandom(%lu)", randomSeed);
			if( itemCount < 0 )
				sprintf(order2, "tivorandom(%lu) DESC", randomSeed);
			else
				sprintf(order2, "tivorandom(%lu)", randomSeed);
		}
		else
		{
			item = strtok_r(sortOrder, ",", &saveptr);
			for( i=0; item != NULL; i++ )
			{
				int reverse=0;
				if( *item == '!' )
				{
					reverse = 1;
					item++;
				}
				if( strcasecmp(item, "Type") == 0 )
				{
					strcat(order, "CLASS");
					strcat(order2, "CLASS");
				}
				else if( strcasecmp(item, "Title") == 0 )
				{
					strcat(order, "TITLE");
					strcat(order2, "TITLE");
				}
				else if( strcasecmp(item, "CreationDate") == 0 ||
				         strcasecmp(item, "CaptureDate") == 0 )
				{
					strcat(order, "DATE");
					strcat(order2, "DATE");
				}
				else
				{
					DPRINTF(E_INFO, L_TIVO, "Unhandled SortOrder [%s]\n", item);
					goto unhandled_order;
				}

				if( reverse )
				{
					strcat(order, " DESC");
					if( itemCount >= 0 )
					{
						strcat(order2, " DESC");
					}
					else
					{
						strcat(order2, " ASC");
					}
				}
				else
				{
					strcat(order, " ASC");
					if( itemCount >= 0 )
					{
						strcat(order2, " ASC");
					}
					else
					{
						strcat(order2, " DESC");
					}
				}
				strcat(order, ", ");
				strcat(order2, ", ");
				unhandled_order:
				item = strtok_r(NULL, ",", &saveptr);
			}
			strcat(order, "DETAIL_ID ASC");
			if( itemCount >= 0 )
			{
				strcat(order2, "DETAIL_ID ASC");
			}
			else
			{
				strcat(order2, "DETAIL_ID DESC");
			}
		}
	}
	else
	{
		sprintf(order, "CLASS, NAME, DETAIL_ID");
		if( itemCount < 0 )
			sprintf(order2, "CLASS DESC, NAME DESC, DETAIL_ID DESC");
		else
			sprintf(order2, "CLASS, NAME, DETAIL_ID");
	}

	if( filter )
	{
		item = strtok_r(filter, ",", &saveptr);
		for( i=0; item != NULL; i++ )
		{
			if( i )
			{
				strcat(myfilter, " or ");
			}
			if( strcasecmp(item, "x-container/folder") == 0 )
			{
				strcat(myfilter, "CLASS glob 'container*'");
			}
			else if( strncasecmp(item, "image", 5) == 0 )
			{
				strcat(myfilter, "MIME = 'image/jpeg'");
			}
			else if( strncasecmp(item, "audio", 5) == 0 )
			{
				strcat(myfilter, "MIME = 'audio/mpeg'");
			}
			else
			{
				DPRINTF(E_INFO, L_TIVO, "Unhandled Filter [%s]\n", item);
				strcat(myfilter, "0 = 1");
			}
			item = strtok_r(NULL, ",", &saveptr);
		}
	}
	else
	{
		strcpy(myfilter, "MIME = 'image/jpeg' or MIME = 'audio/mpeg' or CLASS glob 'container*'");
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
		sqlite3Prng.isInit = 0;
		sql = sqlite3_mprintf("SELECT %s from OBJECTS o left join DETAILS d on (o.DETAIL_ID = d.ID)"
		                      " where %s and (%s)"
		                      " order by %s", what, which, myfilter, order2);
		if( itemCount < 0 )
		{
			args.requested *= -1;
		}
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
	sqlite3Prng.isInit = 0;
	sql = sqlite3_mprintf("SELECT * from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
		              " where %s and (%s)"
			      " order by %s", which, myfilter, order);
	DPRINTF(E_DEBUG, L_TIVO, "%s\n", sql);
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
	                args.total,
	                title, args.start,
	                args.returned, items);
	free(items);
	free(title);
	free(which);
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
	char *sortOrder = NULL, *filter = NULL;
	int itemStart=0, itemCount=-100, anchorOffset=0, recurse=0;
	unsigned long int randomSeed=0;

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
		else if( strcasecmp(key, "Recurse") == 0 )
		{
			recurse = strcasecmp("yes", val) == 0 ? 1 : 0;
		}
		else if( strcasecmp(key, "SortOrder") == 0 )
		{
			sortOrder = val;
		}
		else if( strcasecmp(key, "Filter") == 0 )
		{
			filter = val;
		}
		else if( strcasecmp(key, "RandomSeed") == 0 )
		{
			randomSeed = strtoul(val, NULL, 10);
		}
		else if( strcasecmp(key, "Format") == 0 )
		{
			// Only send XML
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
			SendContainer(h, container, itemStart, itemCount, anchorItem, anchorOffset, recurse, sortOrder, filter, randomSeed);
		}
	}
	free(path);
	CloseSocket_upnphttp(h);
}
#endif // TIVO_SUPPORT
