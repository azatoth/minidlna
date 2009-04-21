#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#ifdef HAVE_INOTIFY_H
#include <sys/inotify.h>
#else
#include "linux/inotify.h"
#include "linux/inotify-syscalls.h"
#endif

#include "upnpglobalvars.h"
#include "utils.h"
#include "sql.h"
#include "scanner.h"
#include "log.h"

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )
#define DESIRED_WATCH_LIMIT 65536

#define PATH_BUF_SIZE PATH_MAX

struct watch
{
	int wd;		/* watch descriptor */
	char *path;	/* watched path */
	struct watch *next;
	struct watch *prev;
};

static struct watch *watches;
static struct watch *lastwatch = NULL;

char *get_path_from_wd(int wd)
{
	struct watch *w = watches;

	while( w != NULL )
	{
		if( w->wd == wd )
			return w->path;
		w = w->next;
	}

	return NULL;
}

int
add_watch(int fd, const char * path)
{
	struct watch *nw;
	int wd;

	wd = inotify_add_watch(fd, path, IN_CREATE|IN_CLOSE_WRITE|IN_DELETE|IN_MOVE);
	if( wd < 0 )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "inotify_add_watch() [%s]\n", strerror(errno));
		return -1;
	}

	nw = malloc(sizeof(struct watch));
	if( nw == NULL )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "malloc()\n");
		return -1;
	}
	nw->wd = wd;
	nw->prev = lastwatch;
	nw->next = NULL;
	nw->path = strdup(path);

	if( watches == NULL )
	{
		watches = nw;
	}

	if( lastwatch != NULL )
	{
		lastwatch->next = nw;
	}
	lastwatch = nw;

	return wd;
}

int
remove_watch(int fd, const char * path)
{
	struct watch *w;

	for( w = watches; w; w = w->next )
	{
		if( strcmp(path, w->path) == 0 )
			return(inotify_rm_watch(fd, w->wd));
	}

	return 1;
}

unsigned int
next_highest(unsigned int num)
{
	num |= num >> 1;
	num |= num >> 2;
	num |= num >> 4;
	num |= num >> 8;
	num |= num >> 16;
	return(++num);
}

int
inotify_create_watches(int fd)
{
	FILE * max_watches;
	unsigned int num_watches = 0, watch_limit = 8192;
	char **result;
	int i, rows = 0;
	struct media_dir_s * media_path;

	if( sql_get_table(db, "SELECT count(ID) from DETAILS where SIZE is NULL and PATH is not NULL", &result, &rows, NULL) == SQLITE_OK )
	{
		if( rows )
		{
			num_watches = strtoul(result[1], NULL, 10);
		}
		sqlite3_free_table(result);
	}
		
	max_watches = fopen("/proc/sys/fs/inotify/max_user_watches", "r");
	if( max_watches )
	{
		fscanf(max_watches, "%u", &watch_limit);
		fclose(max_watches);
		if( (watch_limit < DESIRED_WATCH_LIMIT) || (watch_limit < (num_watches*3/4)) )
		{
			max_watches = fopen("/proc/sys/fs/inotify/max_user_watches", "w");
			if( max_watches )
			{
				if( DESIRED_WATCH_LIMIT >= (num_watches*3/4) )
				{
					fprintf(max_watches, "%u", DESIRED_WATCH_LIMIT);
				}
				else if( next_highest(num_watches) >= (num_watches*3/4) )
				{
					fprintf(max_watches, "%u", next_highest(num_watches));
				}
				else
				{
					fprintf(max_watches, "%u", next_highest(next_highest(num_watches)));
				}
				fclose(max_watches);
			}
			else
			{
				DPRINTF(E_WARN, L_INOTIFY, "WARNING: Inotify max_user_watches [%u] is low or close to the number of used watches [%u] "
				                        "and I do not have permission to increase this limit.  Please do so manually by "
				                        "writing a higher value into /proc/sys/fs/inotify/max_user_watches.\n", watch_limit, num_watches);
			}
		}
	}
	else
	{
		DPRINTF(E_WARN, L_INOTIFY, "WARNING: Could not read inotify max_user_watches!  "
		                        "Hopefully it is enough to cover %u current directories plus any new ones added.\n", num_watches);
	}

	media_path = media_dirs;
	while( media_path )
	{
		add_watch(fd, media_path->path);
		media_path = media_path->next;
	}
	sql_get_table(db, "SELECT PATH from DETAILS where SIZE is NULL and PATH is not NULL", &result, &rows, NULL);
	for( i=1; i <= rows; i++ )
	{
		DPRINTF(E_DEBUG, L_INOTIFY, "Add watch to %s\n", result[i]);
		add_watch(fd, result[i]);
	}
	sqlite3_free_table(result);

	return rows;
}

int 
inotify_remove_watches(int fd)
{
	struct watch *w = watches;
	int rm_watches = 0;

	while( w )
	{
		inotify_rm_watch(fd, w->wd);
		rm_watches++;
		w = w->next;
	}

	return rm_watches;
}

int add_dir_watch(int fd, char * path, char * filename)
{
	DIR *ds;
	struct dirent *e;
	char *buf;
	int wd;
	int i = 0;

	if( filename )
		asprintf(&buf, "%s/%s", path, filename);
	else
		buf = strdup(path);

	wd = add_watch(fd, buf);
	if( wd == -1 )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "add_watch() [%s]\n", strerror(errno));
	}
	else
	{
		DPRINTF(E_INFO, L_INOTIFY, "Added watch to %s [%d]\n", buf, wd);
	}

	ds = opendir(buf);
	if( ds != NULL )
	{
		while( (e = readdir(ds)) )
		{
			if( strcmp(e->d_name, ".") == 0 ||
			    strcmp(e->d_name, "..") == 0 )
				continue;
			if( e->d_type == DT_DIR )
				i += add_dir_watch(fd, buf, e->d_name);
		}
	}
	else
	{
		DPRINTF(E_ERROR, L_INOTIFY, "Opendir error! [%s]\n", strerror(errno));
	}
	closedir(ds);
	i++;
	free(buf);

	return(i);
}

int
inotify_insert_file(char * name, const char * path)
{
	char * sql;
	char **result;
	int rows;
	char * last_dir = strdup(path);
	char * path_buf = strdup(path);
	char * base_name = malloc(strlen(path));
	char * base_copy = base_name;
	char * parent_buf = NULL;
	char * id = NULL;
	int depth = 1;
	enum media_types type = ALL_MEDIA;
	struct media_dir_s * media_path = media_dirs;

	/* Check if we're supposed to be scanning for this file type in this directory */
	while( media_path )
	{
		if( strncmp(path, media_path->path, strlen(media_path->path)) == 0 )
		{
			type = media_path->type;
			break;
		}
		media_path = media_path->next;
	}
	switch( type )
	{
		case ALL_MEDIA:
			if( !is_image(path) &&
			    !is_audio(path) &&
			    !is_video(path) )
				return -1;
			break;
		case AUDIO_ONLY:
			if( !is_audio(path) )
				return -1;
			break;
		case VIDEO_ONLY:
			if( !is_video(path) )
				return -1;
			break;
		case IMAGES_ONLY:
			if( !is_image(path) )
				return -1;
			break;
                default:
			return -1;
			break;
	}
	
	/* If it's already in the database, just skip it for now.
	 * TODO: compare modify timestamps */
	sql = sqlite3_mprintf("SELECT ID from DETAILS where PATH = '%q'", path);
	if( sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK )
	{
 		if( rows )
		{
			free(last_dir);
			free(path_buf);
			free(base_name);
			sqlite3_free(sql);
			sqlite3_free_table(result);
			return -1;
		}
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);
	/* Find the parentID.  If it's not found, create all necessary parents. */
	while( depth )
	{
		depth = 0;
		strcpy(path_buf, path);
		parent_buf = dirname(path_buf);
		strcpy(last_dir, path_buf);

		do
		{
			DPRINTF(E_DEBUG, L_INOTIFY, "Checking %s\n", parent_buf);
			sql = sqlite3_mprintf("SELECT OBJECT_ID from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
			                      " where d.PATH = '%q' and REF_ID is NULL", parent_buf);
			if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) && rows )
			{
				id = strdup(result[1]);
				sqlite3_free_table(result);
				sqlite3_free(sql);
				if( !depth )
					break;
				DPRINTF(E_DEBUG, L_INOTIFY, "Found first known parentID: %s\n", id);
				/* Insert newly-found directory */
				strcpy(base_name, last_dir);
				base_copy = basename(base_name);
				insert_directory(base_copy, last_dir, BROWSEDIR_ID, id+2, get_next_available_id("OBJECTS", id));
				free(id);
				break;
			}
			depth++;
			strcpy(last_dir, path_buf);
			parent_buf = dirname(parent_buf);
			sqlite3_free_table(result);
			sqlite3_free(sql);
		}
		while( strcmp(parent_buf, "/") != 0 );

		if( strcmp(parent_buf, "/") == 0 )
		{
			id = calloc(1, 3);
			depth = 0;
			break;
		}
		strcpy(path_buf, path);
	}
	free(last_dir);
	free(path_buf);
	free(base_name);

	if( !depth )
	{
		insert_file(name, path, id+2, get_next_available_id("OBJECTS", id));
		free(id);
	}
	return depth;
}

int
inotify_insert_directory(int fd, char *name, const char * path)
{
	DIR * ds;
	struct dirent * e;
	char * sql;
	char **result;
	char *id=NULL, *path_buf, *parent_buf, *esc_name;
	int wd;
	int rows, i = 0;

 	parent_buf = dirname(strdup(path));
	sql = sqlite3_mprintf("SELECT OBJECT_ID from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
	                      " where d.PATH = '%q' and REF_ID is NULL", parent_buf);
	if( sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK )
	{
 		if( rows )
		{
			id = strdup(result[1]);
			sqlite3_free_table(result);
			insert_directory(name, path, BROWSEDIR_ID, id+2, get_next_available_id("OBJECTS", id));
			free(id);
		}
		else
		{
			sqlite3_free_table(result);
			insert_directory(name, path, BROWSEDIR_ID, "", get_next_available_id("OBJECTS", id));
		}
	}
	free(parent_buf);
	sqlite3_free(sql);

	wd = add_watch(fd, path);
	if( wd == -1 )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "add_watch() failed");
	}
	else
	{
		DPRINTF(E_INFO, L_INOTIFY, "Added watch to %s [%d]\n", path, wd);
	}

	ds = opendir(path);
	if( ds != NULL )
	{
		while( (e = readdir(ds)) )
		{
			if( strcmp(e->d_name, ".") == 0 ||
			    strcmp(e->d_name, "..") == 0 )
				continue;
			esc_name = escape_tag(e->d_name);
			if( !esc_name )
				esc_name = strdup(e->d_name);
			asprintf(&path_buf, "%s/%s", path, e->d_name);
			if( e->d_type == DT_DIR )
			{
				inotify_insert_directory(fd, esc_name, path_buf);
			}
			else if( e->d_type == DT_REG )
			{
				inotify_insert_file(esc_name, path_buf);
			}
			free(esc_name);
			free(path_buf);
		}
	}
	else
	{
		DPRINTF(E_ERROR, L_INOTIFY, "opendir failed! [%s]\n", strerror(errno));
	}
	closedir(ds);
	i++;

	return(i);
}

int
inotify_remove_file(const char * path)
{
	char * sql;
	char **result;
	char **result2;
	char * art_cache;
	sqlite_int64 detailID = 0;
	int i, rows, children, ret = 1;

	/* Invalidate the scanner cache so we don't insert files into non-existent containers */
	valid_cache = 0;
	sql = sqlite3_mprintf("SELECT ID from DETAILS where PATH = '%q'", path);
	if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) )
	{
		if( rows )
		{
			detailID = strtoll(result[1], NULL, 10);
			ret = 0;
		}
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);
	if( detailID )
	{
		/* Delete the parent containers if we are about to empty them. */
		asprintf(&sql, "SELECT PARENT_ID from OBJECTS where DETAIL_ID = %lld", detailID);
		if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) )
		{
			for( i=1; i < rows; i++ )
			{
				free(sql);
				asprintf(&sql, "SELECT count(OBJECT_ID) from OBJECTS where PARENT_ID = '%s'", result[i]);
				if( sql_get_table(db, sql, &result2, NULL, NULL) == SQLITE_OK )
				{
					children = atoi(result2[1]);
					if( children < 2 )
					{
						free(sql);
						asprintf(&sql, "DELETE from OBJECTS where OBJECT_ID = '%s'", result[i]);
						sql_exec(db, sql);
					}
					sqlite3_free_table(result2);
					if( children < 2 )
					{
						*rindex(result[i], '$') = '\0';
						free(sql);
						asprintf(&sql, "SELECT count(OBJECT_ID) from OBJECTS where PARENT_ID = '%s'", result[i]);
						if( sql_get_table(db, sql, &result2, NULL, NULL) == SQLITE_OK )
						{
							if( atoi(result2[1]) == 0 )
							{
								free(sql);
								asprintf(&sql, "DELETE from OBJECTS where OBJECT_ID = '%s'", result[i]);
								sql_exec(db, sql);
							}
							sqlite3_free_table(result2);
						}
					}
				}
			}
			sqlite3_free_table(result);
		}
		free(sql);
		/* Now delete the actual objects */
		asprintf(&sql, "DELETE from DETAILS where ID = %lld", detailID);
		sql_exec(db, sql);
		free(sql);
		asprintf(&sql, "DELETE from OBJECTS where DETAIL_ID = %lld", detailID);
		sql_exec(db, sql);
		free(sql);
	}
	asprintf(&art_cache, "%s/art_cache%s", DB_PATH, path);
	remove(art_cache);
	free(art_cache);

	return ret;
}

int
inotify_remove_directory(int fd, const char * path)
{
	char * sql;
	char * sql2;
	char **result;
	sqlite_int64 detailID = 0;
	int rows, i, ret = 1;

	remove_watch(fd, path);
	sql = sqlite3_mprintf("SELECT ID from DETAILS where PATH glob '%q/*'"
	                      " UNION ALL SELECT ID from DETAILS where PATH = '%q'", path, path);
	if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) )
	{
		if( rows )
		{
			for( i=1; i <= rows; i++ )
			{
				detailID = strtoll(result[i], NULL, 10);
				asprintf(&sql2, "DELETE from DETAILS where ID = %lld", detailID);
				sql_exec(db, sql2);
				free(sql2);
				asprintf(&sql2, "DELETE from OBJECTS where DETAIL_ID = %lld", detailID);
				sql_exec(db, sql2);
				free(sql2);
			}
			ret = 0;
		}
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);
	/* Clean up any album art entries in the deleted directory */
	sql = sqlite3_mprintf("DELETE from ALBUM_ART where PATH glob '%q/*'", path);
	sql_exec(db, sql);
	sqlite3_free(sql);

	return ret;
}

void *
start_inotify()
{
	int fd;
	char buffer[BUF_LEN];
	int length, i = 0;
	char * esc_name = NULL;
	char * path_buf = NULL;
        
	fd = inotify_init();

	if ( fd < 0 ) {
		DPRINTF(E_ERROR, L_INOTIFY, "inotify_init() failed!\n");
	}

	while( scanning )
	{
		sleep(1);
	}
	inotify_create_watches(fd);
	if (setpriority(PRIO_PROCESS, 0, 19) == -1)
		DPRINTF(E_WARN, L_INOTIFY,  "Failed to reduce inotify thread priority\n");
        
	while( 1 )
	{
		length = read(fd, buffer, BUF_LEN);  

		if ( length < 0 ) {
			DPRINTF(E_ERROR, L_INOTIFY, "read failed!\n");
		}  

		i = 0;
		while( i < length )
		{
			struct inotify_event * event = (struct inotify_event *) &buffer[i];
			if( event->len )
			{
				if( *(event->name) == '.' )
				{
					i += EVENT_SIZE + event->len;
					continue;
				}
				esc_name = modifyString(strdup(event->name), "&", "&amp;amp;", 0);
				asprintf(&path_buf, "%s/%s", get_path_from_wd(event->wd), event->name);
				if ( (event->mask & IN_CREATE && event->mask & IN_ISDIR) ||
				     (event->mask & IN_MOVED_TO && event->mask & IN_ISDIR) )
				{
					DPRINTF(E_DEBUG, L_INOTIFY,  "The directory %s was %s.\n", path_buf, (event->mask & IN_MOVED_TO ? "moved here" : "created"));
					inotify_insert_directory(fd, esc_name, path_buf);
				}
				else if ( event->mask & IN_CLOSE_WRITE || event->mask & IN_MOVED_TO )
				{
					DPRINTF(E_DEBUG, L_INOTIFY, "The file %s was %s.\n", path_buf, (event->mask & IN_MOVED_TO ? "moved here" : "changed"));
					inotify_insert_file(esc_name, path_buf);
				}
				else if ( event->mask & IN_DELETE || event->mask & IN_MOVED_FROM )
				{
					if ( event->mask & IN_ISDIR )
					{
						DPRINTF(E_DEBUG, L_INOTIFY, "The directory %s was %s.\n", path_buf, (event->mask & IN_MOVED_FROM ? "moved away" : "deleted"));
						inotify_remove_directory(fd, path_buf);
					}
					else
					{
						DPRINTF(E_DEBUG, L_INOTIFY, "The file %s was %s.\n", path_buf, (event->mask & IN_MOVED_FROM ? "moved away" : "deleted"));
						inotify_remove_file(path_buf);
					}
				}
				free(esc_name);
				free(path_buf);
			}
			i += EVENT_SIZE + event->len;
		}
	}
	inotify_remove_watches(fd);
	close(fd);

	return NULL;
}
