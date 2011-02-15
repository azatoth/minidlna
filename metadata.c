/* MiniDLNA media server
 * Copyright (C) 2008-2009  Justin Maggard
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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libexif/exif-loader.h>
#include "image_utils.h"
#include <jpeglib.h>
#include <setjmp.h>
#include <avutil.h>
#include <avcodec.h>
#include <avformat.h>
#include "tagutils/tagutils.h"

#include "upnpglobalvars.h"
#include "upnpreplyparse.h"
#include "metadata.h"
#include "albumart.h"
#include "utils.h"
#include "sql.h"
#include "log.h"

#ifndef FF_PROFILE_H264_BASELINE
#define FF_PROFILE_H264_BASELINE 66
#endif
#ifndef FF_PROFILE_H264_MAIN
#define FF_PROFILE_H264_MAIN 77
#endif
#ifndef FF_PROFILE_H264_HIGH
#define FF_PROFILE_H264_HIGH 100
#endif

#define FLAG_TITLE	0x00000001
#define FLAG_ARTIST	0x00000002
#define FLAG_ALBUM	0x00000004
#define FLAG_GENRE	0x00000008
#define FLAG_COMMENT	0x00000010
#define FLAG_CREATOR	0x00000020
#define FLAG_DATE	0x00000040
#define FLAG_DLNA_PN	0x00000080
#define FLAG_MIME	0x00000100
#define FLAG_DURATION	0x00000200
#define FLAG_RESOLUTION	0x00000400
#define FLAG_BITRATE	0x00000800
#define FLAG_FREQUENCY	0x00001000
#define FLAG_BPS	0x00002000
#define FLAG_CHANNELS	0x00004000

/* Audio profile flags */
enum audio_profiles {
	PROFILE_AUDIO_UNKNOWN,
	PROFILE_AUDIO_MP3,
	PROFILE_AUDIO_AC3,
	PROFILE_AUDIO_WMA_BASE,
	PROFILE_AUDIO_WMA_FULL,
	PROFILE_AUDIO_WMA_PRO,
	PROFILE_AUDIO_MP2,
	PROFILE_AUDIO_PCM,
	PROFILE_AUDIO_AAC,
	PROFILE_AUDIO_AAC_MULT5,
	PROFILE_AUDIO_AMR
};

/* This function shamelessly copied from libdlna */
#define MPEG_TS_SYNC_CODE 0x47
#define MPEG_TS_PACKET_LENGTH 188 /* prepends 4 bytes to TS packet */
#define MPEG_TS_PACKET_LENGTH_DLNA 192 /* prepends 4 bytes to TS packet */
int
dlna_timestamp_is_present(const char * filename)
{
	unsigned char buffer[2*MPEG_TS_PACKET_LENGTH_DLNA+1];
	int fd, i;

	/* read file header */
	fd = open(filename, O_RDONLY);
	read(fd, buffer, MPEG_TS_PACKET_LENGTH_DLNA*2);
	close(fd);
	for( i=0; i < MPEG_TS_PACKET_LENGTH_DLNA; i++ )
	{
		if( buffer[i] == MPEG_TS_SYNC_CODE )
		{
			if (buffer[i + MPEG_TS_PACKET_LENGTH_DLNA] == MPEG_TS_SYNC_CODE)
			{
				if (buffer[i+MPEG_TS_PACKET_LENGTH] == 0x00 &&
				    buffer[i+MPEG_TS_PACKET_LENGTH+1] == 0x00 &&
				    buffer[i+MPEG_TS_PACKET_LENGTH+2] == 0x00 &&
				    buffer[i+MPEG_TS_PACKET_LENGTH+3] == 0x00)
					break;
				else
					return 1;
			}
		}
	}
	return 0;
}

#ifdef TIVO_SUPPORT
int
is_tivo_file(const char * path)
{
	unsigned char buf[5];
	unsigned char hdr[5] = { 'T','i','V','o','\0' };
	int fd;

	/* read file header */
	fd = open(path, O_RDONLY);
	read(fd, buf, 5);
	close(fd);

	return( !memcmp(buf, hdr, 5) );
}
#endif

void
check_for_captions(const char * path, sqlite_int64 detailID)
{
	char * sql;
	char * file = malloc(PATH_MAX);
	char **result;
	int ret, rows;

	sprintf(file, "%s", path);
	strip_ext(file);

	/* If we weren't given a detail ID, look for one. */
	if( !detailID )
	{
		sql = sqlite3_mprintf("SELECT ID from DETAILS where PATH glob '%q.*'"
		                      " and MIME glob 'video/*' limit 1", file);
		ret = sql_get_table(db, sql, &result, &rows, NULL);
		if( ret == SQLITE_OK )
		{
			if( rows )
			{
				detailID = strtoll(result[1], NULL, 10);
				//DEBUG DPRINTF(E_DEBUG, L_METADATA, "New file %s looks like a caption file.\n", path);
			}
			/*else
			{
				DPRINTF(E_DEBUG, L_METADATA, "No file found for caption %s.\n", path);
			}*/
			sqlite3_free_table(result);
		}
		sqlite3_free(sql);
		if( !detailID )
			goto no_source_video;
	}

	strcat(file, ".srt");
	if( access(file, R_OK) == 0 )
	{
		sql_exec(db, "INSERT into CAPTIONS"
		             " (ID, PATH) "
		             "VALUES"
		             " (%lld, %Q)", detailID, file);
	}
no_source_video:
	free(file);
}

void
parse_nfo(const char * path, metadata_t * m)
{
	FILE *nfo;
	char buf[65536];
	struct NameValueParserData xml;
	struct stat file;
	size_t nread;
	char *val, *val2;

	if( stat(path, &file) != 0 ||
	    file.st_size > 65536 )
	{
		DPRINTF(E_INFO, L_METADATA, "Not parsing very large .nfo file %s\n", path);
		return;
	}
	DPRINTF(E_DEBUG, L_METADATA, "Parsing .nfo file: %s\n", path);
	nfo = fopen(path, "r");
	if( !nfo )
		return;
	nread = fread(&buf, 1, sizeof(buf), nfo);
	
	ParseNameValue(buf, nread, &xml);

	//printf("\ttype: %s\n", GetValueFromNameValueList(&xml, "rootElement"));
	val = GetValueFromNameValueList(&xml, "title");
	if( val )
	{
		val2 = GetValueFromNameValueList(&xml, "episodetitle");
		if( val2 )
			asprintf(&m->title, "%s - %s", val, val2);
		else
			m->title = strdup(val);
	}

	val = GetValueFromNameValueList(&xml, "plot");
	if( val )
		m->comment = strdup(val);

	val = GetValueFromNameValueList(&xml, "capturedate");
	if( val )
		m->date = strdup(val);

	val = GetValueFromNameValueList(&xml, "genre");
	if( val )
		m->genre = strdup(val);

	ClearNameValueList(&xml);
	fclose(nfo);
}

void
free_metadata(metadata_t * m, uint32_t flags)
{
	if( m->title && (flags & FLAG_TITLE) )
		free(m->title);
	if( m->artist && (flags & FLAG_ARTIST) )
		free(m->artist);
	if( m->album && (flags & FLAG_ALBUM) )
		free(m->album);
	if( m->genre && (flags & FLAG_GENRE) )
		free(m->genre);
	if( m->creator && (flags & FLAG_CREATOR) )
		free(m->creator);
	if( m->date && (flags & FLAG_DATE) )
		free(m->date);
	if( m->comment && (flags & FLAG_COMMENT) )
		free(m->comment);
	if( m->dlna_pn && (flags & FLAG_DLNA_PN) )
		free(m->dlna_pn);
	if( m->mime && (flags & FLAG_MIME) )
		free(m->mime);
	if( m->duration && (flags & FLAG_DURATION) )
		free(m->duration);
	if( m->resolution && (flags & FLAG_RESOLUTION) )
		free(m->resolution);
	if( m->bitrate && (flags & FLAG_BITRATE) )
		free(m->bitrate);
	if( m->frequency && (flags & FLAG_FREQUENCY) )
		free(m->frequency);
	if( m->bps && (flags & FLAG_BPS) )
		free(m->bps);
	if( m->channels && (flags & FLAG_CHANNELS) )
		free(m->channels);
}

sqlite_int64
GetFolderMetadata(const char * name, const char * path, const char * artist, const char * genre, const char * album_art)
{
	int ret;

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (TITLE, PATH, CREATOR, ARTIST, GENRE, ALBUM_ART) "
	                   "VALUES"
	                   " ('%q', %Q, %Q, %Q, %Q, %lld);",
	                   name, path, artist, artist, genre,
	                   album_art ? strtoll(album_art, NULL, 10) : 0);
	if( ret != SQLITE_OK )
		ret = 0;
	else
		ret = sqlite3_last_insert_rowid(db);

	return ret;
}

sqlite_int64
GetAudioMetadata(const char * path, char * name)
{
	char type[4];
	static char lang[6] = { '\0' };
	struct stat file;
	sqlite_int64 ret;
	char *esc_tag;
	int i;
	sqlite_int64 album_art = 0;
	struct song_metadata song;
	metadata_t m;
	uint32_t free_flags = FLAG_MIME|FLAG_DURATION|FLAG_DLNA_PN|FLAG_DATE;
	memset(&m, '\0', sizeof(metadata_t));

	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);

	if( ends_with(path, ".mp3") )
	{
		strcpy(type, "mp3");
		m.mime = strdup("audio/mpeg");
	}
	else if( ends_with(path, ".m4a") || ends_with(path, ".mp4") ||
	         ends_with(path, ".aac") || ends_with(path, ".m4p") )
	{
		strcpy(type, "aac");
		m.mime = strdup("audio/mp4");
	}
	else if( ends_with(path, ".3gp") )
	{
		strcpy(type, "aac");
		m.mime = strdup("audio/3gpp");
	}
	else if( ends_with(path, ".wma") || ends_with(path, ".asf") )
	{
		strcpy(type, "asf");
		m.mime = strdup("audio/x-ms-wma");
	}
	else if( ends_with(path, ".flac") || ends_with(path, ".fla") || ends_with(path, ".flc") )
	{
		strcpy(type, "flc");
		m.mime = strdup("audio/x-flac");
	}
	else if( ends_with(path, ".wav") )
	{
		strcpy(type, "wav");
		m.mime = strdup("audio/x-wav");
	}
	else if( ends_with(path, ".ogg") )
	{
		strcpy(type, "ogg");
		m.mime = strdup("application/ogg");
	}
	else if( ends_with(path, ".pcm") )
	{
		strcpy(type, "pcm");
		m.mime = strdup("audio/L16");
	}
	else
	{
		DPRINTF(E_WARN, L_GENERAL, "Unhandled file extension on %s\n", path);
		return 0;
	}

	if( !(*lang) )
	{
		if( !getenv("LANG") )
			strcpy(lang, "en_US");
		else
			strncpy(lang, getenv("LANG"), 5);
		lang[5] = '\0';
	}

	if( readtags((char *)path, &song, &file, lang, type) != 0 )
	{
		DPRINTF(E_WARN, L_GENERAL, "Cannot extract tags from %s!\n", path);
        	freetags(&song);
		free_metadata(&m, free_flags);
		return 0;
	}

	if( song.dlna_pn )
		asprintf(&m.dlna_pn, "%s;DLNA.ORG_OP=01;DLNA.ORG_CI=0", song.dlna_pn);
	if( song.year )
		asprintf(&m.date, "%04d-01-01", song.year);
	asprintf(&m.duration, "%d:%02d:%02d.%03d",
	                      (song.song_length/3600000),
	                      (song.song_length/60000%60),
	                      (song.song_length/1000%60),
	                      (song.song_length%1000));
	if( song.title && *song.title )
	{
		m.title = trim(song.title);
		if( (esc_tag = escape_tag(m.title)) )
		{
			free_flags |= FLAG_TITLE;
			m.title = esc_tag;
		}
	}
	else
	{
		m.title = name;
	}
	for( i=ROLE_START; i<N_ROLE; i++ )
	{
		if( song.contributor[i] && *song.contributor[i] )
		{
			m.creator = trim(song.contributor[i]);
			if( strlen(m.creator) > 48 )
			{
				free_flags |= FLAG_ARTIST;
				m.creator = strdup("Various Artists");
			}
			else if( (esc_tag = escape_tag(m.creator)) )
			{
				free_flags |= FLAG_ARTIST;
				m.creator = esc_tag;
			}
			m.artist = m.creator;
			break;
		}
	}
	/* If there is a band associated with the album, use it for virtual containers. */
	if( (i != ROLE_BAND) && (i != ROLE_ALBUMARTIST) )
	{
	        if( song.contributor[ROLE_BAND] && *song.contributor[ROLE_BAND] )
		{
			i = ROLE_BAND;
			m.artist = trim(song.contributor[i]);
			if( strlen(m.artist) > 48 )
			{
				free_flags |= FLAG_CREATOR;
				m.artist = strdup("Various Artists");
			}
			else if( (esc_tag = escape_tag(m.artist)) )
			{
				free_flags |= FLAG_CREATOR;
				m.artist = esc_tag;
			}
		}
	}
	if( song.album && *song.album )
	{
		m.album = trim(song.album);
		if( (esc_tag = escape_tag(m.album)) )
		{
			free_flags |= FLAG_ALBUM;
			m.album = esc_tag;
		}
	}
	if( song.genre && *song.genre )
	{
		m.genre = trim(song.genre);
		if( (esc_tag = escape_tag(m.genre)) )
		{
			free_flags |= FLAG_GENRE;
			m.genre = esc_tag;
		}
	}
	if( song.comment && *song.comment )
	{
		m.comment = trim(song.comment);
		if( (esc_tag = escape_tag(m.comment)) )
		{
			free_flags |= FLAG_COMMENT;
			m.comment = esc_tag;
		}
	}

	album_art = find_album_art(path, song.image, song.image_size);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, SIZE, TIMESTAMP, DURATION, CHANNELS, BITRATE, SAMPLERATE, DATE,"
	                   "  TITLE, CREATOR, ARTIST, ALBUM, GENRE, COMMENT, DISC, TRACK, DLNA_PN, MIME, ALBUM_ART) "
	                   "VALUES"
	                   " (%Q, %lld, %ld, '%s', %d, %d, %d, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %d, %d, %Q, '%s', %lld);",
	                   path, file.st_size, file.st_mtime, m.duration, song.channels, song.bitrate, song.samplerate, m.date,
	                   m.title, m.creator, m.artist, m.album, m.genre, m.comment, song.disc, song.track,
	                   m.dlna_pn, song.mime?song.mime:m.mime, album_art);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
        freetags(&song);
	free_metadata(&m, free_flags);

	return ret;
}

/* For libjpeg error handling */
jmp_buf setjmp_buffer;
static void
libjpeg_error_handler(j_common_ptr cinfo)
{
	cinfo->err->output_message (cinfo);
	longjmp(setjmp_buffer, 1);
	return;
}

sqlite_int64
GetImageMetadata(const char * path, char * name)
{
	ExifData *ed;
	ExifEntry *e = NULL;
	ExifLoader *l;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *infile;
	int width=0, height=0, thumb=0;
	char make[32], model[64] = {'\0'};
	char b[1024];
	struct stat file;
	sqlite_int64 ret;
	image * imsrc;
	metadata_t m;
	uint32_t free_flags = 0xFFFFFFFF;
	memset(&m, '\0', sizeof(metadata_t));

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Parsing %s...\n", path);
	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %jd\n", file.st_size);

	/* MIME hard-coded to JPEG for now, until we add PNG support */
	asprintf(&m.mime, "image/jpeg");

	l = exif_loader_new();
	exif_loader_write_file(l, path);
	ed = exif_loader_get_data(l);
	exif_loader_unref(l);
	if( !ed )
		goto no_exifdata;

	e = exif_content_get_entry (ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_DATE_TIME_ORIGINAL);
	if( e || (e = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_DATE_TIME_DIGITIZED)) ) {
		m.date = strdup(exif_entry_get_value(e, b, sizeof(b)));
		if( strlen(m.date) > 10 )
		{
			m.date[4] = '-';
			m.date[7] = '-';
			m.date[10] = 'T';
		}
		else {
			free(m.date);
			m.date = NULL;
		}
	}
	else {
		/* One last effort to get the date from XMP */
		image_get_jpeg_date_xmp(path, &m.date);
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * date: %s\n", m.date);

	e = exif_content_get_entry (ed->ifd[EXIF_IFD_0], EXIF_TAG_MAKE);
	if( e )
	{
		strncpy(make, exif_entry_get_value(e, b, sizeof(b)), sizeof(make));
		e = exif_content_get_entry (ed->ifd[EXIF_IFD_0], EXIF_TAG_MODEL);
		if( e )
		{
			strncpy(model, exif_entry_get_value(e, b, sizeof(b)), sizeof(model));
			if( !strcasestr(model, make) )
				snprintf(model, sizeof(model), "%s %s", make, exif_entry_get_value(e, b, sizeof(b)));
			m.creator = strdup(model);
		}
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * model: %s\n", model);

	if( ed->size )
	{
		/* We might need to verify that the thumbnail is 160x160 or smaller */
		if( ed->size > 12000 )
		{
			imsrc = image_new_from_jpeg(NULL, 0, (char *)ed->data, ed->size, 1);
			if( imsrc )
			{
 				if( (imsrc->width <= 160) && (imsrc->height <= 160) )
					thumb = 1;
				image_free(imsrc);
			}
		}
		else
			thumb = 1;
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * thumbnail: %d\n", thumb);

	exif_data_unref(ed);

no_exifdata:
	/* If SOF parsing fails, then fall through to reading the JPEG data with libjpeg to get the resolution */
	if( image_get_jpeg_resolution(path, &width, &height) != 0 || !width || !height )
	{
		infile = fopen(path, "r");
		cinfo.err = jpeg_std_error(&jerr);
		jerr.error_exit = libjpeg_error_handler;
		jpeg_create_decompress(&cinfo);
		if( setjmp(setjmp_buffer) )
			goto error;
		jpeg_stdio_src(&cinfo, infile);
		jpeg_read_header(&cinfo, TRUE);
		jpeg_start_decompress(&cinfo);
		width = cinfo.output_width;
		height = cinfo.output_height;
		error:
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * resolution: %dx%d\n", width, height);

	if( !width || !height )
	{
		free_metadata(&m, free_flags);
		return 0;
	}
	if( width <= 640 && height <= 480 )
		asprintf(&m.dlna_pn, "JPEG_SM;%s", dlna_no_conv);
	else if( width <= 1024 && height <= 768 )
		asprintf(&m.dlna_pn, "JPEG_MED;%s", dlna_no_conv);
	else if( (width <= 4096 && height <= 4096) || !(GETFLAG(DLNA_STRICT_MASK)) )
		asprintf(&m.dlna_pn, "JPEG_LRG;%s", dlna_no_conv);
	asprintf(&m.resolution, "%dx%d", width, height);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, TITLE, SIZE, TIMESTAMP, DATE, RESOLUTION, THUMBNAIL, CREATOR, DLNA_PN, MIME) "
	                   "VALUES"
	                   " (%Q, '%q', %lld, %ld, %Q, %Q, %d, %Q, %Q, %Q);",
	                   path, name, file.st_size, file.st_mtime, m.date, m.resolution, thumb, m.creator, m.dlna_pn, m.mime);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	free_metadata(&m, free_flags);

	return ret;
}

sqlite_int64
GetVideoMetadata(const char * path, char * name)
{
	struct stat file;
	int ret, i;
	struct tm *modtime;
	AVFormatContext *ctx;
	int audio_stream = -1, video_stream = -1;
	enum audio_profiles audio_profile = PROFILE_AUDIO_UNKNOWN;
	tsinfo_t *ts;
	ts_timestamp_t ts_timestamp = NONE;
	char fourcc[4];
	int off;
	int duration, hours, min, sec, ms;
	aac_object_type_t aac_type = AAC_INVALID;
	sqlite_int64 album_art = 0;
	char nfo[PATH_MAX], *ext;
	struct song_metadata video;
	metadata_t m;
	uint32_t free_flags = 0xFFFFFFFF;
	memset(&m, '\0', sizeof(m));
	memset(&video, '\0', sizeof(video));

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Parsing video %s...\n", name);
	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %jd\n", file.st_size);

	av_register_all();
	if( av_open_input_file(&ctx, path, NULL, 0, NULL) != 0 )
	{
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed!\n", path);
		return 0;
	}
	av_find_stream_info(ctx);
	//dump_format(ctx, 0, NULL, 0);
	for( i=0; i<ctx->nb_streams; i++)
	{
		if( audio_stream == -1 &&
		    ctx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO )
		{
			audio_stream = i;
			continue;
		}
		else if( video_stream == -1 &&
			 ctx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO )
		{
			video_stream = i;
			continue;
		}
	}
	/* This must not be a video file. */
	if( video_stream == -1 )
	{
		av_close_input_file(ctx);
		if( !is_audio(path) )
			DPRINTF(E_DEBUG, L_METADATA, "File %s does not contain a video stream.\n", basename(path));
		return 0;
	}

	strcpy(nfo, path);
	ext = strrchr(nfo, '.');
	if( ext )
	{
		strcpy(ext+1, "nfo");
		if( access(nfo, F_OK) == 0 )
		{
			parse_nfo(nfo, &m);
		}
	}

	if( !m.date )
	{
		m.date = malloc(20);
		modtime = localtime(&file.st_mtime);
		strftime(m.date, 20, "%FT%T", modtime);
	}

	if( audio_stream >= 0 )
	{
		switch( ctx->streams[audio_stream]->codec->codec_id )
		{
			case CODEC_ID_MP3:
				audio_profile = PROFILE_AUDIO_MP3;
				break;
			case CODEC_ID_AAC:
				if( !ctx->streams[audio_stream]->codec->extradata_size ||
				    !ctx->streams[audio_stream]->codec->extradata )
				{
					DPRINTF(E_DEBUG, L_METADATA, "No AAC type\n");
				}
				else
				{
					uint8_t data;
					memcpy(&data, ctx->streams[audio_stream]->codec->extradata, 1);
					aac_type = data >> 3;
				}
				switch( aac_type )
				{
					/* AAC Low Complexity variants */
					case AAC_LC:
					case AAC_LC_ER:
						if( ctx->streams[audio_stream]->codec->sample_rate < 8000 ||
						    ctx->streams[audio_stream]->codec->sample_rate > 48000 )
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unsupported AAC: sample rate is not 8000 < %d < 48000\n",
								ctx->streams[audio_stream]->codec->sample_rate);
							break;
						}
						/* AAC @ Level 1/2 */
						if( ctx->streams[audio_stream]->codec->channels <= 2 &&
						    ctx->streams[audio_stream]->codec->bit_rate <= 576000 )
							audio_profile = PROFILE_AUDIO_AAC;
						else if( ctx->streams[audio_stream]->codec->channels <= 6 &&
							 ctx->streams[audio_stream]->codec->bit_rate <= 1440000 )
							audio_profile = PROFILE_AUDIO_AAC_MULT5;
						else
							DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC: %d channels, %d bitrate\n",
								ctx->streams[audio_stream]->codec->channels,
								ctx->streams[audio_stream]->codec->bit_rate);
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC type [%d]\n", aac_type);
						break;
				}
				break;
			case CODEC_ID_AC3:
			case CODEC_ID_DTS:
				audio_profile = PROFILE_AUDIO_AC3;
				break;
			case CODEC_ID_WMAV1:
			case CODEC_ID_WMAV2:
				/* WMA Baseline: stereo, up to 48 KHz, up to 192,999 bps */
				if ( ctx->streams[audio_stream]->codec->bit_rate <= 193000 )
					audio_profile = PROFILE_AUDIO_WMA_BASE;
				/* WMA Full: stereo, up to 48 KHz, up to 385 Kbps */
				else if ( ctx->streams[audio_stream]->codec->bit_rate <= 385000 )
					audio_profile = PROFILE_AUDIO_WMA_FULL;
				break;
			#if LIBAVCODEC_VERSION_INT > ((51<<16)+(50<<8)+1)
			case CODEC_ID_WMAPRO:
				audio_profile = PROFILE_AUDIO_WMA_PRO;
				break;
			#endif
			case CODEC_ID_MP2:
				audio_profile = PROFILE_AUDIO_MP2;
				break;
			case CODEC_ID_AMR_NB:
				audio_profile = PROFILE_AUDIO_AMR;
				break;
			default:
				if( (ctx->streams[audio_stream]->codec->codec_id >= CODEC_ID_PCM_S16LE) &&
				    (ctx->streams[audio_stream]->codec->codec_id < CODEC_ID_ADPCM_IMA_QT) )
					audio_profile = PROFILE_AUDIO_PCM;
				else
					DPRINTF(E_DEBUG, L_METADATA, "Unhandled audio codec [0x%X]\n", ctx->streams[audio_stream]->codec->codec_id);
				break;
		}
		asprintf(&m.frequency, "%u", ctx->streams[audio_stream]->codec->sample_rate);
		#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
		asprintf(&m.bps, "%u", ctx->streams[audio_stream]->codec->bits_per_sample);
		#else
		asprintf(&m.bps, "%u", ctx->streams[audio_stream]->codec->bits_per_coded_sample);
		#endif
		asprintf(&m.channels, "%u", ctx->streams[audio_stream]->codec->channels);
	}
	if( video_stream >= 0 )
	{
		DPRINTF(E_DEBUG, L_METADATA, "Container: '%s' [%s]\n", ctx->iformat->name, basename(path));
		asprintf(&m.resolution, "%dx%d", ctx->streams[video_stream]->codec->width, ctx->streams[video_stream]->codec->height);
		if( ctx->bit_rate > 8 )
			asprintf(&m.bitrate, "%u", ctx->bit_rate / 8);
		if( ctx->duration > 0 ) {
			duration = (int)(ctx->duration / AV_TIME_BASE);
			hours = (int)(duration / 3600);
			min = (int)(duration / 60 % 60);
			sec = (int)(duration % 60);
			ms = (int)(ctx->duration / (AV_TIME_BASE/1000) % 1000);
			asprintf(&m.duration, "%d:%02d:%02d.%03d", hours, min, sec, ms);
		}

		/* NOTE: The DLNA spec only provides for ASF (WMV), TS, PS, and MP4 containers. Skip DLNA parsing for everything else. */
		if( strcmp(ctx->iformat->name, "avi") == 0 )
		{
			asprintf(&m.mime, "video/x-msvideo");
			if( ctx->streams[video_stream]->codec->codec_id == CODEC_ID_MPEG4 )
			{
        			fourcc[0] = ctx->streams[video_stream]->codec->codec_tag     & 0xff;
			        fourcc[1] = ctx->streams[video_stream]->codec->codec_tag>>8  & 0xff;
		        	fourcc[2] = ctx->streams[video_stream]->codec->codec_tag>>16 & 0xff;
			        fourcc[3] = ctx->streams[video_stream]->codec->codec_tag>>24 & 0xff;
				if( memcmp(fourcc, "XVID", 4) == 0 ||
				    memcmp(fourcc, "DX50", 4) == 0 ||
				    memcmp(fourcc, "DIVX", 4) == 0 )
					asprintf(&m.creator, "DiVX");
			}
		}
		else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 &&
		         ends_with(path, ".mov") )
			asprintf(&m.mime, "video/quicktime");
		else if( strncmp(ctx->iformat->name, "matroska", 8) == 0 )
			asprintf(&m.mime, "video/x-matroska");
		else if( strcmp(ctx->iformat->name, "flv") == 0 )
			asprintf(&m.mime, "video/x-flv");
		if( m.mime )
			goto video_no_dlna;

		switch( ctx->streams[video_stream]->codec->codec_id )
		{
			case CODEC_ID_MPEG1VIDEO:
				if( strcmp(ctx->iformat->name, "mpeg") == 0 )
				{
					if( (ctx->streams[video_stream]->codec->width  == 352) &&
					    (ctx->streams[video_stream]->codec->height <= 288) )
					{
						asprintf(&m.dlna_pn, "MPEG1;%s", dlna_no_conv);
					}
					asprintf(&m.mime, "video/mpeg");
				}
				break;
			case CODEC_ID_MPEG2VIDEO:
				m.dlna_pn = malloc(64);
				off = sprintf(m.dlna_pn, "MPEG_");
				if( strcmp(ctx->iformat->name, "mpegts") == 0 )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MPEG2 TS\n", video_stream, basename(path), m.resolution);
					off += sprintf(m.dlna_pn+off, "TS_");
					if( (ctx->streams[video_stream]->codec->width  >= 1280) &&
					    (ctx->streams[video_stream]->codec->height >= 720) )
					{
						off += sprintf(m.dlna_pn+off, "HD_NA");
					}
					else
					{
						off += sprintf(m.dlna_pn+off, "SD_");
						if( (ctx->streams[video_stream]->codec->height == 576) ||
						    (ctx->streams[video_stream]->codec->height == 288) )
							off += sprintf(m.dlna_pn+off, "EU");
						else
							off += sprintf(m.dlna_pn+off, "NA");
					}
					ts = ctx->priv_data;
					if( ts->packet_size == 192 )
					{
						if( dlna_timestamp_is_present(path) )
							ts_timestamp = VALID;
						else
							ts_timestamp = EMPTY;
					}
					else if( ts->packet_size != 188 )
					{
						DPRINTF(E_DEBUG, L_METADATA, "Invalid TS packet size [%s]\n", basename(path));
						free(m.dlna_pn);
						m.dlna_pn = NULL;
					}
					switch( ts_timestamp )
					{
						case NONE:
							asprintf(&m.mime, "video/mpeg");
							off += sprintf(m.dlna_pn+off, "_ISO");
							break;
						case VALID:
							off += sprintf(m.dlna_pn+off, "_T");
						case EMPTY:
							asprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
						default:
							break;
					}
				}
				else if( strcmp(ctx->iformat->name, "mpeg") == 0 )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MPEG2 PS\n", video_stream, basename(path), m.resolution);
					off += sprintf(m.dlna_pn+off, "PS_");
					if( (ctx->streams[video_stream]->codec->height == 576) ||
					    (ctx->streams[video_stream]->codec->height == 288) )
						off += sprintf(m.dlna_pn+off, "PAL");
					else
						off += sprintf(m.dlna_pn+off, "NTSC");
					asprintf(&m.mime, "video/mpeg");
				}
				else
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s [UNKNOWN CONTAINER] is %s MPEG2\n", video_stream, basename(path), m.resolution);
					free(m.dlna_pn);
					m.dlna_pn = NULL;
				}
				if( m.dlna_pn )
					sprintf(m.dlna_pn+off, ";%s", dlna_no_conv);
				break;
			case CODEC_ID_H264:
				m.dlna_pn = malloc(128);
				off = sprintf(m.dlna_pn, "AVC_");

				if( strcmp(ctx->iformat->name, "mpegts") == 0 )
				{
					off += sprintf(m.dlna_pn+off, "TS_");
					switch( ctx->streams[video_stream]->codec->profile )
					{
						case FF_PROFILE_H264_BASELINE:
							off += sprintf(m.dlna_pn+off, "BL_");
							if( ctx->streams[video_stream]->codec->width  <= 352 &&
							    ctx->streams[video_stream]->codec->height <= 288 &&
							    ctx->streams[video_stream]->codec->bit_rate <= 384000 )
							{
								off += sprintf(m.dlna_pn+off, "CIF15_");
							}
							else if( ctx->streams[video_stream]->codec->width  <= 352 &&
							         ctx->streams[video_stream]->codec->height <= 288 &&
							         ctx->streams[video_stream]->codec->bit_rate <= 3000000 )
							{
								off += sprintf(m.dlna_pn+off, "CIF30_");
							}
							else
							{
								DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 video profile! [%s, %dx%d, %dbps : %s]\n",
									m.dlna_pn,
									ctx->streams[video_stream]->codec->width,
									ctx->streams[video_stream]->codec->height,
									ctx->streams[video_stream]->codec->bit_rate,
									basename(path));
								free(m.dlna_pn);
								m.dlna_pn = NULL;
							}
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "Unknown AVC profile %d; assuming MP. [%s]\n",
								ctx->streams[video_stream]->codec->profile, basename(path));
						case FF_PROFILE_H264_MAIN:
							off += sprintf(m.dlna_pn+off, "MP_");
							if( ctx->streams[video_stream]->codec->width  <= 720 &&
							    ctx->streams[video_stream]->codec->height <= 576 &&
							    ctx->streams[video_stream]->codec->bit_rate <= 10000000 )
							{
								off += sprintf(m.dlna_pn+off, "SD_");
							}
							else if( ctx->streams[video_stream]->codec->width  <= 1920 &&
							         ctx->streams[video_stream]->codec->height <= 1152 &&
							         ctx->streams[video_stream]->codec->bit_rate <= 20000000 )
							{
								off += sprintf(m.dlna_pn+off, "HD_");
							}
							else
							{
								DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 video profile! [%s, %dx%d, %dbps : %s]\n",
									m.dlna_pn,
									ctx->streams[video_stream]->codec->width,
									ctx->streams[video_stream]->codec->height,
									ctx->streams[video_stream]->codec->bit_rate,
									basename(path));
								free(m.dlna_pn);
								m.dlna_pn = NULL;
							}
							break;
					}
					if( !m.dlna_pn )
						break;
					switch( audio_profile )
					{
						case PROFILE_AUDIO_MP3:
							off += sprintf(m.dlna_pn+off, "MPEG1_L3");
							break;
						case PROFILE_AUDIO_AC3:
							off += sprintf(m.dlna_pn+off, "AC3");
							break;
						case PROFILE_AUDIO_AAC_MULT5:
							off += sprintf(m.dlna_pn+off, "AAC_MULT5");
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for %s file [%s]\n",
								m.dlna_pn, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
							break;
					}
					if( !m.dlna_pn )
						break;
					ts = ctx->priv_data;
					if( ts->packet_size == 192 )
					{
						if( dlna_timestamp_is_present(path) )
							ts_timestamp = VALID;
						else
							ts_timestamp = EMPTY;
					}
					switch( ts_timestamp )
					{
						case NONE:
							off += sprintf(m.dlna_pn+off, "_ISO");
							break;
						case VALID:
							off += sprintf(m.dlna_pn+off, "_T");
						case EMPTY:
							asprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
						default:
							break;
					}
				}
				else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
				{
					off += sprintf(m.dlna_pn+off, "MP4_");

					switch( ctx->streams[video_stream]->codec->profile ) {
					case FF_PROFILE_H264_BASELINE:
						if( ctx->streams[video_stream]->codec->width  <= 352 &&
						    ctx->streams[video_stream]->codec->height <= 288 )
						{
							if( ctx->bit_rate < 600000 )
							{
								off += sprintf(m.dlna_pn+off, "BL_CIF15_");
							}
							else if( ctx->bit_rate < 5000000 )
							{
								off += sprintf(m.dlna_pn+off, "BL_CIF30_");
							}
							else
							{
								DPRINTF(E_DEBUG, L_METADATA, "Unhandled MP4_BL bit rate on %s\n", basename(path));
								free(m.dlna_pn);
								m.dlna_pn = NULL;
								break;
							}
							switch( audio_profile )
							{
								case PROFILE_AUDIO_AMR:
									off += sprintf(m.dlna_pn+off, "AMR");
									break;
								case PROFILE_AUDIO_AAC:
									off += sprintf(m.dlna_pn+off, "AAC_");
									if( ctx->bit_rate < 540000 )
									{
										off += sprintf(m.dlna_pn+off, "540");
										break;
									}
									else if( ctx->bit_rate < 940000 )
									{
										off += sprintf(m.dlna_pn+off, "940");
										break;
									}
								default:
									DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for %s file %s\n",
										m.dlna_pn, basename(path));
									free(m.dlna_pn);
									m.dlna_pn = NULL;
									break;
							}
						}
						else if( ctx->streams[video_stream]->codec->width  <= 720 &&
						         ctx->streams[video_stream]->codec->height <= 576 )
						{
							if( ctx->streams[video_stream]->codec->level == 30 &&
							    audio_profile == PROFILE_AUDIO_AAC &&
							    ctx->bit_rate <= 5000000 )
							{
								off += sprintf(m.dlna_pn+off, "BL_L3L_SD_AAC");
							}
							else if( ctx->streams[video_stream]->codec->level <= 31 &&
							         audio_profile == PROFILE_AUDIO_AAC &&
							         ctx->bit_rate <= 15000000 )
							{
								off += sprintf(m.dlna_pn+off, "BL_L31_HD_AAC");
							}
						}
						else if( ctx->streams[video_stream]->codec->width  <= 1280 &&
						         ctx->streams[video_stream]->codec->height <= 720 )
						{
							if( ctx->streams[video_stream]->codec->level <= 31 &&
							    audio_profile == PROFILE_AUDIO_AAC &&
							    ctx->bit_rate <= 15000000 )
							{
								off += sprintf(m.dlna_pn+off, "BL_L31_HD_AAC");
							}
							else if( ctx->streams[video_stream]->codec->level <= 32 &&
							         audio_profile == PROFILE_AUDIO_AAC &&
							         ctx->bit_rate <= 21000000 )
							{
								off += sprintf(m.dlna_pn+off, "BL_L32_HD_AAC");
							}
						}
						if( strlen(m.dlna_pn) <= 8 )
						{
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for %s file %s\n",
								m.dlna_pn, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
						}
						break;
					case FF_PROFILE_H264_MAIN:
						off += sprintf(m.dlna_pn+off, "MP_");
						/* AVC MP4 SD profiles - 10 Mbps max */
						if( ctx->streams[video_stream]->codec->width  <= 720 &&
						    ctx->streams[video_stream]->codec->height <= 576 &&
						    ctx->streams[video_stream]->codec->bit_rate <= 10000000 )
						{
							sprintf(m.dlna_pn+off, "SD_");
							switch( audio_profile )
							{
								case PROFILE_AUDIO_AC3:
									off += sprintf(m.dlna_pn+off, "AC3");
									break;
								case PROFILE_AUDIO_AAC_MULT5:
									off += sprintf(m.dlna_pn+off, "AAC_MULT5");
									break;
								default:
									DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for MP4/AVC/SD file %s\n",
										basename(path));
									free(m.dlna_pn);
									m.dlna_pn = NULL;
									break;
							}
						}
						else if( ctx->streams[video_stream]->codec->width  <= 1280 &&
						         ctx->streams[video_stream]->codec->height <= 720 &&
						         ctx->streams[video_stream]->codec->bit_rate <= 15000000 &&
						         audio_profile == PROFILE_AUDIO_AAC )
						{
							off += sprintf(m.dlna_pn+off, "HD_720p_AAC");
						}
						else if( ctx->streams[video_stream]->codec->width  <= 1920 &&
						         ctx->streams[video_stream]->codec->height <= 1080 &&
						         ctx->streams[video_stream]->codec->bit_rate <= 21000000 &&
						         audio_profile == PROFILE_AUDIO_AAC )
						{
							off += sprintf(m.dlna_pn+off, "HD_1080i_AAC");
						}
						if( strlen(m.dlna_pn) <= 11 )
						{
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for %s file %s\n",
								m.dlna_pn, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
						}
						break;
					case FF_PROFILE_H264_HIGH:
						if( ctx->streams[video_stream]->codec->width  <= 1920 &&
						    ctx->streams[video_stream]->codec->height <= 1080 &&
						    ctx->streams[video_stream]->codec->bit_rate <= 25000000 &&
						    audio_profile == PROFILE_AUDIO_AAC )
						{
							off += sprintf(m.dlna_pn+off, "HP_HD_AAC");
						}
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "AVC profile [%d] not recognized for file %s\n",
							ctx->streams[video_stream]->codec->profile, basename(path));
						free(m.dlna_pn);
						m.dlna_pn = NULL;
						break;
					}
				}
				if( m.dlna_pn )
					sprintf(m.dlna_pn+off, ";%s", dlna_no_conv);
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is h.264\n", video_stream, basename(path));
				break;
			case CODEC_ID_MPEG4:
        			fourcc[0] = ctx->streams[video_stream]->codec->codec_tag     & 0xff;
			        fourcc[1] = ctx->streams[video_stream]->codec->codec_tag>>8  & 0xff;
			        fourcc[2] = ctx->streams[video_stream]->codec->codec_tag>>16 & 0xff;
			        fourcc[3] = ctx->streams[video_stream]->codec->codec_tag>>24 & 0xff;
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MPEG4 [%c%c%c%c/0x%X]\n",
					video_stream, basename(path),
					isprint(fourcc[0]) ? fourcc[0] : '_',
					isprint(fourcc[1]) ? fourcc[1] : '_',
					isprint(fourcc[2]) ? fourcc[2] : '_',
					isprint(fourcc[3]) ? fourcc[3] : '_',
					ctx->streams[video_stream]->codec->codec_tag);

				if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
				{
					m.dlna_pn = malloc(128);
					off = sprintf(m.dlna_pn, "MPEG4_P2_");

					if( ends_with(path, ".3gp") )
					{
						asprintf(&m.mime, "video/3gpp");
						switch( audio_profile )
						{
							case PROFILE_AUDIO_AAC:
								off += sprintf(m.dlna_pn+off, "3GPP_SP_L0B_AAC");
								break;
							case PROFILE_AUDIO_AMR:
								off += sprintf(m.dlna_pn+off, "3GPP_SP_L0B_AMR");
								break;
							default:
								DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for MPEG4-P2 3GP/0x%X file %s\n",
								        ctx->streams[audio_stream]->codec->codec_id, basename(path));
								free(m.dlna_pn);
								m.dlna_pn = NULL;
								break;
						}
					}
					else
					{
						if( ctx->bit_rate <= 1000000 &&
						    audio_profile == PROFILE_AUDIO_AAC )
						{
							off += sprintf(m.dlna_pn+off, "MP4_ASP_AAC");
						}
						else if( ctx->bit_rate <= 4000000 &&
						         ctx->streams[video_stream]->codec->width  <= 640 &&
						         ctx->streams[video_stream]->codec->height <= 480 &&
						         audio_profile == PROFILE_AUDIO_AAC )
						{
							off += sprintf(m.dlna_pn+off, "MP4_SP_VGA_AAC");
						}
						else
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 video profile! [%dx%d, %dbps]\n",
								ctx->streams[video_stream]->codec->width,
								ctx->streams[video_stream]->codec->height,
								ctx->bit_rate);
							free(m.dlna_pn);
							m.dlna_pn = NULL;
						}
					}
					if( m.dlna_pn )
						sprintf(m.dlna_pn+off, ";%s", dlna_no_conv);
				}
				break;
			case CODEC_ID_WMV3:
				/* I'm not 100% sure this is correct, but it works on everything I could get my hands on */
				if( ctx->streams[video_stream]->codec->extradata_size > 0 )
				{
					if( !((ctx->streams[video_stream]->codec->extradata[0] >> 3) & 1) )
						ctx->streams[video_stream]->codec->level = 0;
					if( !((ctx->streams[video_stream]->codec->extradata[0] >> 6) & 1) )
						ctx->streams[video_stream]->codec->profile = 0;
				}
			case CODEC_ID_VC1:
				if( strcmp(ctx->iformat->name, "asf") != 0 )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Skipping DLNA parsing for non-ASF VC1 file %s\n", path);
					break;
				}
				m.dlna_pn = malloc(64);
				off = sprintf(m.dlna_pn, "WMV");
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is VC1\n", video_stream, basename(path));
				asprintf(&m.mime, "video/x-ms-wmv");
				if( (ctx->streams[video_stream]->codec->width  <= 176) &&
				    (ctx->streams[video_stream]->codec->height <= 144) &&
				    (ctx->streams[video_stream]->codec->level == 0) )
				{
					off += sprintf(m.dlna_pn+off, "SPLL_");
					switch( audio_profile )
					{
						case PROFILE_AUDIO_MP3:
							off += sprintf(m.dlna_pn+off, "MP3");
							break;
						case PROFILE_AUDIO_WMA_BASE:
							off += sprintf(m.dlna_pn+off, "BASE");
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVSPLL/0x%X file %s\n", audio_profile, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
							break;
					}
				}
				else if( (ctx->streams[video_stream]->codec->width  <= 352) &&
				         (ctx->streams[video_stream]->codec->height <= 288) &&
				         (ctx->streams[video_stream]->codec->profile == 0) &&
				         (ctx->bit_rate/8 <= 384000) )
				{
					off += sprintf(m.dlna_pn+off, "SPML_");
					switch( audio_profile )
					{
						case PROFILE_AUDIO_MP3:
							off += sprintf(m.dlna_pn+off, "MP3");
							break;
						case PROFILE_AUDIO_WMA_BASE:
							off += sprintf(m.dlna_pn+off, "BASE");
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVSPML/0x%X file %s\n", audio_profile, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
							break;
					}
				}
				else if( (ctx->streams[video_stream]->codec->width  <= 720) &&
				         (ctx->streams[video_stream]->codec->height <= 576) &&
				         (ctx->bit_rate/8 <= 10000000) )
				{
					off += sprintf(m.dlna_pn+off, "MED_");
					switch( audio_profile )
					{
						case PROFILE_AUDIO_WMA_PRO:
							off += sprintf(m.dlna_pn+off, "PRO");
							break;
						case PROFILE_AUDIO_WMA_FULL:
							off += sprintf(m.dlna_pn+off, "FULL");
							break;
						case PROFILE_AUDIO_WMA_BASE:
							off += sprintf(m.dlna_pn+off, "BASE");
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVMED/0x%X file %s\n", audio_profile, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
							break;
					}
				}
				else if( (ctx->streams[video_stream]->codec->width  <= 1920) &&
				         (ctx->streams[video_stream]->codec->height <= 1080) &&
				         (ctx->bit_rate/8 <= 20000000) )
				{
					off += sprintf(m.dlna_pn+off, "HIGH_");
					switch( audio_profile )
					{
						case PROFILE_AUDIO_WMA_PRO:
							off += sprintf(m.dlna_pn+off, "PRO");
							break;
						case PROFILE_AUDIO_WMA_FULL:
							off += sprintf(m.dlna_pn+off, "FULL");
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVHIGH/0x%X file %s\n", audio_profile, basename(path));
							free(m.dlna_pn);
							m.dlna_pn = NULL;
							break;
					}
				}
				if( m.dlna_pn )
					sprintf(m.dlna_pn+off, ";%s", dlna_no_conv);
				break;
			case CODEC_ID_MSMPEG4V3:
				asprintf(&m.mime, "video/x-msvideo");
			default:
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s [type %d]\n", video_stream, basename(path), m.resolution, ctx->streams[video_stream]->codec->codec_id);
				break;
		}
	}
	if( !m.mime )
	{
		if( strcmp(ctx->iformat->name, "avi") == 0 )
			asprintf(&m.mime, "video/x-msvideo");
		else if( strncmp(ctx->iformat->name, "mpeg", 4) == 0 )
			asprintf(&m.mime, "video/mpeg");
		else if( strcmp(ctx->iformat->name, "asf") == 0 )
			asprintf(&m.mime, "video/x-ms-wmv");
		else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
			if( ends_with(path, ".mov") )
				asprintf(&m.mime, "video/quicktime");
			else
				asprintf(&m.mime, "video/mp4");
		else if( strncmp(ctx->iformat->name, "matroska", 8) == 0 )
			asprintf(&m.mime, "video/x-matroska");
		else if( strcmp(ctx->iformat->name, "flv") == 0 )
			asprintf(&m.mime, "video/x-flv");
		else
			DPRINTF(E_WARN, L_METADATA, "%s: Unhandled format: %s\n", path, ctx->iformat->name);
	}

	if( strcmp(ctx->iformat->name, "asf") == 0 )
	{
		if( readtags((char *)path, &video, &file, "en_US", "asf") == 0 )
		{
			if( video.title && *video.title )
			{
				m.title = strdup(trim(video.title));
			}
			if( video.genre && *video.genre )
			{
				m.genre = strdup(trim(video.genre));
			}
			if( video.contributor[ROLE_TRACKARTIST] && *video.contributor[ROLE_TRACKARTIST] )
			{
				m.artist = strdup(trim(video.contributor[ROLE_TRACKARTIST]));
			}
			if( video.contributor[ROLE_ALBUMARTIST] && *video.contributor[ROLE_ALBUMARTIST] )
			{
				m.creator = strdup(trim(video.contributor[ROLE_ALBUMARTIST]));
			}
			else
			{
				m.creator = m.artist;
				free_flags &= ~FLAG_CREATOR;
			}
		}
	}
video_no_dlna:
	av_close_input_file(ctx);

#ifdef TIVO_SUPPORT
	if( ends_with(path, ".TiVo") && is_tivo_file(path) )
	{
		free(m.mime);
		asprintf(&m.mime, "video/x-tivo-mpeg");
	}
#endif
	if( !m.title )
		m.title = strdup(name);

	album_art = find_album_art(path, video.image, video.image_size);
	freetags(&video);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, SIZE, TIMESTAMP, DURATION, DATE, CHANNELS, BITRATE, SAMPLERATE, RESOLUTION,"
	                   "  TITLE, CREATOR, ARTIST, GENRE, COMMENT, DLNA_PN, MIME, ALBUM_ART) "
	                   "VALUES"
	                   " (%Q, %lld, %ld, %Q, %Q, %Q, %Q, %Q, %Q, '%q', %Q, %Q, %Q, %Q, %Q, '%q', %lld);",
	                   path, file.st_size, file.st_mtime, m.duration,
	                   m.date, m.channels, m.bitrate, m.frequency, m.resolution,
			   m.title, m.creator, m.artist, m.genre, m.comment, m.dlna_pn,
                           m.mime, album_art);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
		check_for_captions(path, ret);
	}
	free_metadata(&m, free_flags);

	return ret;
}
