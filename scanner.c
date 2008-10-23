/*  MiniDLNA media server
 *  Copyright (C) 2008  Justin Maggard
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
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <sys/stat.h>

#include <sqlite3.h>

#include "upnpglobalvars.h"
#include "metadata.h"
#include "sql.h"
#include "scanner.h"

int
ends_with(const char * haystack, const char * needle)
{
	const char *found = strcasestr(haystack, needle);
	return (found && found[strlen(needle)] == '\0');
}

int
is_video(const char * file)
{
	return (ends_with(file, ".mpg") || ends_with(file, ".mpeg") ||
		ends_with(file, ".ts")  || ends_with(file, ".avi"));
}

int
is_audio(const char * file)
{
	return (ends_with(file, ".mp3") ||
		ends_with(file, ".m4a") || ends_with(file, ".aac"));
}

int
is_image(const char * file)
{
	return (ends_with(file, ".jpg") || ends_with(file, ".jpeg"));
}

long long int
insert_container(const char * tmpTable, const char * item, const char * rootParent, const char *subParent, const char *class, long unsigned int detailID)
{
	char **result;
	char *sql;
	int cols, rows, ret;
	char *zErrMsg = NULL;
	int parentID = 0, objectID = 0;

	sql = sqlite3_mprintf("SELECT * from %s where ITEM = '%q' and SUBITEM = '%q'", tmpTable, item, subParent);
	ret = sql_get_table(db, sql, &result, &rows, &cols, &zErrMsg);
	sqlite3_free(sql);
	if( cols )
	{
		sscanf(result[4], "%X", &parentID);
		sqlite3_free_table(result);
		sql = sqlite3_mprintf("SELECT OBJECT_ID, max(ID) from OBJECTS where PARENT_ID = '%s$%X'", rootParent, parentID);
		ret = sql_get_table(db, sql, &result, 0, &cols, &zErrMsg);
		sqlite3_free(sql);
		if( result[2] && (sscanf(rindex(result[2], '$')+1, "%X", &objectID) == 1) )
		{
			objectID++;
		}
	}
	else
	{
		sqlite3_free_table(result);
		sql = sqlite3_mprintf("SELECT OBJECT_ID, max(ID) from OBJECTS where PARENT_ID = '%s'", rootParent);
		sql_get_table(db, sql, &result, &rows, &cols, &zErrMsg);
		sqlite3_free(sql);
		if( result[2] && (sscanf(rindex(result[2], '$')+1, "%X", &parentID) == 1) )
		{
			parentID++;
		}
		else
		{
			parentID = 0;
		}
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, DETAIL_ID, CLASS, NAME) "
					"VALUES"
					" ('%s$%X', '%s', %lu, 'container.%s', '%q')",
					rootParent, parentID, rootParent, detailID, class, item);
		ret = sql_exec(db, sql);
		sqlite3_free(sql);
		sql = sqlite3_mprintf("INSERT into %s values ('%q', '%X', '%q')", tmpTable, item, parentID, subParent);
		sql_exec(db, sql);
		sqlite3_free(sql);
	}
	sqlite3_free_table(result);

	return (long long)parentID<<32|objectID;
}

void
insert_containers(const char * name, const char *path, const char * refID, const char * class, long unsigned int detailID)
{
	char sql_buf[128];
	char *sql;
	char **result;
	int ret;
	int cols, row;
	char *zErrMsg = NULL;
	long long int container;
	int parentID;
	int objectID = -1;

	sprintf(sql_buf, "SELECT * from DETAILS where ID = %lu", detailID);
	ret = sql_get_table(db, sql_buf, &result, &row, &cols, &zErrMsg);

	if( strstr(class, "imageItem") )
	{
		char *date = result[12+cols], *cam = result[16+cols];
		char date_taken[11];  date_taken[10] = '\0';
		static int last_all_objectID = 0;
		if( date )
			strncpy(date_taken, date, 10);
		if( date )
		{
			container = insert_container("DATES", date_taken, "3$12", NULL, "album.photoAlbum", 0);
			parentID = container>>32;
			objectID = container;
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
						"VALUES"
						" ('3$12$%X$%X', '3$12$%X', '%s', '%s', %lu, %Q, %Q)",
						parentID, objectID, parentID, refID, class, detailID, path, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		if( cam && date )
		{
			container = insert_container("CAMS", cam, "3$13", NULL, "storageFolder", 0);
			parentID = container>>32;
			//objectID = container;
			char parent[64];
			sprintf(parent, "3$13$%X", parentID);
			long long int subcontainer = insert_container("CAMDATE", date_taken, parent, cam, "storageFolder", 0);
			int subParentID = subcontainer>>32;
			int subObjectID = subcontainer;
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
						"VALUES"
						" ('3$13$%X$%X$%X', '3$13$%X$%X', '%s', '%s', %lu, %Q, %Q)",
						parentID, subParentID, subObjectID, parentID, subParentID, refID, class, detailID, path, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		/* All Images */
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
					"VALUES"
					" ('3$11$%X', '3$11', '%s', '%s', %lu, %Q, %Q)",
					last_all_objectID++, refID, class, detailID, path, name);
		sql_exec(db, sql);
		sqlite3_free(sql);
	}
	else if( strstr(class, "audioItem") )
	{
		char *artist = result[6+cols], *album = result[7+cols], *genre = result[8+cols];
		static char last_artist[1024];
		static int  last_artist_parentID, last_artist_objectID;
		static char last_album[1024];
		static int  last_album_parentID, last_album_objectID;
		static char last_genre[1024];
		static int  last_genre_parentID, last_genre_objectID;
		static int  last_all_objectID = 0;

		if( artist )
		{
			if( strcmp(artist, last_artist) == 0 )
			{
				objectID = ++last_artist_objectID;
				parentID = last_artist_parentID;
			}
			else
			{
				strcpy(last_artist, artist);
				container = insert_container("ARTISTS", artist, "1$6", NULL, "person.musicArtist", 0);
				parentID = container>>32;
				objectID = container;
				last_artist_objectID = objectID;
				last_artist_parentID = parentID;
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
						"VALUES"
						" ('1$6$%X$%X', '1$6$%X', '%s', '%s', %lu, %Q, %Q)",
						parentID, objectID, parentID, refID, class, detailID, path, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		if( album )
		{
			if( strcmp(album, last_album) == 0 )
			{
				objectID = ++last_album_objectID;
				parentID = last_album_parentID;
			}
			else
			{
				strcpy(last_album, album);
				container = insert_container("ALBUMS", album, "1$7", NULL, "album.musicAlbum", detailID);
				parentID = container>>32;
				objectID = container;
				last_album_objectID = objectID;
				last_album_parentID = parentID;
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
						"VALUES"
						" ('1$7$%X$%X', '1$7$%X', '%s', '%s', %lu, %Q, %Q)",
						parentID, objectID, parentID, refID, class, detailID, path, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		if( genre )
		{
			if( strcmp(genre, last_genre) == 0 )
			{
				objectID = ++last_genre_objectID;
				parentID = last_genre_parentID;
			}
			else
			{
				strcpy(last_genre, genre);
				container = insert_container("GENRES", genre, "1$5", NULL, "genre.musicGenre", 0);
				parentID = container>>32;
				objectID = container;
				last_genre_objectID = objectID;
				last_genre_parentID = parentID;
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
						"VALUES"
						" ('1$5$%X$%X', '1$5$%X', '%s', '%s', %lu, %Q, %Q)",
						parentID, objectID, parentID, refID, class, detailID, path, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		/* All Music */
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
					"VALUES"
					" ('1$4$%X', '1$4', '%s', '%s', %lu, %Q, %Q)",
					last_all_objectID++, refID, class, detailID, path, name);
		sql_exec(db, sql);
		sqlite3_free(sql);
	}
	sqlite3_free_table(result);
}

int
insert_directory(const char * name, const char * path, const char * parentID, int objectID)
{
	char *sql;
	int ret, i;
	char class[] = "container.storageFolder";
	const char * const base[] = { BROWSEDIR_ID, MUSIC_DIR_ID, VIDEO_DIR_ID, IMAGE_DIR_ID, 0 };

	for( i=0; base[i]; i++ )
	{
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, CLASS, PATH, NAME) "
					"VALUES"
					" ('%s%s$%X', '%s%s', '%s', '%q', '%q')",
					base[i], parentID, objectID, base[i], parentID, class, path, name);
		//DEBUG printf("SQL: %s\n", sql);
		ret = sql_exec(db, sql);
		sqlite3_free(sql);
	}
	return -1;
}

int
insert_file(char * name, const char * path, const char * parentID, int object)
{
	char *sql;
	char class[32];
	char objectID[64];
	unsigned long int detailID = 0;
	char base[8];

	static long unsigned int fileno = 0;
	printf("Scanned %lu files...\r", fileno++); fflush(stdout);

	sprintf(objectID, "%s%s$%X", BROWSEDIR_ID, parentID, object);

	if( is_image(name) )
	{
		strcpy(base, IMAGE_DIR_ID);
		strcpy(class, "item.imageItem");
		detailID = GetImageMetadata(path, name);
	}
	else if( is_audio(name) )
	{
		strcpy(base, MUSIC_DIR_ID);
		strcpy(class, "item.audioItem.musicTrack");
		detailID = GetAudioMetadata(path, name);
	}
	else if( is_video(name) )
	{
		strcpy(base, VIDEO_DIR_ID);
		strcpy(class, "item.videoItem");
		detailID = GetVideoMetadata(path, name);
	}
	//DEBUG printf("Got DetailID %lu!\n", detailID);

	sql = sqlite3_mprintf(	"INSERT into OBJECTS"
				" (OBJECT_ID, PARENT_ID, CLASS, DETAIL_ID, PATH, NAME) "
				"VALUES"
				" ('%s', '%s%s', '%s', %lu, '%q', '%q')",
				objectID, BROWSEDIR_ID, parentID, class, detailID, path, name);
	//DEBUG printf("SQL: %s\n", sql);
	sql_exec(db, sql);
	sqlite3_free(sql);
	#if 0
	sql = sqlite3_mprintf(	"INSERT into OBJECTS"
				" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, PATH, NAME) "
				"VALUES"
				" ('%s%s$%X', '%s%s', '%s', '%s', %lu, '%q', '%q')",
				base, parentID, object, base, parentID, objectID, class, detailID, path, name);
	//DEBUG printf("SQL: %s\n", sql);
	sql_exec(db, sql);
	sqlite3_free(sql);
	#else
	insert_containers(name, path, objectID, class, detailID);
	#endif
	return -1;
}

int
create_database(void)
{
	int ret, i;
	char sql_buf[512];
	const char * containers[] = {      "0","-1", "root",
					   "1", "0", "Music",
					 "1$4", "1", "All Music",
					 "1$5", "1", "Genre",
					 "1$6", "1", "Artist",
					 "1$7", "1", "Album",
					"1$20", "1", "Folders",
					   "2", "0", "Video",
					 "2$8", "2", "All Video",
					"2$21", "2", "Folders",
					   "3", "0", "Pictures",
					"3$11", "3", "All Pictures",
					"3$12", "3", "Date Taken",
					"3$13", "3", "Camera",
					"3$22", "3", "Folders",
					  "64", "0", "Browse Folders",
					0 };

	sql_exec(db, "pragma temp_store = MEMORY");
	sql_exec(db, "pragma synchronous = OFF;");
	sql_exec(db, "pragma cache_size = 8192;");

	ret = sql_exec(db, "CREATE TABLE OBJECTS ( "
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, "
					"OBJECT_ID TEXT NOT NULL, "
					"PARENT_ID TEXT NOT NULL, "
					"REF_ID TEXT DEFAULT NULL, "
					"CLASS TEXT NOT NULL, "
					"DETAIL_ID INTEGER DEFAULT NULL, "
					"PATH TEXT DEFAULT NULL, "
					"NAME TEXT DEFAULT NULL"
					");");
	if( ret != SQLITE_OK )
		goto sql_failed;
	ret = sql_exec(db, "CREATE TABLE DETAILS ( "
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, "
					"SIZE INTEGER, "
					"TITLE TEXT, "
					"DURATION TEXT, "
					"BITRATE INTEGER, "
					"SAMPLERATE INTEGER, "
					"ARTIST TEXT, "
					"ALBUM TEXT, "
					"GENRE TEXT, "
					"COMMENT TEXT, "
					"CHANNELS INTEGER, "
					"TRACK INTEGER, "
					"DATE DATE, "
					"WIDTH TEXT, "
					"HEIGHT TEXT, "
					"THUMBNAIL BOOL DEFAULT 0, "
					"CREATOR TEXT, "
					"DLNA_PN TEXT, "
					"MIME TEXT"
					");");
	if( ret != SQLITE_OK )
		goto sql_failed;
	for( i=0; containers[i]; i=i+3 )
	{
		sprintf(sql_buf, "INSERT into OBJECTS (OBJECT_ID, PARENT_ID, CLASS, NAME) values ( '%s', '%s', 'container.storageFolder', '%s')",
			containers[i], containers[i+1], containers[i+2]);
		ret = sql_exec(db, sql_buf);
		if( ret != SQLITE_OK )
			goto sql_failed;
	}
	sql_exec(db, "create TEMP TABLE ARTISTS (ITEM TEXT, OBJECT_ID TEXT, SUBITEM TEXT DEFAULT NULL);");
	sql_exec(db, "create TEMP TABLE ALBUMS (ITEM TEXT, OBJECT_ID TEXT, SUBITEM TEXT DEFAULT NULL);");
	sql_exec(db, "create TEMP TABLE GENRES (ITEM TEXT, OBJECT_ID TEXT, SUBITEM TEXT DEFAULT NULL);");
	sql_exec(db, "create TEMP TABLE DATES (ITEM TEXT, OBJECT_ID TEXT, SUBITEM TEXT DEFAULT NULL);");
	sql_exec(db, "create TEMP TABLE CAMS (ITEM TEXT, OBJECT_ID TEXT, SUBITEM TEXT DEFAULT NULL);");
	sql_exec(db, "create TEMP TABLE CAMDATE (ITEM TEXT, OBJECT_ID TEXT, SUBITEM TEXT DEFAULT NULL);");

	sql_exec(db, "create INDEX IDX_OBJECTS_OBJECT_ID ON OBJECTS(OBJECT_ID);");
	sql_exec(db, "create INDEX IDX_OBJECTS_PARENT_ID ON OBJECTS(PARENT_ID);");
	sql_exec(db, "create INDEX IDX_OBJECTS_DETAIL_ID ON OBJECTS(DETAIL_ID);");
	sql_exec(db, "create INDEX IDX_OBJECTS_CLASS ON OBJECTS(CLASS);");
	sql_exec(db, "create INDEX IDX_OBJECTS_PATH ON OBJECTS(PATH);");
	sql_exec(db, "create INDEX IDX_DETAILS_ID ON DETAILS(ID);");


sql_failed:
	if( ret != SQLITE_OK )
		fprintf(stderr, "Error creating SQLite3 database!\n");
	return (ret != SQLITE_OK);
}

int
filter_media(const struct dirent *d)
{
	struct stat entry;
	return ( (*d->d_name != '.') &&
		 (stat(d->d_name, &entry) == 0) &&
		 (S_ISDIR(entry.st_mode) ||
		  (S_ISREG(entry.st_mode) &&
		   (is_image(d->d_name) ||
		    is_audio(d->d_name) ||
		    is_video(d->d_name)
		   )
	       )) );
}

void
ScanDirectory(const char * dir, const char * parent)
{
	struct dirent **namelist;
        struct stat entry;
	int n, i;
	char parent_id[PATH_MAX];
	char full_path[PATH_MAX];
	char * name;

	if( !parent )
	{
		if( create_database() != 0 )
		{
			fprintf(stderr, "Error creating database!\n");
			return;
		}
	}

	setlocale(LC_COLLATE, "");
	if( chdir(dir) != 0 )
		return;
	n = scandir(".", &namelist, filter_media, alphasort);
	if (n < 0) {
		fprintf(stderr, "Error scanning %s [scandir]\n", dir);
		return;
	}
	for (i=0; i < n; i++) {
                if( stat(namelist[i]->d_name, &entry) == 0 )
		{
			name = NULL;
			sprintf(full_path, "%s/%s", dir, namelist[i]->d_name);
			if( index(namelist[i]->d_name, '&') )
			{
				name = modifyString(strdup(namelist[i]->d_name), "&", "&amp;amp;", 0);
			}
			if( S_ISDIR(entry.st_mode) )
			{
				insert_directory(name?name:namelist[i]->d_name, full_path, (parent ? parent:""), i);
				sprintf(parent_id, "%s$%X", (parent ? parent:""), i);
				ScanDirectory(full_path, parent_id);
			}
			else
			{
				insert_file(name?name:namelist[i]->d_name, full_path, (parent ? parent:""), i);
			}
		}
		if( name )
			free(name);
		free(namelist[i]);
	}
	free(namelist);
	chdir("..");
}
