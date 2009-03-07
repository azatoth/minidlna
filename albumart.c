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
#include <gd.h>

#include "upnpglobalvars.h"
#include "sql.h"
#include "utils.h"
#include "log.h"

/* For libjpeg error handling */
jmp_buf setjmp_buffer;
static void libjpeg_error_handler(j_common_ptr cinfo)
{
	cinfo->err->output_message (cinfo);
	longjmp(setjmp_buffer, 1);
	return;
}

#if 0 // Not needed currently
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
#endif

/* Use our own boxfilter resizer, because gdCopyImageResampled is slow,
 * and gdCopyImageResized looks horrible when you downscale this much. */
#define N_FRAC 8
#define MASK_FRAC ((1 << N_FRAC) - 1)
#define ROUND2(v) (((v) + (1 << (N_FRAC - 1))) >> N_FRAC)
#define DIV(x, y) ( ((x) << (N_FRAC - 3)) / ((y) >> 3) )
static void
boxfilter_resize(gdImagePtr dst, gdImagePtr src,
                 int dstX, int dstY, int srcX, int srcY,
                 int dstW, int dstH, int srcW, int srcH)
{
	int x, y;
	int sy1, sy2, sx1, sx2;

	if(!dst->trueColor)
	{
		gdImageCopyResized(dst, src, dstX, dstY, srcX, srcY, dstW, dstH,
				   srcW, srcH);
		return;
	}
	for(y = dstY; y < (dstY + dstH); y++)
	{
		sy1 = (((y - dstY) * srcH) << N_FRAC) / dstH;
		sy2 = (((y - dstY + 1) * srcH) << N_FRAC) / dstH;
		for(x = dstX; x < (dstX + dstW); x++)
		{
			int sx, sy;
			int spixels = 0;
			int red = 0, green = 0, blue = 0, alpha = 0;
			sx1 = (((x - dstX) * srcW) << N_FRAC) / dstW;
			sx2 = (((x - dstX + 1) * srcW) << N_FRAC) / dstW;
			sy = sy1;
			do {
				int yportion;
				if((sy >> N_FRAC) == (sy1 >> N_FRAC))
				{
					yportion = (1 << N_FRAC) - (sy & MASK_FRAC);
					if(yportion > sy2 - sy1)
					{
						yportion = sy2 - sy1;
					}
					sy = sy & ~MASK_FRAC;
				}
				else if(sy == (sy2 & ~MASK_FRAC))
				{
					yportion = sy2 & MASK_FRAC;
				}
				else
				{
					yportion = (1 << N_FRAC);
				}
				sx = sx1;
				do {
					int xportion;
					int pcontribution;
					int p;
					if((sx >> N_FRAC) == (sx1 >> N_FRAC))
					{
						xportion = (1 << N_FRAC) - (sx & MASK_FRAC);
						if(xportion > sx2 - sx1)
						{
							xportion = sx2 - sx1;
						}
						sx = sx & ~MASK_FRAC;
					}
					else if(sx == (sx2 & ~MASK_FRAC))
					{
						xportion = sx2 & MASK_FRAC;
					}
					else
					{
						xportion = (1 << N_FRAC);
					}

					if(xportion && yportion)
					{
						pcontribution = (xportion * yportion) >> N_FRAC;
						p = gdImageGetTrueColorPixel(src, ROUND2(sx) + srcX, ROUND2(sy) + srcY);
						if(pcontribution == (1 << N_FRAC))
						{
							// optimization for down-scaler, which many pixel has pcontribution=1
							red += gdTrueColorGetRed(p) << N_FRAC;
							green += gdTrueColorGetGreen(p) << N_FRAC;
							blue += gdTrueColorGetBlue(p) << N_FRAC;
							alpha += gdTrueColorGetAlpha(p) << N_FRAC;
							spixels += (1 << N_FRAC);
						}
						else
						{
							red += gdTrueColorGetRed(p) * pcontribution;
							green += gdTrueColorGetGreen(p) * pcontribution;
							blue += gdTrueColorGetBlue(p) * pcontribution;
							alpha += gdTrueColorGetAlpha(p) * pcontribution;
							spixels += pcontribution;
						}
					}
					sx += (1 << N_FRAC);
				}
				while(sx < sx2);
				sy += (1 << N_FRAC);
			}
			while(sy < sy2);
			if(spixels != 0)
			{
				red = DIV(red, spixels);
				green = DIV(green, spixels);
				blue = DIV(blue, spixels);
				alpha = DIV(alpha, spixels);
			}
			/* Clamping to allow for rounding errors above */
			if(red > (255 << N_FRAC))
				red = (255 << N_FRAC);
			if(green > (255 << N_FRAC))
				green = (255 << N_FRAC);
			if(blue > (255 << N_FRAC))
				blue = (255 << N_FRAC);
			if(alpha > (gdAlphaMax << N_FRAC))
				alpha = (gdAlphaMax << N_FRAC);
			gdImageSetPixel(dst, x, y,
					gdTrueColorAlpha(ROUND2(red), ROUND2(green), ROUND2(blue), ROUND2(alpha)));
		}
	}
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
	#if 0 // Try our box filter resizer for now
	#ifdef __sparc__
	gdImageCopyResized(imdst, imsrc, 0, 0, 0, 0, dstw, dsth, imsrc->sx, imsrc->sy);
	#else
	gdImageCopyResampled(imdst, imsrc, 0, 0, 0, 0, dstw, dsth, imsrc->sx, imsrc->sy);
	#endif
	#else
	boxfilter_resize(imdst, imsrc, 0, 0, 0, 0, dstw, dsth, imsrc->sx, imsrc->sy);
	#endif
	gdImageJpeg(imdst, dstfile, 96);
	fclose(dstfile);
	gdImageDestroy(imsrc);  
	gdImageDestroy(imdst);  

	return cache_file;
error:
	free(cache_file);
	return NULL;
}

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

/* Simple, efficient hash function from Daniel J. Bernstein */
unsigned int DJBHash(const char* str, int len)
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
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	int width = 0, height = 0;
	char * art_path = NULL;
	char * cache_dir;
	FILE * dstfile;
	size_t nwritten;
	static char last_path[PATH_MAX];
	static unsigned int last_hash = 0;
	unsigned int hash;

	/* If the embedded image matches the embedded image from the last file we
	 * checked, just make a hard link.  No use in storing it on the disk twice. */
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

	cinfo.err = jpeg_std_error(&jerr);
	jerr.error_exit = libjpeg_error_handler;
	jpeg_create_decompress(&cinfo);
	if( setjmp(setjmp_buffer) )
		goto error;
	jpeg_memory_src(&cinfo, (unsigned char *)image_data, image_size);
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);
	width = cinfo.output_width;
	height = cinfo.output_height;
	error:
	jpeg_destroy_decompress(&cinfo);

	if( width > 160 || height > 160 )
	{
		art_path = save_resized_album_art((void *)image_data, path, width, height, 0, image_size);
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
