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
set_startup_time(int sysuptime)
{
	startup_time = time(NULL);
	if(sysuptime)
	{
		/* use system uptime instead of daemon uptime */
		char buff[64];
		int uptime, fd;
		fd = open("/proc/uptime", O_RDONLY);
		if(fd < 0)
		{
			DPRINTF(E_ERROR, L_GENERAL, "Error opening /proc/uptime: %s\n", strerror(errno));
		}
		else
		{
			memset(buff, 0, sizeof(buff));
			read(fd, buff, sizeof(buff) - 1);
			uptime = atoi(buff);
			DPRINTF(E_DEBUG, L_GENERAL, "system uptime is %d seconds\n", uptime);
			close(fd);
			startup_time -= uptime;
		}
	}
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
		fprintf(stderr, "Error parsing address/mask : %s\n", str);
		return -1;
	}
	memcpy(lan_addr->str, str, n);
	lan_addr->str[n] = '\0';
	if(!inet_aton(lan_addr->str, &lan_addr->addr))
	{
		fprintf(stderr, "Error parsing address/mask : %s\n", str);
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
		printf("No MAC addresses found!\n");
		strcpy(mac_str, "554e4b4e4f57");
	}
	strcpy(uuidvalue+5, "4d696e69-444c-164e-9d41-");
	strncat(uuidvalue, mac_str, 12);
	free(mac_str);

	getfriendlyname(friendly_name, FRIENDLYNAME_MAX_LEN);
	
	char ext_ip_addr[INET_ADDRSTRLEN];
	if( (getsysaddr(ext_ip_addr, INET_ADDRSTRLEN) < 0) &&
	    (getifaddr("eth0", ext_ip_addr, INET_ADDRSTRLEN) < 0) &&
	    (getifaddr("eth1", ext_ip_addr, INET_ADDRSTRLEN) < 0) )
	{
		printf("No IP!\n");
		return 1;
	}
	if( parselanaddr(&lan_addr[n_lan_addr], ext_ip_addr) == 0 )
		n_lan_addr++;
	runtime_vars.port = -1;
	runtime_vars.notify_interval = 30;	/* seconds between SSDP announces */

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
			case UPNPSYSTEM_UPTIME:
				if(strcmp(ary_options[i].value, "yes") == 0)
					SETFLAG(SYSUPTIMEMASK);	/*sysuptime = 1;*/
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
				usleep(1);
				enum media_types type = ALL_MEDIA;
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
					usleep(1);
					char * path = realpath(myval ? myval:ary_options[i].value, NULL);
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
				usleep(1);
				char *string, *word;
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
		case 'U':
			/*sysuptime = 1;*/
			SETFLAG(SYSUPTIMEMASK);
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
		case 'f':
			i++;	/* discarding, the config file is already read */
			break;
		default:
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
		}
	}
	if( (n_lan_addr==0) || (runtime_vars.port<=0) )
	{
		fprintf(stderr, "Usage:\n\t"
		        "%s [-f config_file] [-i ext_ifname] [-o ext_ip]\n"
				"\t\t[-a listening_ip] [-p port] [-d] [-L] [-U] [-S]\n"
				/*"[-l logfile] " not functionnal */
				"\t\t[-s serial] [-m model_number] \n"
				"\t\t[-t notify_interval] [-P pid_filename]\n"
				"\t\t[-B down up] [-w url]\n"
		        "\nNotes:\n\tThere can be one or several listening_ips.\n"
		        "\tNotify interval is in seconds. Default is 30 seconds.\n"
				"\tDefault pid file is %s.\n"
				"\tWith -d miniupnpd will run as a standard program.\n"
				"\t-L sets packet log in pf and ipf on.\n"
				"\t-S sets \"secure\" mode : clients can only add mappings to their own ip\n"
				"\t-U causes miniupnpd to report system uptime instead "
				"of daemon uptime.\n"
				"\t-B sets bitrates reported by daemon in bits per second.\n"
				"\t-w sets the presentation url. Default is http address on port 80\n"
		        "", argv[0], pidfilename);
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

	set_startup_time(GETFLAG(SYSUPTIMEMASK)/*sysuptime*/);

	/* presentation url */
	if(presurl)
	{
		strncpy(presentationurl, presurl, PRESENTATIONURL_MAX_LEN);
		presentationurl[PRESENTATIONURL_MAX_LEN-1] = '\0';
	}
	else
	{
		snprintf(presentationurl, PRESENTATIONURL_MAX_LEN,
		         "http://%s/", lan_addr[0].str);
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
	struct timeval timeout, timeofday, lasttimeofday = {0, 0}, lastupdatetime = {0, 0};
	int max_fd = -1;
	int last_changecnt = 0;
	#ifdef TIVO_SUPPORT
	unsigned short int loop_cnt = 0;
	int sbeacon = -1;
	struct sockaddr_in tivo_bcast;
	#endif
	char * sql;
	pthread_t thread[2];

	if(init(argc, argv) != 0)
		return 1;

	DPRINTF(E_WARN, L_GENERAL, "Starting MiniDLNA...\n");
	LIST_INIT(&upnphttphead);

	if( access(DB_PATH, F_OK) != 0 )
	{
		char *db_path = strdup(DB_PATH);
		make_dir(db_path, S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
		free(db_path);
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
		if( sql_get_table(db, "pragma user_version", &result, &rows, 0) == SQLITE_OK )
		{
			if( atoi(result[1]) != DB_VERSION ) {
				DPRINTF(E_WARN, L_GENERAL, "Database version mismatch; need to recreate...\n");
				sqlite3_close(db);
				unlink(DB_PATH "/files.db");
				system("rm -rf " DB_PATH "/art_cache");
				sqlite3_open(DB_PATH "/files.db", &db);
				sqlite3_busy_timeout(db, 5000);
				if( CreateDatabase() != 0 )
				{
					DPRINTF(E_FATAL, L_GENERAL, "ERROR: Failed to create sqlite database!  Exiting...\n");
				}
				if( pthread_create(&thread[0], NULL, start_scanner, NULL) )
				{
					DPRINTF(E_FATAL, L_GENERAL, "ERROR: pthread_create() failed for start_scanner.\n");
				}
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
		tivo_bcast.sin_port = htons( 2190 );
	}
	#endif

	SendSSDPGoodbye(snotify, n_lan_addr);

	/* main loop */
	while(!quitting)
	{
		/* Check if we need to send SSDP NOTIFY messages and do it if
		 * needed */
		/* Also check if we need to increment our SystemUpdateID
		 * at most once every 2 seconds */
		if(gettimeofday(&timeofday, 0) < 0)
		{
			DPRINTF(E_ERROR, L_GENERAL, "gettimeofday(): %s\n", strerror(errno));
			timeout.tv_sec = runtime_vars.notify_interval;
			timeout.tv_usec = 0;
		}
		else
		{
			/* the comparaison is not very precise but who cares ? */
			if(timeofday.tv_sec >= (lasttimeofday.tv_sec + runtime_vars.notify_interval))
			{
				SendSSDPNotifies2(snotify,
			                  (unsigned short)runtime_vars.port,
			                  (runtime_vars.notify_interval << 1)+10);
				memcpy(&lasttimeofday, &timeofday, sizeof(struct timeval));
				timeout.tv_sec = runtime_vars.notify_interval;
				timeout.tv_usec = 0;
			}
			else
			{
				timeout.tv_sec = lasttimeofday.tv_sec + runtime_vars.notify_interval
				                 - timeofday.tv_sec;
				if(timeofday.tv_usec > lasttimeofday.tv_usec)
				{
					timeout.tv_usec = 1000000 + lasttimeofday.tv_usec
					                  - timeofday.tv_usec;
					timeout.tv_sec--;
				}
				else
				{
					timeout.tv_usec = lasttimeofday.tv_usec - timeofday.tv_usec;
				}
			}
			if(timeofday.tv_sec >= (lastupdatetime.tv_sec + 2))
			{
				if( sqlite3_total_changes(db) != last_changecnt )
				{
					updateID++;
					last_changecnt = sqlite3_total_changes(db);
					upnp_event_var_change_notify(EContentDirectory);
				}
				#ifdef TIVO_SUPPORT
				if( GETFLAG(TIVOMASK) )
				{
					if( loop_cnt < 10 )
					{
   						sendBeaconMessage(sbeacon, &tivo_bcast, sizeof(struct sockaddr_in), 1);
						loop_cnt++;
					}
					else
					{
   						if( loop_cnt == 30 )
						{
							sendBeaconMessage(sbeacon, &tivo_bcast, sizeof(struct sockaddr_in), 1);
							loop_cnt = 10;
						}
						loop_cnt++;
					}
				}
				#endif
				memcpy(&lastupdatetime, &timeofday, sizeof(struct timeval));
			}
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
		/* process active HTTP connections */
		/* LIST_FOREACH macro is not available under linux */
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

	if(unlink(pidfilename) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "Failed to remove pidfile %s: %s\n", pidfilename, strerror(errno));
	}

	freeoptions();
	
	return 0;
}

