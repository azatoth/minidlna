/* $Id$ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard 
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#include <sys/types.h>
#include <netinet/in.h>

#include "config.h"
#include "upnpglobalvars.h"

/* LAN address */
/*const char * listen_addr = 0;*/

/* startup time */
time_t startup_time = 0;

struct runtime_vars_s runtime_vars;
int runtime_flags = INOTIFYMASK;

const char * pidfilename = "/var/run/minidlna.pid";

char uuidvalue[] = "uuid:00000000-0000-0000-0000-000000000000";
char serialnumber[SERIALNUMBER_MAX_LEN] = "00000000";

char modelnumber[MODELNUMBER_MAX_LEN] = "1";

/* presentation url :
 * http://nnn.nnn.nnn.nnn:ppppp/  => max 30 bytes including terminating 0 */
char presentationurl[PRESENTATIONURL_MAX_LEN];

int n_lan_addr = 0;
struct lan_addr_s lan_addr[MAX_LAN_ADDR];

/* UPnP-A/V [DLNA] */
sqlite3 * db;
char friendly_name[FRIENDLYNAME_MAX_LEN];
struct media_dir_s * media_dirs = NULL;
struct album_art_name_s * album_art_names = NULL;
__u32 updateID = 0;
