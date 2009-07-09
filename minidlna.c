/* MiniDLNA project
 *
 * http://sourceforge.net/projects/minidlna/
 * (c) 2008-2009 Justin Maggard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution
 *
 * Portions of the code (c) Thomas Bernard, subject to
 * the conditions detailed in the LICENSE.miniupnpd file.
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <sys/param.h>
#include <errno.h>
#include <pthread.h>

/* unix sockets */
#include "config.h"

#include "upnpglobalvars.h"
#include "sql.h"
#include "upnphttp.h"
#include "upnpdescgen.h"
#include "minidlnapath.h"
#include "getifaddr.h"
#include "upnpsoap.h"
#include "options.h"
#include "utils.h"
#include "minissdp.h"
#include "minidlnatypes.h"
#include "daemonize.h"
#include "upnpevents.h"
#include "scanner.h"
#include "inotify.h"
#include "commonrdr.h"
#include "log.h"
#ifdef TIVO_SUPPORT
#include "tivo_beacon.h"
#include "tivo_utils.h"
#endif

/* MAX_LAN_ADDR : maximum number of interfaces
 * to listen to SSDP traffic */
/*#define MAX_LAN_ADDR (4)*/

static volatile int quitting = 0;

/* OpenAndConfHTTPSocket() :
 * setup the socket used to handle incoming HTTP connections. */
static int
OpenAndConfHTTPSocket(unsigned short port)
{
	int s;
	int i = 1;
	struct sockaddr_in listenname;

	/* Initialize client type cache */
	memset(&clients, 0, sizeof(struct client_cache_s));

	if( (s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "socket(http): %s\n", strerror(errno));
		return -1;
	}

	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
	{
		DPRINTF(E_WARN, L_GENERAL, "setsockopt(http, SO_REUSEADDR): %s\n", strerror(errno));
	}

	memset(&listenname, 0, sizeof(struct sockaddr_in));
	listenname.sin_family = AF_INET;
	listenname.sin_port = htons(port);
	listenname.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(s, (struct sockaddr *)&listenname, sizeof(struct sockaddr_in)) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "bind(http): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	if(listen(s, 6) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "listen(http): %s\n", strerror(errno));
		close(s);
		return -1;
	}

	return s;
}

/* Handler for the SIGTERM signal (kill) 
 * SIGINT is also handled */
static void
sigterm(int sig)
{
	/*int save_errno = errno;*/
	signal(sig, SIG_IGN);	/* Ignore this signal while we are quitting */

	DPRINTF(E_WARN, L_GENERAL, "received signal %d, good-bye\n", sig);

	quitting = 1;
	/*errno = save_errno;*/
}

/* record the startup time, for returning uptime */
static void
set_startup_time(void)
{
	startup_time = time(NULL);
}

/* parselanaddr()
 * parse address with mask
 * ex: 192.168.1.1/24
 * return value : 
 *    0 : ok
 *   -1 : error */
static int
parselanaddr(struct lan_addr_s * lan_addr, const char * str)
{
	const char * p;
	int nbits = 24;
	int n;
	p = str;
	while(*p && *p != '/' && !isspace(*p))
		p++;
	n = p - str;
	if(*p == '/')
	{
		nbits = atoi(++p);
		while(*p && !isspace(*p))
			p++;
	}
	if(n>15)
	{
		DPRINTF(E_OFF, L_GENERAL, "Error parsing address/mask: %s\n", str);
		return -1;
	}
	memcpy(lan_addr->str, str, n);
	lan_addr->str[n] = '\0';
	if(!inet_aton(lan_addr->str, &lan_addr->addr))
	{
		DPRINTF(E_OFF, L_GENERAL, "Error parsing address/mask: %s\n", str);
		return -1;
	}
	lan_addr->mask.s_addr = htonl(nbits ? (0xffffffff << (32 - nbits)) : 0);
	return 0;
}

void
getfriendlyname(char * buf, int len)
{
	char * dot = NULL;
	char * hn = calloc(1, 256);
	if( gethostname(hn, 256) == 0 )
	{
		strncpy(buf, hn, len-1);
		buf[len] = '\0';
		dot = index(buf, '.');
		if( dot )
			*dot = '\0';
	}
	else
	{
		strcpy(buf, "Unknown");
	}
	free(hn);
	strcat(buf, ": ");
	#ifdef READYNAS
	strncat(buf, "ReadyNAS", len-strlen(buf)-1);
	#else
	strncat(buf, getenv("LOGNAME"), len-strlen(buf)-1);
	#endif
}

/* init phase :
 * 1) read configuration file
 * 2) read command line arguments
 * 3) daemonize
 * 4) check and write pid file
 * 5) set startup time stamp
 * 6) compute presentation URL
 * 7) set signal handlers */
static int
init(int argc, char * * argv)
{
	int i;
	int pid;
	int debug_flag = 0;
	int options_flag = 0;
	struct sigaction sa;
	/*const char * logfilename = 0;*/
	const char * presurl = 0;
	const char * optionsfile = "/etc/minidlna.conf";
	char * mac_str = calloc(1, 64);
	char * string, * word;
	enum media_types type;
	char * path;
	char ext_ip_addr[INET_ADDRSTRLEN] = {'\0'};

	/* first check if "-f" option is used */
	for(i=2; i<argc; i++)
	{
		if(0 == strcmp(argv[i-1], "-f"))
		{
			optionsfile = argv[i];
			options_flag = 1;
			break;
		}
	}

	/* set up uuid based on mac address */
	if( (getifhwaddr("eth0", mac_str, 64) < 0) &&
	    (getifhwaddr("eth1", mac_str, 64) < 0) )
	{
		DPRINTF(E_OFF, L_GENERAL, "No MAC address found.  Falling back to generic UUID.\n");
		strcpy(mac_str, "554e4b4e4f57");
	}
	strcpy(uuidvalue+5, "4d696e69-444c-164e-9d41-");
	strncat(uuidvalue, mac_str, 12);
	free(mac_str);

	getfriendlyname(friendly_name, FRIENDLYNAME_MAX_LEN);
	
	runtime_vars.port = -1;
	runtime_vars.notify_interval = 895;	/* seconds between SSDP announces */

	/* read options file first since
	 * command line arguments have final say */
	if(readoptionsfile(optionsfile) < 0)
	{
		/* only error if file exists or using -f */
		if(access(optionsfile, F_OK) == 0 || options_flag)
			fprintf(stderr, "Error reading configuration file %s\n", optionsfile);
	}
	else
	{
		for(i=0; i<num_options; i++)
		{
			switch(ary_options[i].id)
			{
			case UPNPIFNAME:
				if(getifaddr(ary_options[i].value, ext_ip_addr, INET_ADDRSTRLEN) >= 0)
				{
					if( *ext_ip_addr && parselanaddr(&lan_addr[n_lan_addr], ext_ip_addr) == 0 )
						n_lan_addr++;
				}
				else
					fprintf(stderr, "Interface %s not found, ignoring.\n", ary_options[i].value);
				break;
			case UPNPLISTENING_IP:
				if(n_lan_addr < MAX_LAN_ADDR)
				{
					if(parselanaddr(&lan_addr[n_lan_addr],
					             ary_options[i].value) == 0)
						n_lan_addr++;
				}
				else
				{
					fprintf(stderr, "Too many listening ips (max: %d), ignoring %s\n",
			    		    MAX_LAN_ADDR, ary_options[i].value);
				}
				break;
			case UPNPPORT:
				runtime_vars.port = atoi(ary_options[i].value);
				break;
			case UPNPPRESENTATIONURL:
				presurl = ary_options[i].value;
				break;
			case UPNPNOTIFY_INTERVAL:
				runtime_vars.notify_interval = atoi(ary_options[i].value);
				break;
			case UPNPSERIAL:
				strncpy(serialnumber, ary_options[i].value, SERIALNUMBER_MAX_LEN);
				serialnumber[SERIALNUMBER_MAX_LEN-1] = '\0';
				break;				
			case UPNPMODEL_NUMBER:
				strncpy(modelnumber, ary_options[i].value, MODELNUMBER_MAX_LEN);
				modelnumber[MODELNUMBER_MAX_LEN-1] = '\0';
				break;
			case UPNPFRIENDLYNAME:
				strncpy(friendly_name, ary_options[i].value, FRIENDLYNAME_MAX_LEN);
				friendly_name[FRIENDLYNAME_MAX_LEN-1] = '\0';
				break;
			case UPNPMEDIADIR:
				type = ALL_MEDIA;
				char * myval = NULL;
				switch( ary_options[i].value[0] )
				{
				case 'A':
				case 'a':
					if( ary_options[i].value[0] == 'A' || ary_options[i].value[0] == 'a' )
						type = AUDIO_ONLY;
				case 'V':
				case 'v':
					if( ary_options[i].value[0] == 'V' || ary_options[i].value[0] == 'v' )
						type = VIDEO_ONLY;
				case 'P':
				case 'p':
					if( ary_options[i].value[0] == 'P' || ary_options[i].value[0] == 'p' )
						type = IMAGES_ONLY;
					myval = index(ary_options[i].value, '/');
				case '/':
					path = realpath(myval ? myval:ary_options[i].value, NULL);
					if( !path )
						path = strdup(myval ? myval:ary_options[i].value);
					if( access(path, F_OK) != 0 )
					{
						fprintf(stderr, "Media directory not accessible! [%s]\n",
						        path);
						free(path);
						break;
					}
					struct media_dir_s * this_dir = calloc(1, sizeof(struct media_dir_s));
					this_dir->path = path;
					this_dir->type = type;
					if( !media_dirs )
					{
						media_dirs = this_dir;
					}
					else
					{
						struct media_dir_s * all_dirs = media_dirs;
						while( all_dirs->next )
							all_dirs = all_dirs->next;
						all_dirs->next = this_dir;
					}
					break;
				default:
					fprintf(stderr, "Media directory entry not understood! [%s]\n",
					        ary_options[i].value);
					break;
				}
				break;
			case UPNPALBUMART_NAMES:
				for( string = ary_options[i].value; (word = strtok(string, "/")); string = NULL ) {
					struct album_art_name_s * this_name = calloc(1, sizeof(struct album_art_name_s));
					this_name->name = strdup(word);
					if( !album_art_names )
					{
						album_art_names = this_name;
					}
					else
					{
						struct album_art_name_s * all_names = album_art_names;
						while( all_names->next )
							all_names = all_names->next;
						all_names->next = this_name;
					}
				}
				break;
			case UPNPINOTIFY:
				if( (strcmp(ary_options[i].value, "yes") != 0) && !atoi(ary_options[i].value) )
					CLEARFLAG(INOTIFYMASK);
				break;
			case ENABLE_TIVO:
				if( (strcmp(ary_options[i].value, "yes") == 0) || atoi(ary_options[i].value) )
					SETFLAG(TIVOMASK);
				break;
			default:
				fprintf(stderr, "Unknown option in file %s\n",
				        optionsfile);
			}
		}
	}

	/* command line arguments processing */
	for(i=1; i<argc; i++)
	{
		if(argv[i][0]!='-')
		{
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
		}
		else if(strcmp(argv[i], "--help")==0)
		{
			runtime_vars.port = 0;
			break;
		}
		else switch(argv[i][1])
		{
		case 't':
			if(i+1 < argc)
				runtime_vars.notify_interval = atoi(argv[++i]);
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 's':
			if(i+1 < argc)
				strncpy(serialnumber, argv[++i], SERIALNUMBER_MAX_LEN);
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			serialnumber[SERIALNUMBER_MAX_LEN-1] = '\0';
			break;
		case 'm':
			if(i+1 < argc)
				strncpy(modelnumber, argv[++i], MODELNUMBER_MAX_LEN);
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			modelnumber[MODELNUMBER_MAX_LEN-1] = '\0';
			break;
		/*case 'l':
			logfilename = argv[++i];
			break;*/
		case 'p':
			if(i+1 < argc)
				runtime_vars.port = atoi(argv[++i]);
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'P':
			if(i+1 < argc)
				pidfilename = argv[++i];
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'd':
			debug_flag = 1;
			break;
		case 'w':
			if(i+1 < argc)
				presurl = argv[++i];
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'a':
			if(i+1 < argc)
			{
				int address_already_there = 0;
				int j;
				i++;
				for(j=0; j<n_lan_addr; j++)
				{
					struct lan_addr_s tmpaddr;
					parselanaddr(&tmpaddr, argv[i]);
					if(0 == strcmp(lan_addr[j].str, tmpaddr.str))
						address_already_there = 1;
				}
				if(address_already_there)
					break;
				if(n_lan_addr < MAX_LAN_ADDR)
				{
					if(parselanaddr(&lan_addr[n_lan_addr], argv[i]) == 0)
						n_lan_addr++;
				}
				else
				{
					fprintf(stderr, "Too many listening ips (max: %d), ignoring %s\n",
				    	    MAX_LAN_ADDR, argv[i]);
				}
			}
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'i':
			if(i+1 < argc)
			{
				int address_already_there = 0;
				int j;
				i++;
				if( getifaddr(argv[i], ext_ip_addr, INET_ADDRSTRLEN) < 0 )
				{
					fprintf(stderr, "Network interface '%s' not found.\n",
						argv[i]);
					exit(-1);
				}
				for(j=0; j<n_lan_addr; j++)
				{
					struct lan_addr_s tmpaddr;
					parselanaddr(&tmpaddr, ext_ip_addr);
					if(0 == strcmp(lan_addr[j].str, tmpaddr.str))
						address_already_there = 1;
				}
				if(address_already_there)
					break;
				if(n_lan_addr < MAX_LAN_ADDR)
				{
					if(parselanaddr(&lan_addr[n_lan_addr], ext_ip_addr) == 0)
						n_lan_addr++;
				}
				else
				{
					fprintf(stderr, "Too many listening ips (max: %d), ignoring %s\n",
				    	    MAX_LAN_ADDR, argv[i]);
				}
			}
			else
				fprintf(stderr, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'f':
			i++;	/* discarding, the config file is already read */
			break;
		case 'h':
			runtime_vars.port = 0; // triggers help display
			break;
		case 'R':
			system("rm -rf " DB_PATH); // triggers a full rescan
			break;
		case 'V':
			printf("Version " MINIDLNA_VERSION "\n");
			exit(0);
			break;
		default:
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
		}
	}
	/* If no IP was specified, try to detect one */
	if( n_lan_addr < 1 )
	{
		if( (getsysaddr(ext_ip_addr, INET_ADDRSTRLEN) < 0) &&
		    (getifaddr("eth0", ext_ip_addr, INET_ADDRSTRLEN) < 0) &&
		    (getifaddr("eth1", ext_ip_addr, INET_ADDRSTRLEN) < 0) )
		{
			DPRINTF(E_OFF, L_GENERAL, "No IP address automatically detected!\n");
		}
		if( *ext_ip_addr && parselanaddr(&lan_addr[n_lan_addr], ext_ip_addr) == 0 )
		{
			n_lan_addr++;
		}
	}

	if( (n_lan_addr==0) || (runtime_vars.port<=0) )
	{
		fprintf(stderr, "Usage:\n\t"
		        "%s [-d] [-f config_file]\n"
			"\t\t[-a listening_ip] [-p port]\n"
			/*"[-l logfile] " not functionnal */
			"\t\t[-s serial] [-m model_number] \n"
			"\t\t[-t notify_interval] [-P pid_filename]\n"
			"\t\t[-w url] [-R] [-V] [-h]\n"
		        "\nNotes:\n\tNotify interval is in seconds. Default is 895 seconds.\n"
			"\tDefault pid file is %s.\n"
			"\tWith -d minidlna will run in debug mode (not daemonize).\n"
			"\t-w sets the presentation url. Default is http address on port 80\n"
			"\t-h displays this text\n"
			"\t-R forces a full rescan\n"
			"\t-V print the version number\n",
		        argv[0], pidfilename);
		return 1;
	}

	if(debug_flag)
	{
		pid = getpid();
		log_init(NULL, "general,artwork,database,inotify,scanner,metadata,http,ssdp,tivo=debug");
	}
	else
	{
#ifdef USE_DAEMON
		if(daemon(0, 0)<0) {
			perror("daemon()");
		}
		pid = getpid();
#else
		pid = daemonize();
#endif
		#ifdef READYNAS
		log_init("/var/log/upnp-av.log", "general,artwork,database,inotify,scanner,metadata,http,ssdp,tivo=warn");
		#else
		log_init(DB_PATH "/minidlna.log", "general,artwork,database,inotify,scanner,metadata,http,ssdp,tivo=warn");
		#endif
	}

	if(checkforrunning(pidfilename) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "MiniDLNA is already running. EXITING.\n");
		return 1;
	}	

	set_startup_time();

	/* presentation url */
	if(presurl)
	{
		strncpy(presentationurl, presurl, PRESENTATIONURL_MAX_LEN);
		presentationurl[PRESENTATIONURL_MAX_LEN-1] = '\0';
	}
	else
	{
#ifdef READYNAS
		snprintf(presentationurl, PRESENTATIONURL_MAX_LEN,
		         "https://%s/admin/", lan_addr[0].str);
#else
		snprintf(presentationurl, PRESENTATIONURL_MAX_LEN,
		         "http://%s/", lan_addr[0].str);
#endif
	}

	/* set signal handler */
	signal(SIGCLD, SIG_IGN);
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sigterm;
	if (sigaction(SIGTERM, &sa, NULL))
	{
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set SIGTERM handler. EXITING.\n");
	}
	if (sigaction(SIGINT, &sa, NULL))
	{
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set SIGINT handler. EXITING.\n");
	}

	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		DPRINTF(E_FATAL, L_GENERAL, "Failed to ignore SIGPIPE signals. EXITING.\n");
	}

	writepidfile(pidfilename, pid);

	return 0;
}

/* === main === */
/* process HTTP or SSDP requests */
int
main(int argc, char * * argv)
{
	int i;
	int sudp = -1, shttpl = -1;
	int snotify[MAX_LAN_ADDR];
	LIST_HEAD(httplisthead, upnphttp) upnphttphead;
	struct upnphttp * e = 0;
	struct upnphttp * next;
	fd_set readset;	/* for select() */
	fd_set writeset;
	struct timeval timeout, timeofday, lastnotifytime = {0, 0}, lastupdatetime = {0, 0};
	int max_fd = -1;
	int last_changecnt = 0;
	char * sql;
	short int new_db = 0;
	pthread_t thread[2];
#ifdef TIVO_SUPPORT
	unsigned short int beacon_interval = 5;
	int sbeacon = -1;
	struct sockaddr_in tivo_bcast;
	struct timeval lastbeacontime = {0, 0};
#endif

	if(init(argc, argv) != 0)
		return 1;

#ifdef READYNAS
	DPRINTF(E_WARN, L_GENERAL, "Starting ReadyDLNA version " MINIDLNA_VERSION ".\n");
	unlink("/ramfs/.upnp-av_scan");
#else
	DPRINTF(E_WARN, L_GENERAL, "Starting MiniDLNA version " MINIDLNA_VERSION " [SQLite %s].\n", sqlite3_libversion());
#endif
	LIST_INIT(&upnphttphead);

	if( access(DB_PATH, F_OK) != 0 )
	{
		char *db_path = strdup(DB_PATH);
		make_dir(db_path, S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
		free(db_path);
		new_db = 1;
	}
	if( sqlite3_open(DB_PATH "/files.db", &db) != SQLITE_OK )
	{
		DPRINTF(E_FATAL, L_GENERAL, "ERROR: Failed to open sqlite database!  Exiting...\n");
	}
	else
	{
		char **result;
		int rows;
		sqlite3_busy_timeout(db, 5000);
		if( !new_db && (sql_get_table(db, "SELECT UPDATE_ID from SETTINGS", &result, &rows, 0) == SQLITE_OK) )
		{
			if( rows )
			{
				updateID = atoi(result[1]);
			}
			sqlite3_free_table(result);
		}
		if( sql_get_table(db, "pragma user_version", &result, &rows, 0) == SQLITE_OK )
		{
			if( atoi(result[1]) != DB_VERSION ) {
				if( new_db )
				{
					DPRINTF(E_WARN, L_GENERAL, "Creating new database...\n");
				}
				else
				{
					DPRINTF(E_WARN, L_GENERAL, "Database version mismatch; need to recreate...\n");
				}
				sqlite3_close(db);
				unlink(DB_PATH "/files.db");
				system("rm -rf " DB_PATH "/art_cache");
				sqlite3_open(DB_PATH "/files.db", &db);
				sqlite3_busy_timeout(db, 5000);
				if( CreateDatabase() != 0 )
				{
					DPRINTF(E_FATAL, L_GENERAL, "ERROR: Failed to create sqlite database!  Exiting...\n");
				}
				scanning = 1;
				#if USE_FORK
				if( pthread_create(&thread[0], NULL, start_scanner, NULL) )
				{
					DPRINTF(E_FATAL, L_GENERAL, "ERROR: pthread_create() failed for start_scanner.\n");
				}
				#else
				start_scanner();
				#endif
			}
			sqlite3_free_table(result);
		}
		if( GETFLAG(INOTIFYMASK) && pthread_create(&thread[1], NULL, start_inotify, NULL) )
		{
			DPRINTF(E_FATAL, L_GENERAL, "ERROR: pthread_create() failed for start_inotify.\n");
		}
	}

	sudp = OpenAndConfSSDPReceiveSocket(n_lan_addr, lan_addr);
	if(sudp < 0)
	{
		DPRINTF(E_FATAL, L_GENERAL, "Failed to open socket for receiving SSDP. EXITING\n");
	}
	/* open socket for HTTP connections. Listen on the 1st LAN address */
	shttpl = OpenAndConfHTTPSocket(runtime_vars.port);
	if(shttpl < 0)
	{
		DPRINTF(E_FATAL, L_GENERAL, "Failed to open socket for HTTP. EXITING\n");
	}
	DPRINTF(E_WARN, L_GENERAL, "HTTP listening on port %d\n", runtime_vars.port);

	/* open socket for sending notifications */
	if(OpenAndConfSSDPNotifySockets(snotify) < 0)
	{
		DPRINTF(E_FATAL, L_GENERAL, "Failed to open sockets for sending SSDP notify "
	                "messages. EXITING\n");
	}

#ifdef TIVO_SUPPORT
	if( GETFLAG(TIVOMASK) )
	{
		DPRINTF(E_WARN, L_GENERAL, "TiVo support is enabled.\n");
		/* Add TiVo-specific randomize function to sqlite */
		if( sqlite3_create_function(db, "tivorandom", 1, SQLITE_UTF8, NULL, &TiVoRandomSeedFunc, NULL, NULL) != SQLITE_OK )
		{
			DPRINTF(E_ERROR, L_TIVO, "ERROR: Failed to add sqlite randomize function for TiVo!\n");
		}
		/* open socket for sending Tivo notifications */
		sbeacon = OpenAndConfTivoBeaconSocket();
		if(sbeacon < 0)
		{
			DPRINTF(E_FATAL, L_GENERAL, "Failed to open sockets for sending Tivo beacon notify "
		                "messages. EXITING\n");
		}
		tivo_bcast.sin_family = AF_INET;
		tivo_bcast.sin_addr.s_addr = htonl(getBcastAddress());
		tivo_bcast.sin_port = htons(2190);
	}
	else
	{
		sbeacon = -1;
	}
#endif

	SendSSDPGoodbye(snotify, n_lan_addr);

	/* main loop */
	while(!quitting)
	{
		/* Check if we need to send SSDP NOTIFY messages and do it if
		 * needed */
		if(gettimeofday(&timeofday, 0) < 0)
		{
			DPRINTF(E_ERROR, L_GENERAL, "gettimeofday(): %s\n", strerror(errno));
			timeout.tv_sec = runtime_vars.notify_interval;
			timeout.tv_usec = 0;
		}
		else
		{
			/* the comparaison is not very precise but who cares ? */
			if(timeofday.tv_sec >= (lastnotifytime.tv_sec + runtime_vars.notify_interval))
			{
				SendSSDPNotifies2(snotify,
			                  (unsigned short)runtime_vars.port,
			                  (runtime_vars.notify_interval << 1)+10);
				memcpy(&lastnotifytime, &timeofday, sizeof(struct timeval));
				timeout.tv_sec = runtime_vars.notify_interval;
				timeout.tv_usec = 0;
			}
			else
			{
				timeout.tv_sec = lastnotifytime.tv_sec + runtime_vars.notify_interval
				                 - timeofday.tv_sec;
				if(timeofday.tv_usec > lastnotifytime.tv_usec)
				{
					timeout.tv_usec = 1000000 + lastnotifytime.tv_usec
					                  - timeofday.tv_usec;
					timeout.tv_sec--;
				}
				else
				{
					timeout.tv_usec = lastnotifytime.tv_usec - timeofday.tv_usec;
				}
			}
#ifdef TIVO_SUPPORT
			if( GETFLAG(TIVOMASK) )
			{
				if(timeofday.tv_sec >= (lastbeacontime.tv_sec + beacon_interval))
				{
   					sendBeaconMessage(sbeacon, &tivo_bcast, sizeof(struct sockaddr_in), 1);
					memcpy(&lastbeacontime, &timeofday, sizeof(struct timeval));
					if( timeout.tv_sec > beacon_interval )
					{
						timeout.tv_sec = beacon_interval;
						timeout.tv_usec = 0;
					}
					/* Beacons should be sent every 5 seconds or so for the first minute,
					 * then every minute or so thereafter. */
					if( beacon_interval == 5 && (timeofday.tv_sec - startup_time) > 60 )
					{
						beacon_interval = 60;
					}
				}
				else if( timeout.tv_sec > (lastbeacontime.tv_sec + beacon_interval + 1 - timeofday.tv_sec) )
				{
					timeout.tv_sec = lastbeacontime.tv_sec + beacon_interval - timeofday.tv_sec;
				}
			}
#endif
		}

		/* select open sockets (SSDP, HTTP listen, and all HTTP soap sockets) */
		FD_ZERO(&readset);

		if (sudp >= 0) 
		{
			FD_SET(sudp, &readset);
			max_fd = MAX( max_fd, sudp);
		}
		
		if (shttpl >= 0) 
		{
			FD_SET(shttpl, &readset);
			max_fd = MAX( max_fd, shttpl);
		}

		i = 0;	/* active HTTP connections count */
		for(e = upnphttphead.lh_first; e != NULL; e = e->entries.le_next)
		{
			if((e->socket >= 0) && (e->state <= 2))
			{
				FD_SET(e->socket, &readset);
				max_fd = MAX( max_fd, e->socket);
				i++;
			}
		}
		/* for debug */
#ifdef DEBUG
		if(i > 1)
		{
			DPRINTF(E_DEBUG, L_GENERAL, "%d active incoming HTTP connections\n", i);
		}
#endif

		FD_ZERO(&writeset);
		upnpevents_selectfds(&readset, &writeset, &max_fd);

		if(select(max_fd+1, &readset, &writeset, 0, &timeout) < 0)
		{
			if(quitting) goto shutdown;
			DPRINTF(E_ERROR, L_GENERAL, "select(all): %s\n", strerror(errno));
			DPRINTF(E_FATAL, L_GENERAL, "Failed to select open sockets. EXITING\n");
		}
		upnpevents_processfds(&readset, &writeset);
		/* process SSDP packets */
		if(sudp >= 0 && FD_ISSET(sudp, &readset))
		{
			/*DPRINTF(E_DEBUG, L_GENERAL, "Received UDP Packet\n");*/
			ProcessSSDPRequest(sudp, (unsigned short)runtime_vars.port);
		}
		/* increment SystemUpdateID if the content database has changed,
		 * and if there is an active HTTP connection, at most once every 2 seconds */
		if( i && (time(NULL) >= (lastupdatetime.tv_sec + 2)) )
		{
			if( sqlite3_total_changes(db) != last_changecnt )
			{
				updateID++;
				last_changecnt = sqlite3_total_changes(db);
				upnp_event_var_change_notify(EContentDirectory);
				memcpy(&lastupdatetime, &timeofday, sizeof(struct timeval));
			}
		}
		/* process active HTTP connections */
		for(e = upnphttphead.lh_first; e != NULL; e = e->entries.le_next)
		{
			if(  (e->socket >= 0) && (e->state <= 2)
				&&(FD_ISSET(e->socket, &readset)) )
			{
				Process_upnphttp(e);
			}
		}
		/* process incoming HTTP connections */
		if(shttpl >= 0 && FD_ISSET(shttpl, &readset))
		{
			int shttp;
			socklen_t clientnamelen;
			struct sockaddr_in clientname;
			clientnamelen = sizeof(struct sockaddr_in);
			shttp = accept(shttpl, (struct sockaddr *)&clientname, &clientnamelen);
			if(shttp<0)
			{
				DPRINTF(E_ERROR, L_GENERAL, "accept(http): %s\n", strerror(errno));
			}
			else
			{
				struct upnphttp * tmp = 0;
				DPRINTF(E_DEBUG, L_GENERAL, "HTTP connection from %s:%d\n",
					inet_ntoa(clientname.sin_addr),
					ntohs(clientname.sin_port) );
				/*if (fcntl(shttp, F_SETFL, O_NONBLOCK) < 0) {
					DPRINTF(E_ERROR, L_GENERAL, "fcntl F_SETFL, O_NONBLOCK");
				}*/
				/* Create a new upnphttp object and add it to
				 * the active upnphttp object list */
				tmp = New_upnphttp(shttp);
				if(tmp)
				{
					tmp->clientaddr = clientname.sin_addr;
					LIST_INSERT_HEAD(&upnphttphead, tmp, entries);
				}
				else
				{
					DPRINTF(E_ERROR, L_GENERAL, "New_upnphttp() failed\n");
					close(shttp);
				}
			}
		}
		/* delete finished HTTP connections */
		for(e = upnphttphead.lh_first; e != NULL; )
		{
			next = e->entries.le_next;
			if(e->state >= 100)
			{
				LIST_REMOVE(e, entries);
				Delete_upnphttp(e);
			}
			e = next;
		}
	}

shutdown:
	/* close out open sockets */
	while(upnphttphead.lh_first != NULL)
	{
		e = upnphttphead.lh_first;
		LIST_REMOVE(e, entries);
		Delete_upnphttp(e);
	}

	if (sudp >= 0) close(sudp);
	if (shttpl >= 0) close(shttpl);
	#ifdef TIVO_SUPPORT
	if (sbeacon >= 0) close(sbeacon);
	#endif
	
	if(SendSSDPGoodbye(snotify, n_lan_addr) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "Failed to broadcast good-bye notifications\n");
	}
	for(i=0; i<n_lan_addr; i++)
		close(snotify[i]);

	asprintf(&sql, "UPDATE SETTINGS set UPDATE_ID = %u", updateID);
	sql_exec(db, sql);
	free(sql);
	sqlite3_close(db);

	struct media_dir_s * media_path = media_dirs;
	while( media_path )
	{
		free(media_path->path);
		media_path = media_path->next;
	}
	struct album_art_name_s * art_names = album_art_names;
	while( art_names )
	{
		free(art_names->name);
		art_names = art_names->next;
	}

	if(unlink(pidfilename) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "Failed to remove pidfile %s: %s\n", pidfilename, strerror(errno));
	}

	freeoptions();
	
	return 0;
}

