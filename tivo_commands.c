#include "config.h"
#ifdef ENABLE_TIVO
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tivo_utils.h"
#include "upnpglobalvars.h"
#include "upnphttp.h"
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
			    "<Url>/TiVoConnect?Command=QueryContainer&amp;Container=3/Pictures on %s</Url>"
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
			"</TiVoContainer>", friendly_name, friendly_name, friendly_name, friendly_name);
	BuildResp_upnphttp(h, resp, len);
	SendResp_upnphttp(h);
}

#if 0
int callback(void *args, int argc, char **argv, char **azColName)
{
	struct Response *passed_args = (struct Response *)args;
	char *id = argv[1], *parent = argv[2], *refID = argv[3], *class = argv[4], *detailID = argv[5], *size = argv[9], *title = argv[10],
	     *duration = argv[11], *bitrate = argv[12], *sampleFrequency = argv[13], *artist = argv[14], *album = argv[15],
	     *genre = argv[16], *comment = argv[17], *nrAudioChannels = argv[18], *track = argv[19], *date = argv[20], *resolution = argv[21],
	     *tn = argv[22], *creator = argv[23], *dlna_pn = argv[24], *mime = argv[25], *album_art = argv[26], *art_dlna_pn = argv[27];
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
		if( album_art && atoi(album_art) && (!passed_args->filter || strstr(passed_args->filter, "upnp:albumArtURI")) ) {
			strcat(passed_args->resp, "&lt;upnp:albumArtURI ");
			if( !passed_args->filter || strstr(passed_args->filter, "upnp:albumArtURI@dlna:profileID") ) {
				sprintf(str_buf, "dlna:profileID=\"%s\" xmlns:dlna=\"urn:schemas-dlnaorg:metadata-1-0/\"", art_dlna_pn);
				strcat(passed_args->resp, str_buf);
			}
			sprintf(str_buf, "&gt;http://%s:%d/AlbumArt/%s.jpg&lt;/upnp:albumArtURI&gt;",
					 lan_addr[0].str, runtime_vars.port, album_art);
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
						"http://%s:%d/MediaItems/%s.dat"
					 "&lt;/res&gt;",
					 mime, dlna_buf, lan_addr[0].str, runtime_vars.port, detailID);
			#if 0 //JPEG_RESIZE
			if( dlna_pn && (strncmp(dlna_pn, "JPEG_LRG", 8) == 0) ) {
				strcat(passed_args->resp, str_buf);
				sprintf(str_buf, "&lt;res "
						 "protocolInfo=\"http-get:*:%s:%s\"&gt;"
							"http://%s:%d/Resized/%s"
						 "&lt;/res&gt;",
						 mime, "DLNA.ORG_PN=JPEG_SM", lan_addr[0].str, runtime_vars.port, id);
			}
			#endif
			if( tn && atoi(tn) && dlna_pn ) {
				strcat(passed_args->resp, str_buf);
				strcat(passed_args->resp, "&lt;res ");
				sprintf(str_buf, "protocolInfo=\"http-get:*:%s:%s\"&gt;"
							"http://%s:%d/Thumbnails/%s.dat"
						 "&lt;/res&gt;",
						 mime, "DLNA.ORG_PN=JPEG_TN", lan_addr[0].str, runtime_vars.port, detailID);
			}
			strcat(passed_args->resp, str_buf);
		}
		strcpy(str_buf, "&lt;/item&gt;");
	}
	else if( strncmp(class, "container", 9) == 0 )
	{
		sprintf(str_buf, "SELECT count(ID) from OBJECTS where PARENT_ID = '%s';", id);
		ret = sql_get_table(db, str_buf, &result, NULL, NULL);
		sprintf(str_buf, "&lt;container id=\"%s\" parentID=\"%s\" restricted=\"1\" ", id, parent);
		strcat(passed_args->resp, str_buf);
		if( !passed_args->filter || strstr(passed_args->filter, "@childCount"))
		{
			sprintf(str_buf, "childCount=\"%s\"", result[1]);
			strcat(passed_args->resp, str_buf);
		}
		/* If the client calls for BrowseMetadata on root, we have to include our "upnp:searchClass"'s, unless they're filtered out */
		if( (passed_args->requested == 1) && (strcmp(id, "0") == 0) )
		{
			if( !passed_args->filter || strstr(passed_args->filter, "upnp:searchClass") )
			{
				strcat(passed_args->resp, "&gt;"
							  "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.audioItem&lt;/upnp:searchClass&gt;"
							  "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.imageItem&lt;/upnp:searchClass&gt;"
							  "&lt;upnp:searchClass includeDerived=\"1\"&gt;object.item.videoItem&lt;/upnp:searchClass");
			}
		}
		sprintf(str_buf, "&gt;"
				 "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
				 "&lt;upnp:class&gt;object.%s&lt;/upnp:class&gt;",
				 title, class);
		strcat(passed_args->resp, str_buf);
		if( creator && (!passed_args->filter || strstr(passed_args->filter, "dc:creator")) ) {
			sprintf(str_buf, "&lt;dc:creator&gt;%s&lt;/dc:creator&gt;", creator);
			strcat(passed_args->resp, str_buf);
		}
		if( genre && (!passed_args->filter || strstr(passed_args->filter, "upnp:genre")) ) {
			sprintf(str_buf, "&lt;upnp:genre&gt;%s&lt;/upnp:genre&gt;", genre);
			strcat(passed_args->resp, str_buf);
		}
		if( artist && (!passed_args->filter || strstr(passed_args->filter, "upnp:artist")) ) {
			sprintf(str_buf, "&lt;upnp:artist&gt;%s&lt;/upnp:artist&gt;", artist);
			strcat(passed_args->resp, str_buf);
		}
		if( album_art && atoi(album_art) && (!passed_args->filter || strstr(passed_args->filter, "upnp:albumArtURI")) ) {
			strcat(passed_args->resp, "&lt;upnp:albumArtURI ");
			if( !passed_args->filter || strstr(passed_args->filter, "upnp:albumArtURI@dlna:profileID") ) {
				sprintf(str_buf, "dlna:profileID=\"%s\" xmlns:dlna=\"urn:schemas-dlnaorg:metadata-1-0/\"", art_dlna_pn);
				strcat(passed_args->resp, str_buf);
			}
			sprintf(str_buf, "&gt;http://%s:%d/AlbumArt/%s.jpg&lt;/upnp:albumArtURI&gt;",
					 lan_addr[0].str, runtime_vars.port, album_art);
			strcat(passed_args->resp, str_buf);
		}
		sprintf(str_buf, "&lt;/container&gt;");
		sqlite3_free_table(result);
	}
	strcat(passed_args->resp, str_buf);

	return 0;
}
#endif

void
SendContainer(struct upnphttp * h, const char * objectID, const char * title, int itemStart, int itemCount)
{
	char * resp;
	int len;

	len = asprintf(&resp, "<?xml version='1.0' encoding='UTF-8' ?>\n"
			"<TiVoContainer>"
			 "<Details>"
			  "<ContentType>x-container/tivo-%s</ContentType>"
			  "<SourceFormat>x-container/folder</SourceFormat>"
			  "<TotalDuration>0</TotalDuration>"
			  "<TotalItems>2</TotalItems>"
			  "<Title>%s</Title>"
			 "</Details>"
			 "<ItemStart>%d</ItemStart>"
			 "<ItemCount>%d</ItemCount>"
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
			"</TiVoContainer>",
	                (objectID[0]=='1') ? "music":"photos",
	                title, itemStart, itemCount, title, title);
	BuildResp_upnphttp(h, resp, len);
	SendResp_upnphttp(h);
}

void
ProcessTiVoCommand(struct upnphttp * h, const char * orig_path)
{
	char *path;
	char *key, *val;
	char *saveptr, *item;
	char *command = NULL, *container = NULL;
	int itemStart=0, itemCount=0;

	path = decodeString(orig_path);
	DPRINTF(E_DEBUG, L_GENERAL, "Processing TiVo command %s\n", path);

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
		if( strcasecmp(key, "command") == 0 )
		{
			command = val;
		}
		else if( strcasecmp(key, "container") == 0 )
		{
			container = val;
		}
		else if( strcasecmp(key, "itemstart") == 0 )
		{
			itemStart = atoi(val);
		}
		else if( strcasecmp(key, "itemcount") == 0 )
		{
			itemCount = atoi(val);
		}
		item = strtok_r( NULL, "&", &saveptr );
	}

	if( strcmp(command, "QueryContainer") == 0 )
	{
		if( !container || (strcmp(container, "/") == 0) )
		{
			SendRootContainer(h);
		}
		else
		{
			val = container;
			key = strsep(&val, "/");
			SendContainer(h, container, val, itemStart, itemCount);
		}
	}
	CloseSocket_upnphttp(h);
}
#endif // ENABLE_TIVO
