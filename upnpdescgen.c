/* $Id$ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2008 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "getifaddr.h"
#include "upnpdescgen.h"
#include "minidlnapath.h"
#include "upnpglobalvars.h"
#include "upnpdescstrings.h"

static const char * const upnptypes[] =
{
	"string",
	"boolean",
	"ui2",
	"ui4",
	"i4",
	"uri",
	"int",
	"bin.base64"
};

static const char * const upnpdefaultvalues[] =
{
	0,
	"Unconfigured"
};

static const char * const upnpallowedvalues[] =
{
	0,			/* 0 */
	"DSL",			/* 1 */
	"POTS",
	"Cable",
	"Ethernet",
	0,
	"Up",			/* 6 */
	"Down",
	"Initializing",
	"Unavailable",
	0,
	"TCP",			/* 11 */
	"UDP",
	0,
	"Unconfigured",		/* 14 */
	"IP_Routed",
	"IP_Bridged",
	0,
	"Unconfigured",		/* 18 */
	"Connecting",
	"Connected",
	"PendingDisconnect",
	"Disconnecting",
	"Disconnected",
	0,
	"ERROR_NONE",		/* 25 */
	0,
	"OK",			/* 27 */
	"ContentFormatMismatch",
	"InsufficientBandwidth",
	"UnreliableChannel",
	"Unknown",
	0,
	"Input",		/* 33 */
	"Output",
	0,
	"BrowseMetadata",	/* 36 */
	"BrowseDirectChildren",
	0,
	"COMPLETED",		/* 39 */
	"ERROR",
	"IN_PROGRESS",
	"STOPPED",
	0,
	RESOURCE_PROTOCOL_INFO_VALUES,		/* 44 */
	0,
	"",			/* 46 */
	0
};

static const char xmlver[] = 
	"<?xml version=\"1.0\"?>\r\n";
static const char root_service[] =
	"scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"";
static const char root_device[] = 
	"root xmlns=\"urn:schemas-upnp-org:device-1-0\"";

/* root Description of the UPnP Device 
 * fixed to match UPnP_IGD_InternetGatewayDevice 1.0.pdf 
 * presentationURL is only "recommended" but the router doesn't appears
 * in "Network connections" in Windows XP if it is not present. */
static const struct XMLElt rootDesc[] =
{
	{root_device, INITHELPER(1,2)},
	{"specVersion", INITHELPER(3,2)},
	{"device", INITHELPER(5,13)},
	{"/major", "1"},
	{"/minor", "0"},
	{"/deviceType", "urn:schemas-upnp-org:device:MediaServer:1"},
	{"/friendlyName", friendly_name},	/* required */
	{"/manufacturer", ROOTDEV_MANUFACTURER},		/* required */
	{"/manufacturerURL", ROOTDEV_MANUFACTURERURL},	/* optional */
	{"/modelDescription", ROOTDEV_MODELDESCRIPTION}, /* recommended */
	{"/modelName", ROOTDEV_MODELNAME},	/* required */
	{"/modelNumber", modelnumber},
	{"/modelURL", ROOTDEV_MODELURL},
	{"/serialNumber", serialnumber},
	{"/UDN", uuidvalue},	/* required */
	{"/dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\"", "DMS-1.50"},
	{"/presentationURL", presentationurl},	/* recommended */
	{"serviceList", INITHELPER(18,3)},
	{"service", INITHELPER(21,5)},
	{"service", INITHELPER(26,5)},
	{"service", INITHELPER(31,5)},
	{"/serviceType", "urn:schemas-upnp-org:service:ContentDirectory:1"},
	{"/serviceId", "urn:upnp-org:serviceId:ContentDirectory"},
	{"/controlURL", CONTENTDIRECTORY_CONTROLURL},
	{"/eventSubURL", CONTENTDIRECTORY_EVENTURL},
	{"/SCPDURL", CONTENTDIRECTORY_PATH},
	{"/serviceType", "urn:schemas-upnp-org:service:ConnectionManager:1"},
	{"/serviceId", "urn:upnp-org:serviceId:ConnectionManager"},
	{"/controlURL", CONNECTIONMGR_CONTROLURL},
	{"/eventSubURL", CONNECTIONMGR_EVENTURL},
	{"/SCPDURL", CONNECTIONMGR_PATH},
	{"/serviceType", "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1"},
	{"/serviceId", "urn:microsoft.com:serviceId:X_MS_MediaReceiverRegistrar"},
	{"/controlURL", X_MS_MEDIARECEIVERREGISTRAR_CONTROLURL},
	{"/eventSubURL", X_MS_MEDIARECEIVERREGISTRAR_EVENTURL},
	{"/SCPDURL", X_MS_MEDIARECEIVERREGISTRAR_PATH},
	{0, 0}
};

static const struct argument AddPortMappingArgs[] =
{
	{NULL, 1, 11},
	{NULL, 1, 12},
	{NULL, 1, 14},
	{NULL, 1, 13},
	{NULL, 1, 15},
	{NULL, 1, 9},
	{NULL, 1, 16},
	{NULL, 1, 10},
	{NULL, 0, 0}
};

static const struct argument DeletePortMappingArgs[] = 
{
	{NULL, 1, 11},
	{NULL, 1, 12},
	{NULL, 1, 14},
	{NULL, 0, 0}
};

static const struct argument SetConnectionTypeArgs[] =
{
	{NULL, 1, 0},
	{NULL, 0, 0}
};

static const struct argument GetConnectionTypeInfoArgs[] =
{
	{NULL, 2, 0},
	{NULL, 2, 1},
	{NULL, 0, 0}
};

static const struct argument GetNATRSIPStatusArgs[] =
{
	{NULL, 2, 5},
	{NULL, 2, 6},
	{NULL, 0, 0}
};

static const struct argument GetGenericPortMappingEntryArgs[] =
{
	{NULL, 1, 8},
	{NULL, 2, 11},
	{NULL, 2, 12},
	{NULL, 2, 14},
	{NULL, 2, 13},
	{NULL, 2, 15},
	{NULL, 2, 9},
	{NULL, 2, 16},
	{NULL, 2, 10},
	{NULL, 0, 0}
};

static const struct argument GetSpecificPortMappingEntryArgs[] =
{
	{NULL, 1, 11},
	{NULL, 1, 12},
	{NULL, 1, 14},
	{NULL, 2, 13},
	{NULL, 2, 15},
	{NULL, 2, 9},
	{NULL, 2, 16},
	{NULL, 2, 10},
	{NULL, 0, 0}
};

/* For ConnectionManager */
static const struct argument GetProtocolInfoArgs[] =
{
	{"Source", 2, 0},
	{"Sink", 2, 1},
	{NULL, 0, 0}
};

static const struct argument PrepareForConnectionArgs[] =
{
	{"RemoteProtocolInfo", 1, 6},
	{"PeerConnectionManager", 1, 4},
	{"PeerConnectionID", 1, 7},
	{"Direction", 1, 5},
	{"ConnectionID", 2, 7},
	{"AVTransportID", 2, 8},
	{"RcsID", 2, 9},
	{NULL, 0, 0}
};

static const struct argument ConnectionCompleteArgs[] =
{
	{"ConnectionID", 1, 7},
	{NULL, 0, 0}
};

static const struct argument GetCurrentConnectionIDsArgs[] =
{
	{"ConnectionIDs", 2, 2},
	{NULL, 0, 0}
};

static const struct argument GetCurrentConnectionInfoArgs[] =
{
	{"ConnectionID", 1, 7},
	{"RcsID", 2, 9},
	{"AVTransportID", 2, 8},
	{"ProtocolInfo", 2, 6},
	{"PeerConnectionManager", 2, 4},
	{"PeerConnectionID", 2, 7},
	{"Direction", 2, 5},
	{"Status", 2, 3},
	{NULL, 0, 0}
};

static const struct action ConnectionManagerActions[] =
{
	{"GetProtocolInfo", GetProtocolInfoArgs}, /* R */
	//OPTIONAL {"PrepareForConnection", PrepareForConnectionArgs}, /* R */
	//OPTIONAL {"ConnectionComplete", ConnectionCompleteArgs}, /* R */
	{"GetCurrentConnectionIDs", GetCurrentConnectionIDsArgs}, /* R */
	{"GetCurrentConnectionInfo", GetCurrentConnectionInfoArgs}, /* R */
	{0, 0}
};

static const struct stateVar ConnectionManagerVars[] =
{
	{"SourceProtocolInfo", 1<<7, 0, 44, 44}, /* required */
	{"SinkProtocolInfo", 1<<7, 0}, /* required */
	{"CurrentConnectionIDs", 1<<7, 0}, /* required */
	{"A_ARG_TYPE_ConnectionStatus", 0, 0, 27}, /* required */
	{"A_ARG_TYPE_ConnectionManager", 0, 0}, /* required */
	{"A_ARG_TYPE_Direction", 0, 0, 33}, /* required */
	{"A_ARG_TYPE_ProtocolInfo", 0, 0}, /* required */
	{"A_ARG_TYPE_ConnectionID", 4, 0}, /* required */
	{"A_ARG_TYPE_AVTransportID", 4, 0}, /* required */
	{"A_ARG_TYPE_RcsID", 4, 0}, /* required */
	{0, 0}
};

static const struct argument GetSearchCapabilitiesArgs[] =
{
	{"SearchCaps", 2, 16},
	{0, 0}
};

static const struct argument GetSortCapabilitiesArgs[] =
{
	{"SortCaps", 2, 17},
	{0, 0}
};

static const struct argument GetSystemUpdateIDArgs[] =
{
	{"Id", 2, 18},
	{0, 0}
};

static const struct argument BrowseArgs[] =
{
	{"ObjectID", 1, 1},
	{"BrowseFlag", 1, 4},
	{"Filter", 1, 5},
	{"StartingIndex", 1, 7},
	{"RequestedCount", 1, 8},
	{"SortCriteria", 1, 6},
	{"Result", 2, 2},
	{"NumberReturned", 2, 8},
	{"TotalMatches", 2, 8},
	{"UpdateID", 2, 9},
	{0, 0}
};

static const struct argument SearchArgs[] =
{
	{"ContainerID", 1, 1},
	{"SearchCriteria", 1, 3},
	{"Filter", 1, 5},
	{"StartingIndex", 1, 7},
	{"RequestedCount", 1, 8},
	{"SortCriteria", 1, 6},
	{"Result", 2, 2},
	{"NumberReturned", 2, 8},
	{"TotalMatches", 2, 8},
	{"UpdateID", 2, 9},
	{0, 0}
};

static const struct action ContentDirectoryActions[] =
{
	{"GetSearchCapabilities", GetSearchCapabilitiesArgs}, /* R */
	{"GetSortCapabilities", GetSortCapabilitiesArgs}, /* R */
	{"GetSystemUpdateID", GetSystemUpdateIDArgs}, /* R */
	{"Browse", BrowseArgs}, /* R */
	{"Search", SearchArgs}, /* O */
#if 0 // Not implementing optional features yet...
	{"CreateObject", CreateObjectArgs}, /* O */
	{"DestroyObject", DestroyObjectArgs}, /* O */
	{"UpdateObject", UpdateObjectArgs}, /* O */
	{"ImportResource", ImportResourceArgs}, /* O */
	{"ExportResource", ExportResourceArgs}, /* O */
	{"StopTransferResource", StopTransferResourceArgs}, /* O */
	{"GetTransferProgress", GetTransferProgressArgs}, /* O */
	{"DeleteResource", DeleteResourceArgs}, /* O */
	{"CreateReference", CreateReferenceArgs}, /* O */
#endif
	{0, 0}
};

static const struct stateVar ContentDirectoryVars[] =
{
	{"TransferIDs", 1<<7, 0, 46, 46}, /* 0 */
	{"A_ARG_TYPE_ObjectID", 0, 0},
	{"A_ARG_TYPE_Result", 0, 0},
	{"A_ARG_TYPE_SearchCriteria", 0, 0},
	{"A_ARG_TYPE_BrowseFlag", 0, 0, 36},
	/* Allowed Values : BrowseMetadata / BrowseDirectChildren */
	{"A_ARG_TYPE_Filter", 0, 0}, /* 5 */
	{"A_ARG_TYPE_SortCriteria", 0, 0},
	{"A_ARG_TYPE_Index", 3, 0},
	{"A_ARG_TYPE_Count", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	{"A_ARG_TYPE_UpdateID", 3, 0},
	//JM{"A_ARG_TYPE_TransferID", 3, 0}, /* 10 */
	//JM{"A_ARG_TYPE_TransferStatus", 0, 0, 39},
	/* Allowed Values : COMPLETED / ERROR / IN_PROGRESS / STOPPED */
	//JM{"A_ARG_TYPE_TransferLength", 0, 0},
	//JM{"A_ARG_TYPE_TransferTotal", 0, 0},
	//JM{"A_ARG_TYPE_TagValueList", 0, 0},
	//JM{"A_ARG_TYPE_URI", 5, 0}, /* 15 */
	{"SearchCapabilities", 0, 0},
	{"SortCapabilities", 0, 0},
	{"SystemUpdateID", 3|0x80, 0, 46, 46},
	//{"ContainerUpdateIDs", 0, 0},
	{0, 0}
};

static const struct argument GetIsAuthorizedArgs[] =
{
	{"DeviceID", 1, 0},
	{"Result", 2, 3},
	{NULL, 0, 0}
};

static const struct argument GetIsValidatedArgs[] =
{
	{"DeviceID", 1, 0},
	{"Result", 2, 3},
	{NULL, 0, 0}
};

static const struct argument GetRegisterDeviceArgs[] =
{
	{"RegistrationReqMsg", 1, 1},
	{"RegistrationRespMsg", 2, 2},
	{NULL, 0, 0}
};

static const struct action X_MS_MediaReceiverRegistrarActions[] =
{
	{"IsAuthorized", GetIsAuthorizedArgs}, /* R */
	{"IsValidated", GetIsValidatedArgs}, /* R */
	{"RegisterDevice", GetRegisterDeviceArgs}, /* R */
	{0, 0}
};

static const struct stateVar X_MS_MediaReceiverRegistrarVars[] =
{
	{"A_ARG_TYPE_DeviceID", 0, 0},
	{"A_ARG_TYPE_RegistrationReqMsg", 7, 0},
	{"A_ARG_TYPE_RegistrationRespMsg", 7, 0},
	{"A_ARG_TYPE_Result", 6, 0},
	{"AuthorizationDeniedUpdateID", (1<<7)|3, 0},
	{"AuthorizationGrantedUpdateID", (1<<7)|3, 0},
	{"ValidationRevokedUpdateID", (1<<7)|3, 0},
	{"ValidationSucceededUpdateID", (1<<7)|3, 0},
	{0, 0}
};

/* WANCfg.xml */
/* See UPnP_IGD_WANCommonInterfaceConfig 1.0.pdf */

static const struct argument GetCommonLinkPropertiesArgs[] =
{
	{NULL, 2, 0},
	{NULL, 2, 1},
	{NULL, 2, 2},
	{NULL, 2, 3},
	{NULL, 0, 0}
};

static const struct argument GetTotalBytesSentArgs[] =
{
	{NULL, 2, 4},
	{NULL, 0, 0}
};

static const struct argument GetTotalBytesReceivedArgs[] =
{
	{NULL, 2, 5},
	{NULL, 0, 0}
};

static const struct argument GetTotalPacketsSentArgs[] =
{
	{NULL, 2, 6},
	{NULL, 0, 0}
};

static const struct argument GetTotalPacketsReceivedArgs[] =
{
	{NULL, 2, 7},
	{NULL, 0, 0}
};

static const struct serviceDesc scpdContentDirectory =
{ ContentDirectoryActions, ContentDirectoryVars };
//{ ContentDirectoryActions, ContentDirectoryVars };

static const struct serviceDesc scpdConnectionManager =
{ ConnectionManagerActions, ConnectionManagerVars };

static const struct serviceDesc scpdX_MS_MediaReceiverRegistrar =
{ X_MS_MediaReceiverRegistrarActions, X_MS_MediaReceiverRegistrarVars };

/* strcat_str()
 * concatenate the string and use realloc to increase the
 * memory buffer if needed. */
static char *
strcat_str(char * str, int * len, int * tmplen, const char * s2)
{
	int s2len;
	s2len = (int)strlen(s2);
	if(*tmplen <= (*len + s2len))
	{
		if(s2len < 256)
			*tmplen += 256;
		else
			*tmplen += s2len + 1;
		str = (char *)realloc(str, *tmplen);
	}
	/*strcpy(str + *len, s2); */
	memcpy(str + *len, s2, s2len + 1);
	*len += s2len;
	return str;
}

/* strcat_char() :
 * concatenate a character and use realloc to increase the
 * size of the memory buffer if needed */
static char *
strcat_char(char * str, int * len, int * tmplen, char c)
{
	if(*tmplen <= (*len + 1))
	{
		*tmplen += 256;
		str = (char *)realloc(str, *tmplen);
	}
	str[*len] = c;
	(*len)++;
	return str;
}

/* iterative subroutine using a small stack
 * This way, the progam stack usage is kept low */
static char *
genXML(char * str, int * len, int * tmplen,
                   const struct XMLElt * p)
{
	unsigned short i, j, k;
	int top;
	const char * eltname, *s;
	char c;
	char element[64];
	struct {
		unsigned short i;
		unsigned short j;
		const char * eltname;
	} pile[16]; /* stack */
	top = -1;
	i = 0;	/* current node */
	j = 1;	/* i + number of nodes*/
	for(;;)
	{
		eltname = p[i].eltname;
		if(!eltname)
			return str;
		if(eltname[0] == '/')
		{
			/*printf("<%s>%s<%s>\n", eltname+1, p[i].data, eltname); */
			str = strcat_char(str, len, tmplen, '<');
			str = strcat_str(str, len, tmplen, eltname+1);
			str = strcat_char(str, len, tmplen, '>');
			str = strcat_str(str, len, tmplen, p[i].data);
			str = strcat_char(str, len, tmplen, '<');
			sscanf(eltname, "%s", element);
			str = strcat_str(str, len, tmplen, element);
			str = strcat_char(str, len, tmplen, '>');
			for(;;)
			{
				if(top < 0)
					return str;
				i = ++(pile[top].i);
				j = pile[top].j;
				/*printf("  pile[%d]\t%d %d\n", top, i, j); */
				if(i==j)
				{
					/*printf("</%s>\n", pile[top].eltname); */
					str = strcat_char(str, len, tmplen, '<');
					str = strcat_char(str, len, tmplen, '/');
					s = pile[top].eltname;
					for(c = *s; c > ' '; c = *(++s))
						str = strcat_char(str, len, tmplen, c);
					str = strcat_char(str, len, tmplen, '>');
					top--;
				}
				else
					break;
			}
		}
		else
		{
			/*printf("<%s>\n", eltname); */
			str = strcat_char(str, len, tmplen, '<');
			str = strcat_str(str, len, tmplen, eltname);
			str = strcat_char(str, len, tmplen, '>');
			k = i;
			/*i = p[k].index; */
			/*j = i + p[k].nchild; */
			i = (unsigned)p[k].data & 0xffff;
			j = i + ((unsigned)p[k].data >> 16);
			top++;
			/*printf(" +pile[%d]\t%d %d\n", top, i, j); */
			pile[top].i = i;
			pile[top].j = j;
			pile[top].eltname = eltname;
		}
	}
}

/* genRootDesc() :
 * - Generate the root description of the UPnP device.
 * - the len argument is used to return the length of
 *   the returned string. 
 * - tmp_uuid argument is used to build the uuid string */
char *
genRootDesc(int * len)
{
	char * str;
	int tmplen;
	tmplen = 2048;
	str = (char *)malloc(tmplen);
	if(str == NULL)
		return NULL;
#if 1
	* len = strlen(xmlver);
	/*strcpy(str, xmlver); */
	memcpy(str, xmlver, *len + 1);
	str = genXML(str, len, &tmplen, rootDesc);
	str[*len] = '\0';
	return str;
#else
char *ret = calloc(1, 8192);
sprintf(ret, "<?xml version='1.0' encoding='UTF-8' ?>\r\n"
"<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>MiniDLNA (MaggardMachine2)</friendlyName><manufacturer>NETGEAR</manufacturer><manufacturerURL>http://www.netgear.com</manufacturerURL><modelDescription>NETGEAR ReadyNAS NV</modelDescription><modelName>ReadyNAS</modelName><modelNumber>NV</modelNumber><modelURL>http://www.netgear.com</modelURL><UDN>uuid:aefc3d94-8cf7-11dd-b3bb-ff0d6f9a7e6d</UDN><dlna:X_DLNADOC>DMS-1.50</dlna:X_DLNADOC>\r\n"
"<serviceList><service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId><SCPDURL>/ConnectionMgr.xml</SCPDURL><controlURL>/ctl/ConnectionMgr</controlURL><eventSubURL>/evt/ConnectionMgr</eventSubURL></service><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId><SCPDURL>/ContentDir.xml</SCPDURL><controlURL>/ctl/ContentDir</controlURL><eventSubURL>/evt/ContentDir</eventSubURL></service></serviceList></device></root>");
	* len = strlen(ret);
return ret;
#endif
}

/* genServiceDesc() :
 * Generate service description with allowed methods and 
 * related variables. */
static char *
genServiceDesc(int * len, const struct serviceDesc * s)
{
	int i, j;
	const struct action * acts;
	const struct stateVar * vars;
	const struct argument * args;
	const char * p;
	char * str;
	int tmplen;
	tmplen = 2048;
	str = (char *)malloc(tmplen);
	if(str == NULL)
		return NULL;
	/*strcpy(str, xmlver); */
	*len = strlen(xmlver);
	memcpy(str, xmlver, *len + 1);
	
	acts = s->actionList;
	vars = s->serviceStateTable;

	str = strcat_char(str, len, &tmplen, '<');
	str = strcat_str(str, len, &tmplen, root_service);
	str = strcat_char(str, len, &tmplen, '>');

	str = strcat_str(str, len, &tmplen,
		"<specVersion><major>1</major><minor>0</minor></specVersion>");

	i = 0;
	str = strcat_str(str, len, &tmplen, "<actionList>");
	while(acts[i].name)
	{
		str = strcat_str(str, len, &tmplen, "<action><name>");
		str = strcat_str(str, len, &tmplen, acts[i].name);
		str = strcat_str(str, len, &tmplen, "</name>");
		/* argument List */
		args = acts[i].args;
		if(args)
		{
			str = strcat_str(str, len, &tmplen, "<argumentList>");
			j = 0;
			while(args[j].dir)
			{
				//JM str = strcat_str(str, len, &tmplen, "<argument><name>New");
				str = strcat_str(str, len, &tmplen, "<argument><name>");
				p = vars[args[j].relatedVar].name;
				if(0 == memcmp(p, "PortMapping", 11)
				   && 0 != memcmp(p + 11, "Description", 11)) {
					if(0 == memcmp(p + 11, "NumberOfEntries", 15))
						str = strcat_str(str, len, &tmplen, "PortMappingIndex");
					else
						str = strcat_str(str, len, &tmplen, p + 11);
				} else {
					str = strcat_str(str, len, &tmplen, (args[j].name ? args[j].name : p));
				}
				str = strcat_str(str, len, &tmplen, "</name><direction>");
				str = strcat_str(str, len, &tmplen, args[j].dir==1?"in":"out");
				str = strcat_str(str, len, &tmplen,
						"</direction><relatedStateVariable>");
				str = strcat_str(str, len, &tmplen, p);
				str = strcat_str(str, len, &tmplen,
						"</relatedStateVariable></argument>");
				j++;
			}
			str = strcat_str(str, len, &tmplen,"</argumentList>");
		}
		str = strcat_str(str, len, &tmplen, "</action>");
		/*str = strcat_char(str, len, &tmplen, '\n'); // TEMP ! */
		i++;
	}
	str = strcat_str(str, len, &tmplen, "</actionList><serviceStateTable>");
	i = 0;
	while(vars[i].name)
	{
		str = strcat_str(str, len, &tmplen,
				"<stateVariable sendEvents=\"");
		str = strcat_str(str, len, &tmplen, (vars[i].itype & 0x80)?"yes":"no");
		str = strcat_str(str, len, &tmplen, "\"><name>");
		str = strcat_str(str, len, &tmplen, vars[i].name);
		str = strcat_str(str, len, &tmplen, "</name><dataType>");
		str = strcat_str(str, len, &tmplen, upnptypes[vars[i].itype & 0x0f]);
		str = strcat_str(str, len, &tmplen, "</dataType>");
		if(vars[i].iallowedlist)
		{
		  str = strcat_str(str, len, &tmplen, "<allowedValueList>");
		  for(j=vars[i].iallowedlist; upnpallowedvalues[j]; j++)
		  {
		    str = strcat_str(str, len, &tmplen, "<allowedValue>");
		    str = strcat_str(str, len, &tmplen, upnpallowedvalues[j]);
		    str = strcat_str(str, len, &tmplen, "</allowedValue>");
		  }
		  str = strcat_str(str, len, &tmplen, "</allowedValueList>");
		}
		/*if(vars[i].defaultValue) */
		if(vars[i].idefault)
		{
		  str = strcat_str(str, len, &tmplen, "<defaultValue>");
		  /*str = strcat_str(str, len, &tmplen, vars[i].defaultValue); */
		  str = strcat_str(str, len, &tmplen, upnpdefaultvalues[vars[i].idefault]);
		  str = strcat_str(str, len, &tmplen, "</defaultValue>");
		}
		str = strcat_str(str, len, &tmplen, "</stateVariable>");
		/*str = strcat_char(str, len, &tmplen, '\n'); // TEMP ! */
		i++;
	}
	str = strcat_str(str, len, &tmplen, "</serviceStateTable></scpd>");
	str[*len] = '\0';
	return str;
}

/* genContentDirectory() :
 * Generate the ContentDirectory xml description */
char *
genContentDirectory(int * len)
{
	return genServiceDesc(len, &scpdContentDirectory);
}

/* genConnectionManager() :
 * Generate the ConnectionManager xml description */
char *
genConnectionManager(int * len)
{
	return genServiceDesc(len, &scpdConnectionManager);
}

/* genX_MS_MediaReceiverRegistrar() :
 * Generate the X_MS_MediaReceiverRegistrar xml description */
char *
genX_MS_MediaReceiverRegistrar(int * len)
{
	return genServiceDesc(len, &scpdX_MS_MediaReceiverRegistrar);
}

static char *
genEventVars(int * len, const struct serviceDesc * s, const char * servns)
{
	const struct stateVar * v;
	char * str;
	int tmplen;
	tmplen = 512;
	str = (char *)malloc(tmplen);
	if(str == NULL)
		return NULL;
	*len = 0;
	v = s->serviceStateTable;
	str = strcat_str(str, len, &tmplen, "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\" xmlns:s=\"");
	str = strcat_str(str, len, &tmplen, servns);
	str = strcat_str(str, len, &tmplen, "\">");
	while(v->name) {
		if(v->itype & 0x80) {
			str = strcat_str(str, len, &tmplen, "<e:property><");
			str = strcat_str(str, len, &tmplen, v->name);
			str = strcat_str(str, len, &tmplen, ">");
			//printf("<e:property><s:%s>", v->name);
			switch(v->ieventvalue) {
			case 0:
				break;
			case 255:	/* Magical values should go around here */
				break;
			default:
				str = strcat_str(str, len, &tmplen, upnpallowedvalues[v->ieventvalue]);
				//printf("%s", upnpallowedvalues[v->ieventvalue]);
			}
			str = strcat_str(str, len, &tmplen, "</");
			str = strcat_str(str, len, &tmplen, v->name);
			str = strcat_str(str, len, &tmplen, "></e:property>");
			//printf("</s:%s></e:property>\n", v->name);
		}
		v++;
	}
	str = strcat_str(str, len, &tmplen, "</e:propertyset>");
	//printf("</e:propertyset>\n");
	//printf("\n");
	//printf("%d\n", tmplen);
	str[*len] = '\0';
	return str;
}

char *
getVarsContentDirectory(int * l)
{
	return genEventVars(l,
                        &scpdContentDirectory,
	                    "urn:schemas-upnp-org:service:ContentDirectory:1");
}

char *
getVarsConnectionManager(int * l)
{
	return genEventVars(l,
                        &scpdConnectionManager,
	                    "urn:schemas-upnp-org:service:ConnectionManager:1");
}

char *
getVarsX_MS_MediaReceiverRegistrar(int * l)
{
	return genEventVars(l,
                        &scpdX_MS_MediaReceiverRegistrar,
	                    "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1");
}

