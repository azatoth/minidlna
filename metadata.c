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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libexif/exif-loader.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <avutil.h>
#include <avcodec.h>
#include <avformat.h>
#include "tagutils/tagutils.h"

#include "upnpglobalvars.h"
#include "metadata.h"
#include "albumart.h"
#include "utils.h"
#include "sql.h"
#include "log.h"

#define FLAG_TITLE	0x00000001
#define FLAG_ARTIST	0x00000002
#define FLAG_ALBUM	0x00000004
#define FLAG_GENRE	0x00000008
#define FLAG_COMMENT	0x00000010

/* Audio profile flags */
#define MP3		0x00000001
#define AC3		0x00000002
#define WMA_BASE	0x00000004
#define WMA_FULL	0x00000008
#define WMA_PRO		0x00000010
#define MP2		0x00000020
#define PCM		0x00000040
#define AAC		0x00000100
#define AAC_MULT5	0x00000200

/* This function shamelessly copied from libdlna */
#define MPEG_TS_SYNC_CODE 0x47
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
				if (buffer[i] == 0x00 && buffer [i+1] == 0x00 &&
				    buffer [i+2] == 0x00 && buffer [i+3] == 0x00)
				{
					break;
				}
				else
				{
					return 1;
				}
			}
		}
	}
	return 0;
}

/* This function taken from libavutil (ffmpeg), because it's not included with all versions of libavutil. */
int
get_fourcc(const char *s)
{
	return (s[0]) + (s[1]<<8) + (s[2]<<16) + (s[3]<<24);
}

sqlite_int64
GetFolderMetadata(const char * name, const char * path, const char * artist, const char * genre, const char * album_art, const char * art_dlna_pn)
{
	char * sql;
	int ret;

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (TITLE, PATH, CREATOR, ARTIST, GENRE, ALBUM_ART, ART_DLNA_PN) "
				"VALUES"
				" ('%q', %Q, %Q, %Q, %Q, %lld, %Q);",
				name, path, artist, artist, genre,
				album_art ? strtoll(album_art, NULL, 10) : 0,
				art_dlna_pn);

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
	char duration[16], mime[16], type[4];
	struct stat file;
	sqlite_int64 ret;
	char *sql;
	char *title, *artist = NULL, *album = NULL, *genre = NULL, *comment = NULL, *date = NULL;
	int free_flags = 0;
	sqlite_int64 album_art = 0;
	char art_dlna_pn[9];
	struct song_metadata song;
	char *dlna_pn = NULL;

	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);

	if( ends_with(path, ".mp3") )
	{
		strcpy(type, "mp3");
		strcpy(mime, "audio/mpeg");
	}
	else if( ends_with(path, ".m4a") || ends_with(path, ".mp4") ||
	         ends_with(path, ".aac") || ends_with(path, ".m4p") )
	{
		strcpy(type, "aac");
		strcpy(mime, "audio/mp4");
	}
	else if( ends_with(path, ".wma") || ends_with(path, ".asf") )
	{
		strcpy(type, "asf");
		strcpy(mime, "audio/x-ms-wma");
	}
	else if( ends_with(path, ".flac") || ends_with(path, ".fla") || ends_with(path, ".flc") )
	{
		strcpy(type, "flc");
		strcpy(mime, "audio/x-flac");
	}
	else
	{
		DPRINTF(E_WARN, L_GENERAL, "Unhandled file extension on %s\n", path);
		return 0;
	}

	if( readtags((char *)path, &song, &file, NULL, type) != 0 )
	{
		DPRINTF(E_WARN, L_GENERAL, "Cannot extract tags from %s\n", path);
		return 0;
	}

	if( song.dlna_pn )
		asprintf(&dlna_pn, "%s;DLNA.ORG_OP=01", song.dlna_pn);
	if( song.year )
		asprintf(&date, "%04d-01-01", song.year);
	sprintf(duration, "%d:%02d:%02d.%03d",
	                  (song.song_length/3600000),
	                  (song.song_length/60000),
	                  (song.song_length/1000%60),
	                  (song.song_length%1000));
	title = song.title;
	if( title )
	{
		title = trim(title);
		if( index(title, '&') )
		{
			free_flags |= FLAG_TITLE;
			title = modifyString(strdup(title), "&", "&amp;amp;", 0);
		}
	}
	else
	{
		title = name;
	}
	if( song.contributor[ROLE_ARTIST] )
	{
		artist = song.contributor[ROLE_ARTIST];
		artist = trim(artist);
		if( index(artist, '&') )
		{
			free_flags |= FLAG_ARTIST;
			artist = modifyString(strdup(artist), "&", "&amp;amp;", 0);
		}
	}
	if( song.album )
	{
		album = song.album;
		album = trim(album);
		if( index(album, '&') )
		{
			free_flags |= FLAG_ALBUM;
			album = modifyString(strdup(album), "&", "&amp;amp;", 0);
		}
	}
	if( song.genre )
	{
		genre = song.genre;
		genre = trim(genre);
		if( index(genre, '&') )
		{
			free_flags |= FLAG_GENRE;
			genre = modifyString(strdup(genre), "&", "&amp;amp;", 0);
		}
	}
	if( song.comment )
	{
		comment = song.comment;
		comment = trim(comment);
		if( index(comment, '&') )
		{
			free_flags |= FLAG_COMMENT;
			comment = modifyString(strdup(comment), "&", "&amp;amp;", 0);
		}
	}

	album_art = find_album_art(path, art_dlna_pn, song.image, song.image_size);

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (PATH, SIZE, DURATION, CHANNELS, BITRATE, SAMPLERATE, DATE,"
				"  TITLE, CREATOR, ARTIST, ALBUM, GENRE, COMMENT, TRACK, DLNA_PN, MIME, ALBUM_ART, ART_DLNA_PN) "
				"VALUES"
				" (%Q, %d, '%s', %d, %d, %d, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %d, %Q, '%s', %lld, %Q);",
				path, song.file_size, duration, song.channels, song.bitrate, song.samplerate, date,
				title,
				artist, artist,
				album,
				genre,
				comment,
				song.track,
				dlna_pn, mime,
				album_art, album_art?art_dlna_pn:NULL );
        freetags(&song);
	if( dlna_pn )
		free(dlna_pn);
	if( date )
		free(date);
	if( free_flags & FLAG_TITLE )
		free(title);
	if( free_flags & FLAG_ARTIST )
		free(artist);
	if( free_flags & FLAG_ALBUM )
		free(album);
	if( free_flags & FLAG_GENRE )
		free(genre);
	if( free_flags & FLAG_COMMENT )
		free(comment);

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "SQL: %s\n", sql);
	if( sql_exec(db, sql) != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	sqlite3_free(sql);

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
	ExifTag tag;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *infile;
	int width=0, height=0, thumb=0;
	off_t size;
	char date[64], make[32], model[64];
	char b[1024];
	struct stat file;
	sqlite_int64 ret;
	char *sql;
	metadata_t m;
	memset(&m, '\0', sizeof(metadata_t));

	date[0] = '\0';
	model[0] = '\0';

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Parsing %s...\n", path);
	if ( stat(path, &file) == 0 )
		size = file.st_size;
	else
		return 0;
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %d\n", size);

	/* MIME hard-coded to JPEG for now, until we add PNG support */
	asprintf(&m.mime, "image/jpeg");

	l = exif_loader_new();
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
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * resolution: %dx%d\n", width, height);

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
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * date: %s\n", date);

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
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * model: %s\n", model);

	if( ed->size )
		thumb = 1;
	else
		thumb = 0;
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * thumbnail: %d\n", thumb);

	exif_data_unref(ed);

	/* If EXIF parsing fails, then fall through to reading the JPEG data with libjpeg to get the resolution */
	if( !width || !height )
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
				" (PATH, TITLE, SIZE, DATE, RESOLUTION, THUMBNAIL, CREATOR, DLNA_PN, MIME) "
				"VALUES"
				" (%Q, '%q', %llu, '%s', %Q, %d, '%q', %Q, %Q);",
				path, name, size, date, m.resolution, thumb, model, m.dlna_pn, m.mime);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "SQL: %s\n", sql);
	if( sql_exec(db, sql) != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
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
	off_t size = 0;
	struct stat file;
	char *sql;
	int ret, i;
	struct tm *modtime;
	char date[20];
	AVFormatContext *ctx;
	int audio_stream = -1, video_stream = -1;
	int audio_profile = 0;
	ts_timestamp_t ts_timestamp = NONE;
	int duration, hours, min, sec, ms;
	aac_object_type_t aac_type = AAC_INVALID;
	metadata_t m;
	memset(&m, '\0', sizeof(m));
	date[0] = '\0';

	DPRINTF(E_DEBUG, L_METADATA, "Parsing %s...\n", path);
	if ( stat(path, &file) == 0 )
	{
		modtime = localtime(&file.st_mtime);
		strftime(date, sizeof(date), "%FT%T", modtime);
		size = file.st_size;
	}
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %d\n", size);

	av_register_all();
	if( av_open_input_file(&ctx, path, NULL, 0, NULL) == 0 )
	{
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
		if( audio_stream >= 0 )
		{
			switch( ctx->streams[audio_stream]->codec->codec_id )
			{
				case CODEC_ID_MP3:
					audio_profile = MP3;
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
								audio_profile = AAC;
							else if( ctx->streams[audio_stream]->codec->channels <= 6 &&
								 ctx->streams[audio_stream]->codec->bit_rate <= 1440000 )
								audio_profile = AAC_MULT5;
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
					audio_profile = AC3;
					break;
				case CODEC_ID_WMAV1:
				case CODEC_ID_WMAV2:
					/* WMA Baseline: stereo, up to 48 KHz, up to 192,999 bps */
					if ( ctx->streams[audio_stream]->codec->bit_rate <= 193000 )
						audio_profile = WMA_BASE;
					/* WMA Full: stereo, up to 48 KHz, up to 385 Kbps */
					else if ( ctx->streams[audio_stream]->codec->bit_rate <= 385000 )
						audio_profile = WMA_FULL;
					break;
				#ifdef CODEC_ID_WMAPRO
				case CODEC_ID_WMAPRO:
					audio_profile = WMA_PRO;
					break;
				#endif
				case CODEC_ID_MP2:
					audio_profile = MP2;
					break;
				default:
					if( (ctx->streams[audio_stream]->codec->codec_id >= CODEC_ID_PCM_S16LE) &&
					    (ctx->streams[audio_stream]->codec->codec_id < CODEC_ID_ADPCM_IMA_QT) )
						audio_profile = PCM;
					else
						DPRINTF(E_DEBUG, L_METADATA, "Unhandled audio codec [%X]\n", ctx->streams[audio_stream]->codec->codec_id);
					break;
			}
			asprintf(&m.frequency, "%u", ctx->streams[audio_stream]->codec->sample_rate);
			#if LIBAVCODEC_VERSION_MAJOR >= 52
			asprintf(&m.bps, "%u", ctx->streams[audio_stream]->codec->bits_per_coded_sample);
			#else
			asprintf(&m.bps, "%u", ctx->streams[audio_stream]->codec->bits_per_sample);
			#endif
			asprintf(&m.channels, "%u", ctx->streams[audio_stream]->codec->channels);
		}
		if( video_stream >= 0 )
		{
			DPRINTF(E_DEBUG, L_METADATA, "Container: '%s' [%s]\n", ctx->iformat->name, path);
			asprintf(&m.resolution, "%dx%d", ctx->streams[video_stream]->codec->width, ctx->streams[video_stream]->codec->height);
			asprintf(&m.bitrate, "%u", ctx->bit_rate / 8);
			if( ctx->duration > 0 ) {
				duration = (int)(ctx->duration / AV_TIME_BASE);
				hours = (int)(duration / 3600);
				min = (int)(duration / 60 % 60);
				sec = (int)(duration % 60);
				ms = (int)(ctx->duration / (AV_TIME_BASE/1000) % 1000);
				asprintf(&m.duration, "%d:%02d:%02d.%03d", hours, min, sec, ms);
			}
			/* NOTE: The DLNA spec only provides for ASF (WMV), TS, PS, and MP4 containers -- not AVI. */
			switch( ctx->streams[video_stream]->codec->codec_id )
			{
				case CODEC_ID_MPEG1VIDEO:
					if( strcmp(ctx->iformat->name, "mpeg") == 0 )
					{
						asprintf(&m.dlna_pn, "MPEG1;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
						asprintf(&m.mime, "video/mpeg");
					}
					break;
				case CODEC_ID_MPEG2VIDEO:
					if( strcmp(ctx->iformat->name, "mpegts") == 0 )
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MPEG2 TS\n", video_stream, path, m.resolution);
						char res;
						tsinfo_t * ts = ctx->priv_data;
						if( ts->packet_size == 192 )
						{
							asprintf(&m.dlna_pn, "MPEG_TS_HD_NA;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
							asprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
						}
						else if( ts->packet_size == 188 )
						{
							if( (ctx->streams[video_stream]->codec->width  >= 1280) &&
							    (ctx->streams[video_stream]->codec->height >= 720) )
								res = 'H';
							else
								res = 'S';
							asprintf(&m.dlna_pn, "MPEG_TS_%cD_NA_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0", res);
							asprintf(&m.mime, "video/mpeg");
						}
					}
					else if( strcmp(ctx->iformat->name, "mpeg") == 0 )
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MPEG2 PS\n", video_stream, path, m.resolution);
						char region[5];
						if( (ctx->streams[video_stream]->codec->height == 576) ||
						    (ctx->streams[video_stream]->codec->height == 288) )
							strcpy(region, "PAL");
						else
							strcpy(region, "NTSC");
						asprintf(&m.dlna_pn, "MPEG_PS_%s;DLNA.ORG_OP=01;DLNA.ORG_CI=0", region);
						asprintf(&m.mime, "video/mpeg");
					}
					else
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s [UNKNOWN CONTAINER] is %s MPEG2\n", video_stream, path, m.resolution);
					}
					break;
				case CODEC_ID_H264:
					if( strcmp(ctx->iformat->name, "mpegts") == 0 )
					{
						tsinfo_t * ts = ctx->priv_data;
						if( ts->packet_size == 192 )
						{
							if( dlna_timestamp_is_present(path) )
								ts_timestamp = VALID;
							else
								ts_timestamp = EMPTY;
						}
						char res = '\0';
						if( ctx->streams[video_stream]->codec->width  <= 720 &&
						    ctx->streams[video_stream]->codec->height <= 576 &&
						    ctx->streams[video_stream]->codec->bit_rate <= 10000000 )
							res = 'S';
						else if( ctx->streams[video_stream]->codec->width  <= 1920 &&
						         ctx->streams[video_stream]->codec->height <= 1152 &&
						         ctx->streams[video_stream]->codec->bit_rate <= 20000000 )
							res = 'H';
						if( res )
						{
							switch( audio_profile )
							{
								case MP3:
									asprintf(&m.dlna_pn, "AVC_TS_MP_HD_MPEG1_L3%s;DLNA.ORG_OP=01;DLNA.ORG_CI=0",
										ts_timestamp==NONE?"_ISO" : ts_timestamp==VALID?"_T":"");
									break;
								case AC3:
									asprintf(&m.dlna_pn, "AVC_TS_MP_HD_AC3%s;DLNA.ORG_OP=01;DLNA.ORG_CI=0",
										ts_timestamp==NONE?"_ISO" : ts_timestamp==VALID?"_T":"");
									break;
								case AAC_MULT5:
									asprintf(&m.dlna_pn, "AVC_TS_MP_HD_AAC_MULT5%s;DLNA.ORG_OP=01;DLNA.ORG_CI=0",
										ts_timestamp==NONE?"_ISO" : ts_timestamp==VALID?"_T":"");
									break;
								default:
									DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for TS/AVC/%cD file %s\n", res, path);
									break;
							}
							if( m.dlna_pn && (ts_timestamp != NONE) )
								asprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
						}
						else
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 video profile! [%dx%d, %dbps]\n",
								ctx->streams[video_stream]->codec->width,
								ctx->streams[video_stream]->codec->height,
								ctx->streams[video_stream]->codec->bit_rate);
						}
					}
					else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
					{
						/* AVC wrapped in MP4 only has SD profiles - 10 Mbps max */
						if( ctx->streams[video_stream]->codec->width  <= 720 &&
						    ctx->streams[video_stream]->codec->height <= 576 &&
						    ctx->streams[video_stream]->codec->bit_rate <= 10000000 )
						{
							switch( audio_profile )
							{
								case AC3:
									asprintf(&m.dlna_pn, "AVC_MP4_MP_SD_AC3;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
									break;
								case AAC_MULT5:
									asprintf(&m.dlna_pn, "AVC_MP4_MP_SD_AAC_MULT5;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
									break;
								default:
									DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for MP4/AVC/SD file %s\n", path);
									break;
							}
						}
					}
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is h.264\n", video_stream, path);
					break;
				case CODEC_ID_MPEG4:
					if( ctx->streams[video_stream]->codec->codec_tag == get_fourcc("XVID") )
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s XViD\n", video_stream, path, m.resolution);
						asprintf(&m.mime, "video/divx");
					}
					else if( ctx->streams[video_stream]->codec->codec_tag == get_fourcc("DX50") )
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s DiVX5\n", video_stream, path, m.resolution);
						asprintf(&m.mime, "video/divx");
					}
					else if( ctx->streams[video_stream]->codec->codec_tag == get_fourcc("DIVX") )
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is DiVX\n", video_stream, path);
						asprintf(&m.mime, "video/divx");
					}
					else
					{
						DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MPEG4 [%X]\n", video_stream, path, ctx->streams[video_stream]->codec->codec_tag);
					}
					break;
				case CODEC_ID_WMV3:
				case CODEC_ID_VC1:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is VC1\n", video_stream, path);
					char profile[5]; profile[0] = '\0';
					asprintf(&m.mime, "video/x-ms-wmv");
					if( (ctx->streams[video_stream]->codec->width  <= 352) &&
					    (ctx->streams[video_stream]->codec->height <= 288) &&
					    (ctx->bit_rate/8 <= 384000) )
					{
						switch( audio_profile )
						{
							case MP3:
								asprintf(&m.dlna_pn, "WMVSPML_MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							case WMA_BASE:
								asprintf(&m.dlna_pn, "WMVSPML_BASE;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							default:
								DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVSPML/0x%X file %s\n", audio_profile, path);
								break;
						}
					}
					else if( (ctx->streams[video_stream]->codec->width  <= 720) &&
					         (ctx->streams[video_stream]->codec->height <= 576) &&
					         (ctx->bit_rate/8 <= 10000000) )
					{
						switch( audio_profile )
						{
							case WMA_PRO:
								asprintf(&m.dlna_pn, "WMVMED_PRO;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							case WMA_FULL:
								asprintf(&m.dlna_pn, "WMVMED_FULL;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							case WMA_BASE:
								asprintf(&m.dlna_pn, "WMVMED_BASE;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							default:
								DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVMED/0x%X file %s\n", audio_profile, path);
								break;
						}
					}
					else if( (ctx->streams[video_stream]->codec->width  <= 1920) &&
					         (ctx->streams[video_stream]->codec->height <= 1080) &&
					         (ctx->bit_rate/8 <= 20000000) )
					{
						switch( audio_profile )
						{
							case WMA_PRO:
								asprintf(&m.dlna_pn, "WMVHIGH_PRO;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							case WMA_FULL:
								asprintf(&m.dlna_pn, "WMVHIGH_FULL;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
								break;
							default:
								DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVHIGH/0x%X file %s\n", audio_profile, path);
								break;
						}
					}
					break;
				case CODEC_ID_XVID:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s UNKNOWN XVID\n", video_stream, path, m.resolution);
					break;
				case CODEC_ID_MSMPEG4V1:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MS MPEG4 v1\n", video_stream, path, m.resolution);
				case CODEC_ID_MSMPEG4V3:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MS MPEG4 v3\n", video_stream, path, m.resolution);
					asprintf(&m.mime, "video/avi");
					break;
				case CODEC_ID_H263I:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is h.263i\n", video_stream, path);
					break;
				case CODEC_ID_MJPEG:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MJPEG\n", video_stream, path);
					break;
				default:
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %d\n", video_stream, path, ctx->streams[video_stream]->codec->codec_id);
					break;
			}
		}
		if( !m.mime )
		{
			if( strcmp(ctx->iformat->name, "avi") == 0 )
				asprintf(&m.mime, "video/x-msvideo");
			else if( strcmp(ctx->iformat->name, "mpegts") == 0 )
				asprintf(&m.mime, "video/mpeg");
			else if( strcmp(ctx->iformat->name, "mpeg") == 0 )
				asprintf(&m.mime, "video/mpeg");
			else if( strcmp(ctx->iformat->name, "asf") == 0 )
				asprintf(&m.mime, "video/x-ms-wmv");
			else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
				asprintf(&m.mime, "video/mp4");
		}
		av_close_input_file(ctx);
	}
	else
	{
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed!\n", path);
	}

	sql = sqlite3_mprintf(	"INSERT into DETAILS"
				" (PATH, SIZE, DURATION, DATE, CHANNELS, BITRATE, SAMPLERATE, RESOLUTION,"
				"  TITLE, DLNA_PN, MIME) "
				"VALUES"
				" (%Q, %lld, %Q, %Q, %Q, %Q, %Q, %Q, '%q', %Q, '%q');",
				path, size, m.duration,
				strlen(date) ? date : NULL,
				m.channels,
				m.bitrate,
				m.frequency,
				m.resolution,
				name, m.dlna_pn, m.mime);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "SQL: %s\n", sql);
	if( sql_exec(db, sql) != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	sqlite3_free(sql);
	if( m.dlna_pn )
		free(m.dlna_pn);
	if( m.mime )
		free(m.mime);
	if( m.duration )
		free(m.duration);
	if( m.resolution )
		free(m.resolution);
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
