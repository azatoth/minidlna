/* MiniDLNA media server
 * Copyright (C) 2008  Justin Maggard
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <libgen.h>
#include <setjmp.h>
#include <errno.h>

#include <jpeglib.h>

#include "upnpglobalvars.h"
#include "albumart.h"
#include "sql.h"
#include "utils.h"
#include "image_utils.h"
#include "log.h"

int
art_cache_exists(const char * orig_path, char ** cache_file)
{
	asprintf(cache_file, "%s/art_cache%s", db_path, orig_path);
	strcpy(strchr(*cache_file, '\0')-4, ".jpg");

	return (!access(*cache_file, F_OK));
}

char *
save_resized_album_art(image_s * imsrc, const char * path)
{
	int dstw, dsth;
	image_s * imdst;
	char * cache_file;
	char * cache_dir;

	if( !imsrc )
		return NULL;

	if( art_cache_exists(path, &cache_file) )
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
void
update_if_album_art(const char * path)
{
	char * dir;
	char * match = NULL;
	char * file = NULL;
	int ncmp = 0;
	int album_art;
	DIR * dh;
	struct dirent *dp;
	enum file_types type = TYPE_UNKNOWN;
	sqlite_int64 art_id = 0;

	match = strdup(basename((char *)path));
	/* Check if this file name matches a specific audio or video file */
	if( ends_with(match, ".cover.jpg") )
	{
		ncmp = strlen(match)-10;
	}
	else
	{
		ncmp = strrchr(match, '.') - match;
	}
	/* Check if this file name matches one of the default album art names */
	album_art = is_album_art(match);

	dir = dirname(strdup(path));
	dh = opendir(dir);
	if( !dh )
		return;
	while ((dp = readdir(dh)) != NULL)
	{
		switch( dp->d_type )
		{
			case DT_REG:
				type = TYPE_FILE;
				break;
			case DT_LNK:
			case DT_UNKNOWN:
				asprintf(&file, "%s/%s", dir, dp->d_name);
				type = resolve_unknown_type(file, ALL_MEDIA);
				free(file);
				break;
			default:
				type = TYPE_UNKNOWN;
				break;
		}
		if( type != TYPE_FILE )
			continue;
		if( (*(dp->d_name) != '.') &&
		    (is_video(dp->d_name) || is_audio(dp->d_name)) &&
		    (album_art || strncmp(dp->d_name, match, ncmp) == 0) )
		{
			DPRINTF(E_DEBUG, L_METADATA, "New file %s looks like cover art for %s\n", path, dp->d_name);
			asprintf(&file, "%s/%s", dir, dp->d_name);
			art_id = find_album_art(file, NULL, 0);
			if( sql_exec(db, "UPDATE DETAILS set ALBUM_ART = %lld where PATH = '%q'", art_id, file) != SQLITE_OK )
				DPRINTF(E_WARN, L_METADATA, "Error setting %s as cover art for %s\n", match, dp->d_name);
			free(file);
		}
	}
	closedir(dh);
	
	free(dir);
	free(match);
}

char *
check_embedded_art(const char * path, const char * image_data, int image_size)
{
	int width = 0, height = 0;
	char * art_path = NULL;
	char * cache_dir;
	FILE * dstfile;
	image_s * imsrc;
	static char last_path[PATH_MAX];
	static unsigned int last_hash = 0;
	static int last_success = 0;
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
		if( !last_success )
			return NULL;
		art_cache_exists(path, &art_path);
		if( link(last_path, art_path) == 0 )
		{
			return(art_path);
		}
		else
		{
			if( errno == ENOENT )
			{
				cache_dir = strdup(art_path);
				make_dir(dirname(cache_dir), S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
				free(cache_dir);
				if( link(last_path, art_path) == 0 )
					return(art_path);
			}
			DPRINTF(E_WARN, L_METADATA, "Linking %s to %s failed [%s]\n", art_path, last_path, strerror(errno));
			free(art_path);
			art_path = NULL;
		}
	}
	last_hash = hash;

	imsrc = image_new_from_jpeg(NULL, 0, image_data, image_size, 1);
	if( !imsrc )
	{
		last_success = 0;
		return NULL;
	}
	width = imsrc->width;
	height = imsrc->height;

	if( width > 160 || height > 160 )
	{
		art_path = save_resized_album_art(imsrc, path);
	}
	else if( width > 0 && height > 0 )
	{
		size_t nwritten;
		if( art_cache_exists(path, &art_path) )
			goto end_art;
		cache_dir = strdup(art_path);
		make_dir(dirname(cache_dir), S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
		free(cache_dir);
		dstfile = fopen(art_path, "w");
		if( !dstfile )
		{
			free(art_path);
			art_path = NULL;
			goto end_art;
		}
		nwritten = fwrite((void *)image_data, 1, image_size, dstfile);
		fclose(dstfile);
		if( nwritten != image_size )
		{
			DPRINTF(E_WARN, L_METADATA, "Embedded art error: wrote %d/%d bytes\n", nwritten, image_size);
			remove(art_path);
			free(art_path);
			art_path = NULL;
			goto end_art;
		}
	}
end_art:
	image_free(imsrc);
	if( !art_path )
	{
		DPRINTF(E_WARN, L_METADATA, "Invalid embedded album art in %s\n", basename((char *)path));
		last_success = 0;
		return NULL;
	}
	DPRINTF(E_DEBUG, L_METADATA, "Found new embedded album art in %s\n", basename((char *)path));
	last_success = 1;
	strcpy(last_path, art_path);

	return(art_path);
}

char *
check_for_album_file(const char * dir, const char * path)
{
	char file[MAXPATHLEN];
	struct album_art_name_s * album_art_name;
	image_s * imsrc = NULL;
	int width=0, height=0;
	char * art_file;

	/* First look for file-specific cover art */
	snprintf(file, sizeof(file), "%s.cover.jpg", path);
	if( access(file, R_OK) == 0 )
	{
		if( art_cache_exists(file, &art_file) )
			goto existing_file;
		free(art_file);
		imsrc = image_new_from_jpeg(file, 1, NULL, 0, 1);
		if( imsrc )
			goto found_file;
	}
	snprintf(file, sizeof(file), "%s", path);
	art_file = strrchr(file, '.');
	if( art_file )
		strcpy(art_file, ".jpg");
	if( access(file, R_OK) == 0 )
	{
		if( art_cache_exists(file, &art_file) )
			goto existing_file;
		free(art_file);
		imsrc = image_new_from_jpeg(file, 1, NULL, 0, 1);
		if( imsrc )
			goto found_file;
	}

	/* Then fall back to possible generic cover art file names */
	for( album_art_name = album_art_names; album_art_name; album_art_name = album_art_name->next )
	{
		snprintf(file, sizeof(file), "%s/%s", dir, album_art_name->name);
		if( access(file, R_OK) == 0 )
		{
			if( art_cache_exists(file, &art_file) )
			{
existing_file:
				return art_file;
			}
			free(art_file);
			imsrc = image_new_from_jpeg(file, 1, NULL, 0, 1);
			if( !imsrc )
				continue;
found_file:
			width = imsrc->width;
			height = imsrc->height;
			if( width > 160 || height > 160 )
				art_file = save_resized_album_art(imsrc, file);
			else
				art_file = strdup(file);
			image_free(imsrc);
			return(art_file);
		}
	}
	return NULL;
}

sqlite_int64
find_album_art(const char * path, const char * image_data, int image_size)
{
	char * album_art = NULL;
	char * sql;
	char ** result;
	int cols, rows;
	sqlite_int64 ret = 0;
	char * mypath;
	const char * dir;
	struct stat st;

	if( stat(path, &st) == 0 && S_ISDIR(st.st_mode) )
	{
		mypath = NULL;
		dir = path;
	}
	else
	{
		mypath = strdup(path);
		dir = dirname(mypath);
	}

	if( (image_size && (album_art = check_embedded_art(path, image_data, image_size))) ||
	    (album_art = check_for_album_file(dir, path)) )
	{
		sql = sqlite3_mprintf("SELECT ID from ALBUM_ART where PATH = '%q'", album_art ? album_art : path);
		if( (sql_get_table(db, sql, &result, &rows, &cols) == SQLITE_OK) && rows )
		{
			ret = strtoll(result[1], NULL, 10);
		}
		else
		{
			if( sql_exec(db, "INSERT into ALBUM_ART (PATH) VALUES ('%q')", album_art) == SQLITE_OK )
				ret = sqlite3_last_insert_rowid(db);
		}
		sqlite3_free_table(result);
		sqlite3_free(sql);
	}
	free(album_art);
	free(mypath);

	return ret;
}
