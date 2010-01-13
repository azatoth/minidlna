/*  MiniDLNA media server
 *  Copyright (C) 2009-2010  Justin Maggard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <libgen.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "tagutils/tagutils.h"

#include "upnpglobalvars.h"
#include "scanner.h"
#include "metadata.h"
#include "utils.h"
#include "sql.h"
#include "log.h"

int
insert_playlist(const char * path, char * name)
{
	struct song_metadata plist;
	struct stat file;
	int items = 0, matches;
	char type[4];

	strncpy(type, strrchr(name, '.')+1, 4);

	if( start_plist(path, NULL, &file, NULL, type) != 0 )
	{
		DPRINTF(E_WARN, L_SCANNER, "Bad playlist [%s]\n", path);
		return -1;
	}
	while( next_plist_track(&plist, &file, NULL, type) == 0 )
	{
		items++;
		freetags(&plist);
	}
	strip_ext(name);

	DPRINTF(E_DEBUG, L_SCANNER, "Playlist %s contains %d items\n", name, items);
	
	matches = sql_get_int_field(db, "SELECT count(*) from PLAYLISTS where NAME = '%q'", name);
	if( matches > 0 )
	{
		sql_exec(db, "INSERT into PLAYLISTS"
		             " (NAME, PATH, ITEMS) "
	        	     "VALUES"
		             " ('%q(%d)', '%q', %d)",
		             name, matches, path, items);
	}
	else
	{
		sql_exec(db, "INSERT into PLAYLISTS"
		             " (NAME, PATH, ITEMS) "
	        	     "VALUES"
		             " ('%q', '%q', %d)",
		             name, path, items);
	}
	return 0;
}

int
fill_playlists()
{
	int rows, i, found, len;
	char **result;
	char *plpath, *plname, *fname;
	char class[] = "playlistContainer";
	struct song_metadata plist;
	struct stat file;
	char type[4];
	sqlite_int64 plID, detailID;
	char sql_buf[1024] = "SELECT ID, NAME, PATH from PLAYLISTS where ITEMS > FOUND";

	if( sql_get_table(db, sql_buf, &result, &rows, NULL) != SQLITE_OK ) 
		return -1;
	if( !rows )
	{
		sqlite3_free_table(result);
		return 0;
	}

	rows++;
	for( i=3; i<rows*3; i++ )
	{
		plID = strtoll(result[i], NULL, 10);
		plname = result[++i];
		plpath = result[++i];

		strncpy(type, strrchr(plpath, '.')+1, 4);

		if( start_plist(plpath, NULL, &file, NULL, type) != 0 )
			continue;

		DPRINTF(E_DEBUG, L_SCANNER, "Scanning playlist \"%s\" [%s]\n", plname, plpath);
		sprintf(sql_buf, "SELECT ID from OBJECTS where PARENT_ID = '"MUSIC_PLIST_ID"'"
		                  " and NAME = '%s'", plname);
		if( sql_get_int_field(db, sql_buf) <= 0 )
		{
			detailID = GetFolderMetadata(plname, NULL, NULL, NULL, NULL);
			sql_exec(db, "INSERT into OBJECTS"
			             " (OBJECT_ID, PARENT_ID, DETAIL_ID, CLASS, NAME) "
			             "VALUES"
			             " ('%s$%llX', '%s', %lld, 'container.%s', '%q')",
			             MUSIC_PLIST_ID, plID, MUSIC_PLIST_ID, detailID, class, plname);
		}

		plpath = dirname(plpath);
		found = 0;
		while( next_plist_track(&plist, &file, NULL, type) == 0 )
		{
			if( sql_get_int_field(db, "SELECT 1 from OBJECTS where OBJECT_ID = '%s$%llX$%d'",
			                      MUSIC_PLIST_ID, plID, plist.track) == 1 )
			{
				//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "%d: already in database\n", plist.track);
				found++;
       				freetags(&plist);
				continue;
			}
			
			fname = plist.path;
			DPRINTF(E_DEBUG, L_SCANNER, "%d: checking database for %s\n", plist.track, plist.path);
			if( !strpbrk(fname, "\\/") )
			{
				len = strlen(fname) + strlen(plpath) + 2;
				plist.path = malloc(len);
				snprintf(plist.path, len, "%s/%s", plpath, fname);
				free(fname);
				fname = plist.path;
			}
			else
			{
				while( *fname == '\\' )
				{
					fname++;
				}
			}
retry:
			//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "* Searching for %s in db\n", fname);
			detailID = sql_get_int_field(db, "SELECT ID from DETAILS where PATH like '%%%q'", fname);
			if( detailID > 0 )
			{
				DPRINTF(E_DEBUG, L_SCANNER, "+ %s found in db\n", fname);
				sql_exec(db, "INSERT into OBJECTS"
				             " (OBJECT_ID, PARENT_ID, CLASS, DETAIL_ID, NAME, REF_ID) "
				             "SELECT"
				             " '%s$%llX$%d', '%s$%llX', CLASS, DETAIL_ID, NAME, OBJECT_ID from OBJECTS"
				             " where DETAIL_ID = %lld and OBJECT_ID glob '64$*'",
				             MUSIC_PLIST_ID, plID, plist.track,
				             MUSIC_PLIST_ID, plID,
				             detailID);
				found++;
			}
			else
			{
				DPRINTF(E_DEBUG, L_SCANNER, "- %s not found in db\n", fname);
				if( strchr(fname, '\\') )
				{
					fname = modifyString(fname, "\\", "/", 0);
					goto retry;
				}
				else if( (fname = strchr(fname, '/')) )
				{
					fname++;
					goto retry;
				}
			}
       			freetags(&plist);
		}
		sql_exec(db, "UPDATE PLAYLISTS set FOUND = %d where ID = %lld", found, plID);
	}
	sqlite3_free_table(result);

	return 0;
}

