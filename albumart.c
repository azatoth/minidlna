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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <setjmp.h>

#include <jpeglib.h>

#include "upnpglobalvars.h"
#include "sql.h"
#include "utils.h"
#include "image_utils.h"
#include "log.h"

char *
save_resized_album_art(image * imsrc, const char * path)
{
	int dstw, dsth;
	image * imdst;
	char * cache_file;
	char * cache_dir;

	if( !imsrc )
		return NULL;

	asprintf(&cache_file, DB_PATH "/art_cache%s", path);
	if( access(cache_file, F_OK) == 0 )
		return cache_file;

	cache_dir = strdup(cache_file);
	make_dir(dirname(cache_dir), S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	free(cache_dir);

	if( imsrc->width > imsrc->height )
	{
		dstw = 160;
		dsth = (imsrc->height<<8) / ((imsrc->width<<8)/160);
	}
	else
	{
		dstw = (imsrc->width<<8) / ((imsrc->height<<8)/160);
		dsth = 160;
	}
        imdst = image_resize(imsrc, dstw, dsth);
	if( !imdst )
		goto error;

	if( image_save_to_jpeg_file(imdst, cache_file) == 0 )
	{
		image_free(imdst);
		return cache_file;
	}
error:
	free(cache_file);
	return NULL;
}

/* Simple, efficient hash function from Daniel J. Bernstein */
unsigned int DJBHash(const char * str, int len)
{
	unsigned int hash = 5381;
	unsigned int i = 0;

	for(i = 0; i < len; str++, i++)
	{
		hash = ((hash << 5) + hash) + (*str);
	}

	return hash;
}

/* And our main album art functions */
char *
check_embedded_art(const char * path, const char * image_data, int image_size)
{
	int width = 0, height = 0;
	char * art_path = NULL;
	char * cache_dir;
	FILE * dstfile;
	image * imsrc;
	size_t nwritten;
	static char last_path[PATH_MAX];
	static unsigned int last_hash = 0;
	unsigned int hash;

	if( !image_data || !image_size || !path )
	{
		return NULL;
	}
	/* If the embedded image matches the embedded image from the last file we
	 * checked, just make a hard link.  Better than storing it on the disk twice. */
	hash = DJBHash(image_data, image_size);
	if( hash == last_hash )
	{
		asprintf(&art_path, DB_PATH "/art_cache%s", path);
		if( link(last_path, art_path) == 0 )
		{
			return(art_path);
		}
		else
		{
			DPRINTF(E_WARN, L_METADATA, "Linking %s to %s failed\n", art_path, last_path);
			free(art_path);
			art_path = NULL;
		}
	}

	imsrc = image_new_from_jpeg(NULL, 0, image_data, image_size);
	width = imsrc->width;
	height = imsrc->height;

	if( width > 160 || height > 160 )
	{
		art_path = save_resized_album_art(imsrc, path);
	}
	else if( width > 0 && height > 0 )
	{
		asprintf(&art_path, DB_PATH "/art_cache%s", path);
		if( access(art_path, F_OK) == 0 )
			return art_path;
		cache_dir = strdup(art_path);
		make_dir(dirname(cache_dir), S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
		free(cache_dir);
		dstfile = fopen(art_path, "w");
		if( !dstfile )
			return NULL;
		nwritten = fwrite((void *)image_data, image_size, 1, dstfile);
		fclose(dstfile);
		if( nwritten != image_size )
		{
			remove(art_path);
			free(art_path);
			return NULL;
		}
	}
	image_free(imsrc);
	if( !art_path )
	{
		DPRINTF(E_WARN, L_METADATA, "Invalid embedded album art in %s\n", basename((char *)path));
		return NULL;
	}
	DPRINTF(E_DEBUG, L_METADATA, "Found new embedded album art in %s\n", basename((char *)path));
	last_hash = hash;
	strcpy(last_path, art_path);

	return(art_path);
}

char *
check_for_album_file(char * dir)
{
	char * file = malloc(PATH_MAX);
	struct album_art_name_s * album_art_name;
	image * imsrc;
	int width=0, height=0;
	char * art_file;

	for( album_art_name = album_art_names; album_art_name; album_art_name = album_art_name->next )
	{
		sprintf(file, "%s/%s", dir, album_art_name->name);
		if( access(file, R_OK) == 0 )
		{
			imsrc = image_new_from_jpeg(file, 1, NULL, 0);
			width = imsrc->width;
			height = imsrc->height;
			if( width > 160 || height > 160 )
			{
				art_file = file;
				file = save_resized_album_art(imsrc, art_file);
				free(art_file);
			}
			image_free(imsrc);
			return(file);
		}
	}
	free(file);
	return NULL;
}

sqlite_int64
find_album_art(const char * path, char * dlna_pn, const char * image_data, int image_size)
{
	char * album_art = NULL;
	char * sql;
	char ** result;
	int cols, rows;
	sqlite_int64 ret = 0;
	char * mypath = strdup(path);

	if( (image_size && (album_art = check_embedded_art(path, image_data, image_size))) ||
	    (album_art = check_for_album_file(dirname(mypath))) )
	{
		strcpy(dlna_pn, "JPEG_TN");
		sql = sqlite3_mprintf("SELECT ID from ALBUM_ART where PATH = '%q'", album_art ? album_art : path);
		if( (sql_get_table(db, sql, &result, &rows, &cols) == SQLITE_OK) && rows )
		{
			ret = strtoll(result[1], NULL, 10);
		}
		else
		{
			sqlite3_free(sql);
			sql = sqlite3_mprintf("INSERT into ALBUM_ART (PATH) VALUES ('%q')", album_art);
			if( sql_exec(db, sql) == SQLITE_OK )
				ret = sqlite3_last_insert_rowid(db);
		}
		sqlite3_free_table(result);
		sqlite3_free(sql);
	}
	if( album_art )
		free(album_art);
	free(mypath);

	return ret;
}
