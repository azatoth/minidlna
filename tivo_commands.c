#ifdef ENABLE_TIVO
#include <stdio.h>
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
			   "<Title>Pictures</Title>"
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
			   "<Title>Music</Title>"
			  "</Details>"
			  "<Links>"
			   "<Content>"
			    "<Url>/TiVoConnect?Command=QueryContainer&amp;Container=1</Url>"
			   "</Content>"
			  "</Links>"
			 "</Item>"
			"</TiVoContainer>", friendly_name);
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
		item = strtok_r( NULL, "&", &saveptr );
	}

	if( !container || (strcmp(container, "/") == 0) )
	{
		SendRootContainer(h);
	}
	CloseSocket_upnphttp(h);
}
#endif // ENABLE_TIVO
