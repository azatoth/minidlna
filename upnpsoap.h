/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#ifndef __UPNPSOAP_H__
#define __UPNPSOAP_H__

#define CONTENT_DIRECTORY_SCHEMAS \
	" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"" \
	" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"" \
	" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
#define DLNA_NAMESPACE \
	" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\""

struct Response
{
	char *resp;
	int start;
	int returned;
	int requested;
	int size;
	u_int32_t filter;
	enum client_types client;
};

/* ExecuteSoapAction():
 * this method executes the requested Soap Action */
void
ExecuteSoapAction(struct upnphttp *, const char *, int);

/* SoapError():
 * sends a correct SOAP error with an UPNPError code and 
 * description */
void
SoapError(struct upnphttp * h, int errCode, const char * errDesc);

#endif

