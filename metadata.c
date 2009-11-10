/*  MiniDLNA media server
 *  Copyright (C) 2008-2009  Justin Maggard
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
#include "image_utils.h"
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
#define FLAG_BAND	0x00000020

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

/* This function taken from libavutil (ffmpeg), because it's not included with all versions of libavutil. */
int
get_fourcc(const char *s)
{
	return (s[0]) + (s[1]<<8) + (s[2]<<16) + (s[3]<<24);
}

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
	char duration[16], mime[16], type[4];
	static char lang[6] = { '\0' };
	struct stat file;
	sqlite_int64 ret;
	char *title, *artist = NULL, *album = NULL, *band = NULL, *genre = NULL, *comment = NULL, *date = NULL;
	char *esc_tag;
	int i, free_flags = 0;
	sqlite_int64 album_art = 0;
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
	else if( ends_with(path, ".3gp") )
	{
		strcpy(type, "aac");
		strcpy(mime, "audio/3gpp");
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
	else if( ends_with(path, ".wav") )
	{
		strcpy(type, "wav");
		strcpy(mime, "audio/x-wav");
	}
	else if( ends_with(path, ".ogg") )
	{
		strcpy(type, "ogg");
		strcpy(mime, "application/ogg");
	}
	else if( ends_with(path, ".pcm") )
	{
		strcpy(type, "pcm");
		strcpy(mime, "audio/L16");
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
		return 0;
	}

	if( song.dlna_pn )
		asprintf(&dlna_pn, "%s;DLNA.ORG_OP=01", song.dlna_pn);
	if( song.year )
		asprintf(&date, "%04d-01-01", song.year);
	sprintf(duration, "%d:%02d:%02d.%03d",
	                  (song.song_length/3600000),
	                  (song.song_length/60000%60),
	                  (song.song_length/1000%60),
	                  (song.song_length%1000));
	title = song.title;
	if( title && *title )
	{
		title = trim(title);
		if( (esc_tag = escape_tag(title)) )
		{
			free_flags |= FLAG_TITLE;
			title = esc_tag;
		}
	}
	else
	{
		title = name;
	}
	for( i=ROLE_START; i<N_ROLE; i++ )
	{
		if( song.contributor[i] && *song.contributor[i] )
		{
			artist = trim(song.contributor[i]);
			if( strlen(artist) > 48 )
			{
				free_flags |= FLAG_ARTIST;
				artist = strdup("Various Artists");
			}
			else if( (esc_tag = escape_tag(artist)) )
			{
				free_flags |= FLAG_ARTIST;
				artist = esc_tag;
			}
			band = artist;
			break;
		}
	}
	/* If there is a band associated with the album, use it for virtual containers. */
	if( (i != ROLE_BAND) && song.contributor[ROLE_BAND] && *song.contributor[ROLE_BAND] )
	{
		band = trim(song.contributor[ROLE_BAND]);
		if( strlen(band) > 48 )
		{
			free_flags |= FLAG_BAND;
			band = strdup("Various Artists");
		}
		else if( (esc_tag = escape_tag(band)) )
		{
			free_flags |= FLAG_BAND;
			band = esc_tag;
		}
	}
	if( song.album && *song.album )
	{
		album = trim(song.album);
		if( (esc_tag = escape_tag(album)) )
		{
			free_flags |= FLAG_ALBUM;
			album = esc_tag;
		}
	}
	if( song.genre && *song.genre )
	{
		genre = trim(song.genre);
		if( (esc_tag = escape_tag(genre)) )
		{
			free_flags |= FLAG_GENRE;
			genre = esc_tag;
		}
	}
	if( song.comment && *song.comment )
	{
		comment = trim(song.comment);
		if( (esc_tag = escape_tag(comment)) )
		{
			free_flags |= FLAG_COMMENT;
			comment = esc_tag;
		}
	}

	album_art = find_album_art(path, song.image, song.image_size);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, SIZE, TIMESTAMP, DURATION, CHANNELS, BITRATE, SAMPLERATE, DATE,"
	                   "  TITLE, CREATOR, ARTIST, ALBUM, GENRE, COMMENT, DISC, TRACK, DLNA_PN, MIME, ALBUM_ART) "
	                   "VALUES"
	                   " (%Q, %lld, %ld, '%s', %d, %d, %d, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %d, %d, %Q, '%s', %lld);",
	                   path, file.st_size, file.st_mtime, duration, song.channels, song.bitrate, song.samplerate, date,
	                   title, band, artist, album, genre, comment, song.disc, song.track,
	                   dlna_pn, song.mime?song.mime:mime, album_art);
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
	if( free_flags & FLAG_BAND )
		free(band);
	if( free_flags & FLAG_GENRE )
		free(genre);
	if( free_flags & FLAG_COMMENT )
		free(comment);

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
	char *date = NULL, *cam = NULL;
	char make[32], model[64] = {'\0'};
	char b[1024];
	struct stat file;
	sqlite_int64 ret;
	image * imsrc;
	metadata_t m;
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
		date = strdup(exif_entry_get_value(e, b, sizeof(b)));
		if( strlen(date) > 10 )
		{
			date[4] = '-';
			date[7] = '-';
			date[10] = 'T';
		}
		else {
			free(date);
			date = NULL;
		}
	}
	else {
		/* One last effort to get the date from XMP */
		image_get_jpeg_date_xmp(path, &date);
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * date: %s\n", date);

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
			cam = strdup(model);
		}
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * model: %s\n", model);

	if( ed->size )
	{
		/* We might need to verify that the thumbnail is 160x160 or smaller */
		if( ed->size > 12000 )
		{
			imsrc = image_new_from_jpeg(NULL, 0, (char *)ed->data, ed->size);
			if( imsrc )
			{
 				if( (imsrc->width <= 160) && (imsrc->height <= 160) )
				{
					thumb = 1;
				}
				image_free(imsrc);
			}
		}
		else
		{
			thumb = 1;
		}
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
		if( m.mime )
			free(m.mime);
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
	                   path, name, file.st_size, file.st_mtime, date, m.resolution, thumb, cam, m.dlna_pn, m.mime);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	if( m.resolution )
		free(m.resolution);
	if( m.dlna_pn )
		free(m.dlna_pn);
	if( m.mime )
		free(m.mime);
	if( date )
		free(date);
	if( cam )
		free(cam);
	return ret;
}

sqlite_int64
GetVideoMetadata(const char * path, char * name)
{
	struct stat file;
	int ret, i;
	struct tm *modtime;
	char date[20];
	AVFormatContext *ctx;
	int audio_stream = -1, video_stream = -1;
	enum audio_profiles audio_profile = PROFILE_AUDIO_UNKNOWN;
	ts_timestamp_t ts_timestamp = NONE;
	int duration, hours, min, sec, ms;
	aac_object_type_t aac_type = AAC_INVALID;
	sqlite_int64 album_art = 0;
	metadata_t m;
	memset(&m, '\0', sizeof(m));
	date[0] = '\0';

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Parsing video %s...\n", name);
	if ( stat(path, &file) != 0 )
		return 0;
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %jd\n", file.st_size);

	modtime = localtime(&file.st_mtime);
	strftime(date, sizeof(date), "%FT%T", modtime);

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
		/* NOTE: The DLNA spec only provides for ASF (WMV), TS, PS, and MP4 containers -- not AVI. */
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
				if( strcmp(ctx->iformat->name, "mpegts") == 0 )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MPEG2 TS\n", video_stream, basename(path), m.resolution);
					char res;
					tsinfo_t * ts = ctx->priv_data;
					if( ts->packet_size == 192 )
					{
						asprintf(&m.dlna_pn, "MPEG_TS_HD_NA;%s", dlna_no_conv);
						asprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
					}
					else if( ts->packet_size == 188 )
					{
						if( (ctx->streams[video_stream]->codec->width  >= 1280) &&
						    (ctx->streams[video_stream]->codec->height >= 720) )
							res = 'H';
						else
							res = 'S';
						asprintf(&m.dlna_pn, "MPEG_TS_%cD_NA_ISO;%s", res, dlna_no_conv);
						asprintf(&m.mime, "video/mpeg");
					}
				}
				else if( strcmp(ctx->iformat->name, "mpeg") == 0 )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s MPEG2 PS\n", video_stream, basename(path), m.resolution);
					char region[5];
					if( (ctx->streams[video_stream]->codec->height == 576) ||
					    (ctx->streams[video_stream]->codec->height == 288) )
						strcpy(region, "PAL");
					else
						strcpy(region, "NTSC");
					asprintf(&m.dlna_pn, "MPEG_PS_%s;%s", region, dlna_no_conv);
					asprintf(&m.mime, "video/mpeg");
				}
				else
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s [UNKNOWN CONTAINER] is %s MPEG2\n", video_stream, basename(path), m.resolution);
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
							case PROFILE_AUDIO_MP3:
								asprintf(&m.dlna_pn, "AVC_TS_MP_HD_MPEG1_L3%s;%s",
									ts_timestamp==NONE?"_ISO" : ts_timestamp==VALID?"_T":"", dlna_no_conv);
								break;
							case PROFILE_AUDIO_AC3:
								asprintf(&m.dlna_pn, "AVC_TS_MP_HD_AC3%s;%s",
									ts_timestamp==NONE?"_ISO" : ts_timestamp==VALID?"_T":"", dlna_no_conv);
								break;
							case PROFILE_AUDIO_AAC_MULT5:
								asprintf(&m.dlna_pn, "AVC_TS_MP_HD_AAC_MULT5%s;%s",
									ts_timestamp==NONE?"_ISO" : ts_timestamp==VALID?"_T":"", dlna_no_conv);
								break;
							default:
								DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for TS/AVC/%cD file %s\n", res, basename(path));
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
					    ctx->streams[video_stream]->codec->bit_rate <= 10000000 &&
					    !ends_with(path, ".mov") )
					{
						switch( audio_profile )
						{
							case PROFILE_AUDIO_AC3:
								asprintf(&m.dlna_pn, "AVC_MP4_MP_SD_AC3;%s", dlna_no_conv);
								break;
							case PROFILE_AUDIO_AAC_MULT5:
								asprintf(&m.dlna_pn, "AVC_MP4_MP_SD_AAC_MULT5;%s", dlna_no_conv);
								break;
							default:
								DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for MP4/AVC/SD file %s\n", basename(path));
								break;
						}
					}
				}
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is h.264\n", video_stream, basename(path));
				break;
			case CODEC_ID_MPEG4:
				if( ctx->streams[video_stream]->codec->codec_tag == get_fourcc("XVID") )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s XViD\n", video_stream, basename(path), m.resolution);
					asprintf(&m.artist, "DiVX");
				}
				else if( ctx->streams[video_stream]->codec->codec_tag == get_fourcc("DX50") )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is %s DiVX5\n", video_stream, basename(path), m.resolution);
					asprintf(&m.artist, "DiVX");
				}
				else if( ctx->streams[video_stream]->codec->codec_tag == get_fourcc("DIVX") )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is DiVX\n", video_stream, basename(path));
					asprintf(&m.artist, "DiVX");
				}
				else if( ends_with(path, ".3gp") && (strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0) )
				{
					asprintf(&m.mime, "video/3gpp");
					switch( audio_profile )
					{
						case PROFILE_AUDIO_AAC:
							asprintf(&m.dlna_pn, "MPEG4_P2_3GPP_SP_L0B_AAC;%s", dlna_no_conv);
							break;
						case PROFILE_AUDIO_AMR:
							asprintf(&m.dlna_pn, "MPEG4_P2_3GPP_SP_L0B_AMR;%s", dlna_no_conv);
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for MPEG4-P2 3GP/0x%X file %s\n",
							        ctx->streams[audio_stream]->codec->codec_id, basename(path));
							break;
					}
				}
				else
				{
					DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MPEG4 [%X]\n", video_stream, basename(path), ctx->streams[video_stream]->codec->codec_tag);
				}
				break;
			case CODEC_ID_WMV3:
			case CODEC_ID_VC1:
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is VC1\n", video_stream, basename(path));
				char profile[5]; profile[0] = '\0';
				asprintf(&m.mime, "video/x-ms-wmv");
				if( (ctx->streams[video_stream]->codec->width  <= 352) &&
				    (ctx->streams[video_stream]->codec->height <= 288) &&
				    (ctx->bit_rate/8 <= 384000) )
				{
					switch( audio_profile )
					{
						case PROFILE_AUDIO_MP3:
							asprintf(&m.dlna_pn, "WMVSPML_MP3;%s", dlna_no_conv);
							break;
						case PROFILE_AUDIO_WMA_BASE:
							asprintf(&m.dlna_pn, "WMVSPML_BASE;%s", dlna_no_conv);
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVSPML/0x%X file %s\n", audio_profile, basename(path));
							break;
					}
				}
				else if( (ctx->streams[video_stream]->codec->width  <= 720) &&
				         (ctx->streams[video_stream]->codec->height <= 576) &&
				         (ctx->bit_rate/8 <= 10000000) )
				{
					switch( audio_profile )
					{
						case PROFILE_AUDIO_WMA_PRO:
							asprintf(&m.dlna_pn, "WMVMED_PRO;%s", dlna_no_conv);
							break;
						case PROFILE_AUDIO_WMA_FULL:
							asprintf(&m.dlna_pn, "WMVMED_FULL;%s", dlna_no_conv);
							break;
						case PROFILE_AUDIO_WMA_BASE:
							asprintf(&m.dlna_pn, "WMVMED_BASE;%s", dlna_no_conv);
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVMED/0x%X file %s\n", audio_profile, basename(path));
							break;
					}
				}
				else if( (ctx->streams[video_stream]->codec->width  <= 1920) &&
				         (ctx->streams[video_stream]->codec->height <= 1080) &&
				         (ctx->bit_rate/8 <= 20000000) )
				{
					switch( audio_profile )
					{
						case PROFILE_AUDIO_WMA_PRO:
							asprintf(&m.dlna_pn, "WMVHIGH_PRO;%s", dlna_no_conv);
							break;
						case PROFILE_AUDIO_WMA_FULL:
							asprintf(&m.dlna_pn, "WMVHIGH_FULL;%s", dlna_no_conv);
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVHIGH/0x%X file %s\n", audio_profile, basename(path));
							break;
					}
				}
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
		else if( strcmp(ctx->iformat->name, "mpegts") == 0 )
			asprintf(&m.mime, "video/mpeg");
		else if( strcmp(ctx->iformat->name, "mpeg") == 0 )
			asprintf(&m.mime, "video/mpeg");
		else if( strcmp(ctx->iformat->name, "asf") == 0 )
			asprintf(&m.mime, "video/x-ms-wmv");
		else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
			if( ends_with(path, ".mov") )
				asprintf(&m.mime, "video/quicktime");
			else
				asprintf(&m.mime, "video/mp4");
		else if( strcmp(ctx->iformat->name, "matroska") == 0 )
			asprintf(&m.mime, "video/x-matroska");
		else if( strcmp(ctx->iformat->name, "flv") == 0 )
			asprintf(&m.mime, "video/x-flv");
		else
			DPRINTF(E_WARN, L_METADATA, "%s: Unhandled format: %s\n", path, ctx->iformat->name);
	}
	av_close_input_file(ctx);
#ifdef TIVO_SUPPORT
	if( ends_with(path, ".TiVo") && is_tivo_file(path) )
	{
		free(m.mime);
		asprintf(&m.mime, "video/x-tivo-mpeg");
	}
#endif
	album_art = find_album_art(path, NULL, 0);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, SIZE, TIMESTAMP, DURATION, DATE, CHANNELS, BITRATE, SAMPLERATE, RESOLUTION,"
	                   "  CREATOR, TITLE, DLNA_PN, MIME, ALBUM_ART) "
	                   "VALUES"
	                   " (%Q, %lld, %ld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, '%q', %Q, '%q', %lld);",
	                   path, file.st_size, file.st_mtime, m.duration,
	                   strlen(date) ? date : NULL,
	                   m.channels, m.bitrate, m.frequency, m.resolution,
	                   m.artist, name, m.dlna_pn, m.mime, album_art);
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
	if( m.artist )
		free(m.artist);

	return ret;
}
