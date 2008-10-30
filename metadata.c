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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <sqlite3.h>
#include <taglib/tag_c.h>
#include <libexif/exif-loader.h>
#include <dlna.h>

#include "upnpglobalvars.h"
#include "metadata.h"
#include "sql.h"

#define FLAG_ARTIST 0x01

char *
trim(char *str)
{
        if (!str)
                return(NULL);
        int i;
        for (i=0; i <= strlen(str) && (isspace(str[i]) || str[i] == '"'); i++) {
		str++;
	}
        for (i=(strlen(str)-1); i >= 0 && (isspace(str[i]) || str[i] == '"'); i--) {
                str[i] = '\0';
        }
        return str;
}

char *
modifyString(char * string, const char * before, const char * after, short like)
{
	int oldlen, newlen, chgcnt = 0;
	char *s, *p, *t;

	oldlen = strlen(before);
	newlen = strlen(after);
	if( newlen > oldlen )
	{
		s = string;
		while( (p = strstr(s, before)) )
		{
			chgcnt++;
			s = p+oldlen;
		}
		string = realloc(string, strlen(string)+((newlen-oldlen)*chgcnt)+1);
	}

	s = string;
	while( s )
	{
		p = strcasestr(s, before);
		if( !p )
			return string;
		if( like )
		{
			t = p+oldlen;
			while( isspace(*t) )
				t++;
			if( *t == '"' )
				while( *++t != '"' )
					continue;
			memmove(t+1, t, strlen(t)+1);
			*t = '%';
		}
		memmove(p + newlen, p + oldlen, strlen(p + oldlen) + 1);
		memcpy(p, after, newlen);
		s = p + newlen;
	}
	if( newlen < oldlen )
		string = realloc(string, strlen(string)+1);

	return string;
}

void
strip_ext(char * name)
{
	if( rindex(name, '.') )
		*rindex(name, '.') = '\0';
}

sqlite_int64
GetFolderMetadata(const char * name, const char * artist)
{
	char * sql;
	int ret;

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (TITLE, ARTIST) "
				"VALUES"
				" ('%q', %Q);",
				name, artist);

	if( sql_exec(db, sql) != SQLITE_OK )
		ret = 0;
	else
		ret = sqlite3_last_insert_rowid(db);
	sqlite3_free(sql);

	return ret;
}

sqlite_int64
GetAudioMetadata(const char * path, char * name)
{
	size_t size = 0;
	char date[16], duration[16], dlna_pn[24], mime[16];
	struct stat file;
	int seconds, minutes;
	sqlite_int64 ret;
	TagLib_File *audio_file;
	TagLib_Tag *tag;
	const TagLib_AudioProperties *properties;
	char *sql;
	char *zErrMsg = NULL;
	char *title, *artist, *album, *genre, *comment;
	int free_flags = 0;

	if ( stat(path, &file) == 0 )
		size = file.st_size;
	else
		return 0;
	strip_ext(name);

	taglib_set_strings_unicode(1);

	audio_file = taglib_file_new(path);
	if(audio_file == NULL)
		return 0;

	tag = taglib_file_tag(audio_file);
	properties = taglib_file_audioproperties(audio_file);

	seconds = taglib_audioproperties_length(properties) % 60;
	minutes = (taglib_audioproperties_length(properties) - seconds) / 60;

	date[0] = '\0';
	if( taglib_tag_year(tag) )
		sprintf(date, "%04d-01-01", taglib_tag_year(tag));
	sprintf(duration, "%d:%02d:%02d.000", minutes/60, minutes, seconds);

	title = taglib_tag_title(tag);
	if( strlen(title) )
	{
		title = trim(title);
		if( index(title, '&') )
		{
			title = modifyString(strdup(title), "&", "&amp;amp;", 0);
		}
	}
	else
	{
		title = name;
	}
	artist = taglib_tag_artist(tag);
	if( strlen(artist) )
	{
		artist = trim(artist);
		if( index(artist, '&') )
		{
			free_flags |= FLAG_ARTIST;
			artist = modifyString(strdup(artist), "&", "&amp;amp;", 0);
		}
	}
	else
	{
		artist = NULL;
	}
	album = taglib_tag_album(tag);
	if( strlen(album) )
	{
		album = trim(album);
		if( index(album, '&') )
		{
			album = modifyString(strdup(album), "&", "&amp;amp;", 0);
		}
	}
	else
	{
		album = NULL;
	}
	genre = taglib_tag_genre(tag);
	if( strlen(genre) )
	{
		genre = trim(genre);
		if( index(genre, '&') )
		{
			genre = modifyString(strdup(genre), "&", "&amp;amp;", 0);
		}
	}
	else
	{
		genre = NULL;
	}
	comment = taglib_tag_comment(tag);
	if( strlen(comment) )
	{
		comment = trim(comment);
		if( index(comment, '&') )
		{
			comment = modifyString(strdup(comment), "&", "&amp;amp;", 0);
		}
	}
	else
	{
		comment = NULL;
	}
		

	if( 1 ) // Switch on audio file type
	{
		strcpy(dlna_pn, "MP3;DLNA.ORG_OP=01");
		strcpy(mime, "audio/mpeg");
	}

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (SIZE, DURATION, CHANNELS, BITRATE, SAMPLERATE, DATE,"
				"  TITLE, ARTIST, ALBUM, GENRE, COMMENT, TRACK, DLNA_PN, MIME) "
				"VALUES"
				" (%d, '%s', %d, %d, %d, %Q, %Q, %Q, %Q, %Q, %Q, %d, '%s', '%s');",
				size, duration, taglib_audioproperties_channels(properties),
				taglib_audioproperties_bitrate(properties)*1024,
				taglib_audioproperties_samplerate(properties),
				strlen(date) ? date : NULL,
				title,
				artist,
				album,
				genre,
				comment,
				taglib_tag_track(tag),
				dlna_pn, mime);

	taglib_tag_free_strings();
	taglib_file_free(audio_file);

	if( free_flags & FLAG_ARTIST )
		free(artist);

	//DEBUG printf("SQL: %s\n", sql);
	if( sqlite3_exec(db, sql, 0, 0, &zErrMsg) != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'! [%s]\n", path, zErrMsg);
		if (zErrMsg)
			sqlite3_free(zErrMsg);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	sqlite3_free(sql);
	return ret;
}

sqlite_int64
GetImageMetadata(const char * path, char * name)
{
	ExifData *ed;
	ExifEntry *e = NULL;
	ExifTag tag;
	int width=0, height=0, thumb=0;
	size_t size;
	char date[64], make[32], model[64];
	char b[1024];
	struct stat file;
	sqlite_int64 ret;
	char *sql;
	char *zErrMsg = NULL;
	metadata_t m;
	memset(&m, '\0', sizeof(metadata_t));

	date[0] = '\0';
	model[0] = '\0';

	//DEBUG printf("Parsing %s...\n", path);
	if ( stat(path, &file) == 0 )
		size = file.st_size;
	else
		return 0;
	strip_ext(name);
	//DEBUG printf(" * size: %d\n", size);

	/* MIME hard-coded to JPEG for now, until we add PNG support */
	asprintf(&m.mime, "image/jpeg");

	ExifLoader * l = exif_loader_new();
	exif_loader_write_file(l, path);
	ed = exif_loader_get_data(l);
	exif_loader_unref(l);

	tag = EXIF_TAG_PIXEL_X_DIMENSION;
	e = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], tag);
	if( e )
		width = atoi( exif_entry_get_value(e, b, sizeof(b)) );

	tag = EXIF_TAG_PIXEL_Y_DIMENSION;
	e = exif_content_get_entry (ed->ifd[EXIF_IFD_EXIF], tag);
	if( e )
		height = atoi( exif_entry_get_value(e, b, sizeof(b)) );
	//DEBUG printf(" * resolution: %dx%d\n", width, height);

	tag = EXIF_TAG_DATE_TIME_ORIGINAL;
	e = exif_content_get_entry (ed->ifd[EXIF_IFD_EXIF], tag);
	if( e ) {
		strncpy(date, exif_entry_get_value(e, b, sizeof(b)), sizeof(date));
		if( strlen(date) > 10 )
		{
			date[4] = '-';
			date[7] = '-';
			date[10] = 'T';
		}
		else {
			strcpy(date, "0000-00-00");
		}
	}
	else {
		strcpy(date, "0000-00-00");
	}
	//DEBUG printf(" * date: %s\n", date);

	model[0] = '\0';
	tag = EXIF_TAG_MAKE;
	e = exif_content_get_entry (ed->ifd[EXIF_IFD_0], tag);
	if( e )
	{
		strncpy(make, exif_entry_get_value(e, b, sizeof(b)), sizeof(make));
		tag = EXIF_TAG_MODEL;
		e = exif_content_get_entry (ed->ifd[EXIF_IFD_0], tag);
		if( e )
		{
			strncpy(model, exif_entry_get_value(e, b, sizeof(b)), sizeof(model));
			if( !strcasestr(model, make) )
				snprintf(model, sizeof(model), "%s %s", make, exif_entry_get_value(e, b, sizeof(b)));
		}
	}
	if( !strlen(model) )
		strcpy(model, "Unknown");
	//DEBUG printf(" * model: %s\n", model);

	if( ed->size )
		thumb = 1;
	else
		thumb = 0;
	//DEBUG printf(" * thumbnail: %d\n", thumb);

	exif_data_unref(ed);

	if( width <= 640 && height <= 480 )
		asprintf(&m.dlna_pn, "JPEG_SM;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
	else if( width <= 1024 && height <= 768 )
		asprintf(&m.dlna_pn, "JPEG_MED;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
	else if( width <= 4096 && height <= 4096 )
		asprintf(&m.dlna_pn, "JPEG_LRG;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
	else
		asprintf(&m.dlna_pn, "JPEG_XL");
	asprintf(&m.resolution, "%dx%d", width, height);

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (TITLE, SIZE, DATE, RESOLUTION, THUMBNAIL, CREATOR, DLNA_PN, MIME) "
				"VALUES"
				" ('%q', %d, '%s', %Q, %d, '%q', %Q, %Q);",
				name, size, date, m.resolution, thumb, model, m.dlna_pn, m.mime);
	//DEBUG printf("SQL: %s\n", sql);
	if( sqlite3_exec(db, sql, 0, 0, &zErrMsg) != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'! [%s]\n", path, zErrMsg);
		if (zErrMsg)
			sqlite3_free(zErrMsg);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	sqlite3_free(sql);
	if( m.resolution )
		free(m.resolution);
	if( m.dlna_pn )
		free(m.dlna_pn);
	if( m.mime )
		free(m.mime);
	return ret;
}

sqlite_int64
GetVideoMetadata(const char * path, char * name)
{
	size_t size = 0;
	struct stat file;
	dlna_t *dlna;
	dlna_profile_t *p;
	dlna_item_t *item;
	char *sql;
	char *zErrMsg = NULL;
	int ret;
	metadata_t m;
	memset(&m, '\0', sizeof(m));

	//DEBUG printf("Parsing %s...\n", path);
	if ( stat(path, &file) == 0 )
		size = file.st_size;
	strip_ext(name);
	//DEBUG printf(" * size: %d\n", size);

	dlna = dlna_init();
	dlna_register_all_media_profiles(dlna);

	item = dlna_item_new (dlna, path);
	if (item)
	{
		if (item->properties)
		{
			if( strlen(item->properties->duration) )
				m.duration = item->properties->duration;
			if( item->properties->bitrate )
				asprintf(&m.bitrate, "%d", item->properties->bitrate);
			if( item->properties->sample_frequency )
				asprintf(&m.frequency, "%d", item->properties->sample_frequency);
			if( item->properties->bps )
				asprintf(&m.bps, "%d", item->properties->bps);
			if( item->properties->channels )
				asprintf(&m.channels, "%d", item->properties->channels);
			m.resolution = item->properties->resolution;
		}
	}
  
	p = dlna_guess_media_profile (dlna, path);
	if (p)
	{
		m.mime = (char *)p->mime;
		asprintf(&m.dlna_pn, "%s;DLNA.ORG_OP=01;DLNA.ORG_CI=0", p->id);
	}
	else
		printf ("Unknown format [%s]\n", path);

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (SIZE, DURATION, CHANNELS, BITRATE, SAMPLERATE, RESOLUTION,"
				"  TITLE, DLNA_PN, MIME) "
				"VALUES"
				" (%d, %Q, %d, %d, %d, %Q, '%q', %Q, '%q');",
				size, m.duration,
				item->properties ? item->properties->channels : 0,
				item->properties ? item->properties->bitrate : 0,
				item->properties ? item->properties->sample_frequency : 0,
				m.resolution, name,
				m.dlna_pn, m.mime);
/*	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (TITLE, SIZE, MIME) "
				"VALUES"
				" ('%q', %d, %Q);",
				name, size, "video/mpeg");*/
	//DEBUG printf("SQL: %s\n", sql);
	if( sqlite3_exec(db, sql, 0, 0, &zErrMsg) != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'! [%s]\n", path, zErrMsg);
		if (zErrMsg)
			sqlite3_free(zErrMsg);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	sqlite3_free(sql);
	dlna_item_free(item);
	dlna_uninit(dlna);
	if( m.dlna_pn )
		free(m.dlna_pn);
	if( m.bitrate )
		free(m.bitrate);
	if( m.frequency )
		free(m.frequency);
	if( m.bps )
		free(m.bps);
	if( m.channels )
		free(m.channels);

	return ret;
}
