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
#include <sys/types.h>
#include <netinet/in.h>
#include <linux/limits.h>

#include "config.h"
#include "upnpglobalvars.h"

/* LAN address */
/*const char * listen_addr = 0;*/

/* startup time */
time_t startup_time = 0;

struct runtime_vars_s runtime_vars;
int runtime_flags = INOTIFY_MASK;

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
char dlna_no_conv[] = "DLNA.ORG_OP=01;DLNA.ORG_CI=0";
char friendly_name[FRIENDLYNAME_MAX_LEN];
char db_path[PATH_MAX] = DEFAULT_DB_PATH;
struct media_dir_s * media_dirs = NULL;
struct album_art_name_s * album_art_names = NULL;
struct client_cache_s clients[CLIENT_CACHE_SLOTS];
short int scanning = 0;
volatile short int quitting = 0;
volatile __u32 updateID = 0;
