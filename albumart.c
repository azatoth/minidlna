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
#undef HAVE_LIBID3TAG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <setjmp.h>

#include <jpeglib.h>
#include <gd.h>

#include "upnpglobalvars.h"
#include "sql.h"
#include "utils.h"

/* For libjpeg error handling */
jmp_buf setjmp_buffer;
static void libjpeg_error_handler(j_common_ptr cinfo)
{
	cinfo->err->output_message (cinfo);
	longjmp(setjmp_buffer, 1);
	return;
}

int
check_res(int width, int height, char * dlna_pn)
{
	if( (width <= 0) || (height <= 0) )
		return 0;
	if( width <= 160 && height <= 160 )
		strcpy(dlna_pn, "JPEG_TN");
	else if( width <= 640 && height <= 480 )
		strcpy(dlna_pn, "JPEG_SM");
	else if( width <= 1024 && height <= 768 )
		strcpy(dlna_pn, "JPEG_MED");
	else if( width <= 4096 && height <= 4096 )
		strcpy(dlna_pn, "JPEG_LRG");
	else
		return 0;
	return 1;
}

char *
save_resized_album_art(void * ptr, const char * path, int srcw, int srch, int file, int size)
{
	FILE *dstfile;
	gdImagePtr imsrc = 0, imdst = 0;
	int dstw, dsth;
	char * cache_file;
	char * cache_dir;

	asprintf(&cache_file, DB_PATH "/art_cache%s", path);
	if( access(cache_file, F_OK) == 0 )
		return cache_file;

	cache_dir = strdup(cache_file);
	make_dir(dirname(cache_dir), S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	free(cache_dir);

	if( file )
		imsrc = gdImageCreateFromJpeg((FILE *)ptr);
	else
		imsrc = gdImageCreateFromJpegPtr(size, ptr);
	if( !imsrc )
		goto error;

	dstfile = fopen(cache_file, "w");
	if( !dstfile )
		goto error;

	if( srcw > srch )
	{
		dstw = 160;
		dsth = (srch<<8) / ((srcw<<8)/160);
	}
	else
	{
		dstw = (srcw<<8) / ((srch<<8)/160);
		dsth = 160;
	}
	imdst = gdImageCreateTrueColor(dstw, dsth);
	if( !imdst )
	{
		gdImageDestroy(imsrc);  
		fclose(dstfile);
		goto error;
	}
	#ifdef __sparc__
	gdImageCopyResized(imdst, imsrc, 0, 0, 0, 0, dstw, dsth, imsrc->sx, imsrc->sy);
	#else
	gdImageCopyResampled(imdst, imsrc, 0, 0, 0, 0, dstw, dsth, imsrc->sx, imsrc->sy);
	#endif
	gdImageJpeg(imdst, dstfile, -1);
	fclose(dstfile);
	gdImageDestroy(imsrc);  
	gdImageDestroy(imdst);  

	return cache_file;
error:
	free(cache_file);
	return NULL;
}


#ifdef HAVE_LIBID3TAG
#include <id3tag.h>

/* These next few functions are to allow loading JPEG data directly from memory for libjpeg.
 * The standard functions only allow you to read from a file.
 * This code comes from the JpgAlleg library, at http://wiki.allegro.cc/index.php?title=Libjpeg */
struct
my_src_mgr
{
	struct jpeg_source_mgr pub;
	JOCTET eoi_buffer[2];
};

static void
init_source(j_decompress_ptr cinfo)
{
}

static int
fill_input_buffer(j_decompress_ptr cinfo)
{
	return 1;
}

static void
skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	struct my_src_mgr *src = (void *)cinfo->src;
	if (num_bytes > 0)
	{
		while (num_bytes > (long)src->pub.bytes_in_buffer)
		{
			num_bytes -= (long)src->pub.bytes_in_buffer;
			fill_input_buffer(cinfo);
		}
	}
	src->pub.next_input_byte += num_bytes;
	src->pub.bytes_in_buffer -= num_bytes;
}

static void
term_source(j_decompress_ptr cinfo)
{
}

void
jpeg_memory_src(j_decompress_ptr cinfo, unsigned char const *buffer, size_t bufsize)
{
        struct my_src_mgr *src;
        if (! cinfo->src)
        {
                cinfo->src = (*cinfo->mem->alloc_small)((void *)cinfo, JPOOL_PERMANENT, sizeof(struct my_src_mgr));;
        }
        src = (void *)cinfo->src;
        src->pub.init_source = init_source;
        src->pub.fill_input_buffer = fill_input_buffer;
        src->pub.skip_input_data = skip_input_data;
        src->pub.resync_to_restart = jpeg_resync_to_restart;
        src->pub.term_source = term_source;
        src->pub.next_input_byte = buffer;
        src->pub.bytes_in_buffer = bufsize;
}

/* And our main album art functions */
char *
check_embedded_art(const char * path, char * dlna_pn)
{
	struct id3_file *file;
	struct id3_tag *pid3tag;
	struct id3_frame *pid3frame;
	id3_byte_t const *image;
	id3_latin1_t const *mime;
	id3_length_t length;
	int index;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	int width = 0, height = 0;
	char * art_path = NULL;

	file = id3_file_open(path, ID3_FILE_MODE_READONLY);
	if( !file )
		return 0;

	pid3tag = id3_file_tag(file);

	for( index=0; (pid3frame = id3_tag_findframe(pid3tag, "", index)); index++ )
	{
		if( strcmp(pid3frame->id, "APIC") == 0 )
		{
			mime = id3_field_getlatin1(&pid3frame->fields[1]);
			if( strcmp((char*)mime, "image/jpeg") && strcmp((char*)mime, "jpeg") )
				continue;
			image = id3_field_getbinarydata(&pid3frame->fields[4], &length);

			cinfo.err = jpeg_std_error(&jerr);
			jerr.error_exit = libjpeg_error_handler;
			jpeg_create_decompress(&cinfo);
			if( setjmp(setjmp_buffer) )
				goto error;
			jpeg_memory_src(&cinfo, image, length);
			jpeg_read_header(&cinfo, TRUE);
			jpeg_start_decompress(&cinfo);
			width = cinfo.output_width;
			height = cinfo.output_height;
			error:
			jpeg_destroy_decompress(&cinfo);
			break;
		}
	}
	if( width > 160 || height > 160 )
	{
		art_path = save_resized_album_art((void *)image, path, width, height, 0, length);
	}
	else if( width > 0 && height > 0 )
	{
		art_path = path;
	}
	id3_file_close(file);

	return(art_file);
}
#endif // HAVE_LIBID3TAG

char *
check_for_album_file(char * dir, char * dlna_pn)
{
	char * file = malloc(PATH_MAX);
	struct album_art_name_s * album_art_name;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	static FILE * infile;
	int width=0, height=0;
	char * art_file;

	for( album_art_name = album_art_names; album_art_name; album_art_name = album_art_name->next )
	{
		sprintf(file, "%s/%s", dir, album_art_name->name);
		if( access(file, R_OK) == 0 )
		{
			infile = fopen(file, "r");
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
			if( width > 160 || height > 160 )
			{
				art_file = file;
				rewind(infile);
				file = save_resized_album_art((void *)infile, art_file, width, height, 1, 0);
				free(art_file);
			}
			error:
			jpeg_destroy_decompress(&cinfo);
			fclose(infile);

			return(file);
		}
	}
	free(file);
	return NULL;
}

sqlite_int64
find_album_art(const char * path, char * dlna_pn)
{
	char * album_art = NULL;
	char * sql;
	char ** result;
	int cols, rows;
	sqlite_int64 ret = 0;
	char * mypath = strdup(path);

	#ifdef HAVE_LIBID3TAG
	if( check_embedded_art(path, dlna_pn) || (album_art = check_for_album_file(dirname(mypath), dlna_pn)) )
	#else
	if( (album_art = check_for_album_file(dirname(mypath), dlna_pn)) )
	#endif
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
			sql = sqlite3_mprintf(	"INSERT into ALBUM_ART"
						" (PATH, EMBEDDED) "
						"VALUES"
						" ('%s', %d);",
						(album_art ? album_art : path),
						(album_art ? 0 : 1) );
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
