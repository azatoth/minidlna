/*  MiniDLNA media server
 *  Copyright (C) 2009  Justin Maggard
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

/* These functions are mostly based on code from other projects.
 * There are function to effiently resize a JPEG image, and some utility functions.
 * They are here to allow loading and saving JPEG data directly to or from memory with libjpeg.
 * The standard functions only allow you to read from or write to a file.
 *
 * The reading code comes from the JpgAlleg library, at http://wiki.allegro.cc/index.php?title=Libjpeg
 * The writing code was posted on a Google group from openjpeg, at http://groups.google.com/group/openjpeg/browse_thread/thread/331e6cf60f70797f
 * The resize functions come from the resize_image project, at http://www.golac.fr/Image-Resizer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <jpeglib.h>

#include "image_utils.h"
#include "log.h"

#define JPEG_QUALITY  96

#define COL(red, green, blue) (((red) << 24) | ((green) << 16) | ((blue) << 8) | 0xFF)
#define COL_FULL(red, green, blue, alpha) (((red) << 24) | ((green) << 16) | ((blue) << 8) | (alpha))
#define COL_RED(col)   (col >> 24)
#define COL_GREEN(col) ((col >> 16) & 0xFF)
#define COL_BLUE(col)  ((col >> 8) & 0xFF)
#define COL_ALPHA(col) (col & 0xFF)
#define BLACK  0x000000FF


struct my_dst_mgr {
	struct jpeg_destination_mgr jdst;
	JOCTET *buf;
	JOCTET *off;
	size_t sz;
	size_t used;
};

/* Destination manager to store data in a buffer */
static void
my_dst_mgr_init(j_compress_ptr cinfo)
{
	struct my_dst_mgr *dst = (void *)cinfo->dest;

	dst->used = 0;
	dst->sz = cinfo->image_width
		  * cinfo->image_height
		  * cinfo->input_components;
	dst->buf = malloc(dst->sz * sizeof *dst->buf);
	dst->off = dst->buf;
	dst->jdst.next_output_byte = dst->off;
	dst->jdst.free_in_buffer = dst->sz;

	return;

}

static boolean
my_dst_mgr_empty(j_compress_ptr cinfo)
{
	struct my_dst_mgr *dst = (void *)cinfo->dest;

	dst->sz *= 2;
	dst->used = dst->off - dst->buf;
	dst->buf = realloc(dst->buf, dst->sz * sizeof *dst->buf);
	dst->off = dst->buf + dst->used;
	dst->jdst.next_output_byte = dst->off;
	dst->jdst.free_in_buffer = dst->sz - dst->used;

	return TRUE;

}

static void
my_dst_mgr_term(j_compress_ptr cinfo)
{
	struct my_dst_mgr *dst = (void *)cinfo->dest;

	dst->used += dst->sz - dst->jdst.free_in_buffer;
	dst->off = dst->buf + dst->used;

	return;

}

static void
jpeg_memory_dest(j_compress_ptr cinfo, struct my_dst_mgr *dst)
{
	dst->jdst.init_destination = my_dst_mgr_init;
	dst->jdst.empty_output_buffer = my_dst_mgr_empty;
	dst->jdst.term_destination = my_dst_mgr_term;
	cinfo->dest = (void *)dst;

	return;

}

/* Source manager to read data from a buffer */
struct
my_src_mgr
{
	struct jpeg_source_mgr pub;
	JOCTET eoi_buffer[2];
};

static void
init_source(j_decompress_ptr cinfo)
{
	return;
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
	return;
}

void
jpeg_memory_src(j_decompress_ptr cinfo, const unsigned char * buffer, size_t bufsize)
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

/* Don't exit on error like libjpeg likes to do */
static void
libjpeg_error_handler(j_common_ptr cinfo)
{
	cinfo->err->output_message(cinfo);
	return;
}

void
image_free(image *pimage)
{
	free(pimage->buf);
	free(pimage);
}

pix
get_pix(image *pimage, int32_t x, int32_t y)
{
	if((x >= 0) && (y >= 0) && (x < pimage->width) && (y < pimage->height))
	{
		return(pimage->buf[(y * pimage->width) + x]);
	}
	else
	{
		pix vpix = BLACK;
		return(vpix);
	}
}

void
put_pix_alpha_replace(image *pimage, int32_t x, int32_t y, pix col)
{
	if((x >= 0) && (y >= 0) && (x < pimage->width) && (y < pimage->height))
		pimage->buf[(y * pimage->width) + x] = col;
}

image *
image_new(int32_t width, int32_t height)
{
	image *vimage;

	if((vimage = (image *)malloc(sizeof(image))) == NULL)
	{
		DPRINTF(E_WARN, L_METADATA, "malloc failed\n");
		return NULL;
	}
	vimage->width = width; vimage->height = height;

	if((vimage->buf = (pix *)malloc(width * height * sizeof(pix))) == NULL)
	{
		DPRINTF(E_WARN, L_METADATA, "malloc failed\n");
		return NULL;
	}
	return(vimage);
}

image *
image_new_from_jpeg(const char * path, int is_file, const char * buf, int size)
{
	image *vimage;
	FILE  *file;
	struct jpeg_decompress_struct cinfo;
	unsigned char *line[16], *ptr;
	int x, y, i, w, h, ofs;
	int maxbuf;
	struct jpeg_error_mgr pub;


	cinfo.err = jpeg_std_error(&pub);
	pub.error_exit = libjpeg_error_handler;
	jpeg_create_decompress(&cinfo);
	if( is_file )
	{
		if( (file = fopen(path, "r")) == NULL )
		{
			return NULL;
		}
		jpeg_stdio_src(&cinfo, file);
	}
	else
	{
		jpeg_memory_src(&cinfo, (const unsigned char *)buf, size);
	}
	jpeg_read_header(&cinfo, TRUE);
	cinfo.do_fancy_upsampling = FALSE;
	cinfo.do_block_smoothing = FALSE;
	jpeg_start_decompress(&cinfo);
	w = cinfo.output_width;
	h = cinfo.output_height;
	vimage = image_new(w, h);
	if(!vimage)
	{
		jpeg_destroy_decompress(&cinfo);
		if( is_file )
			fclose(file);
		return NULL;
	}

	if(cinfo.rec_outbuf_height > 16)
	{
		DPRINTF(E_WARN, L_METADATA, "ERROR image_from_jpeg : (image_from_jpeg.c) JPEG uses line buffers > 16. Cannot load.\n");
		image_free(vimage);
		if( is_file )
			fclose(file);
		return NULL;
	}
	maxbuf = vimage->width * vimage->height;
	if(cinfo.output_components == 3)
	{
		ofs = 0;
		if((ptr = (unsigned char *)malloc(w * 3 * cinfo.rec_outbuf_height)) == NULL)
		{
			DPRINTF(E_WARN, L_METADATA, "malloc failed\n");
			return NULL;
		}

		for(y = 0; y < h; y += cinfo.rec_outbuf_height)
		{
			for(i = 0; i < cinfo.rec_outbuf_height; i++)
			{
				line[i] = ptr + (w * 3 * i);
			}
			jpeg_read_scanlines(&cinfo, line, cinfo.rec_outbuf_height);
			for(x = 0; x < w * cinfo.rec_outbuf_height; x++)
			{
				if( ofs < maxbuf )
				{
					vimage->buf[ofs] = COL(ptr[x + x + x], ptr[x + x + x + 1], ptr[x + x + x + 2]);
					ofs++;
				}
			}
		}
		free(ptr);
	}
	else if(cinfo.output_components == 1)
	{
		ofs = 0;
		for(i = 0; i < cinfo.rec_outbuf_height; i++)
		{
			if((line[i] = (unsigned char *)malloc(w)) == NULL)
			{
				int t = 0;

				for(t = 0; t < i; t++) free(line[t]);
				jpeg_destroy_decompress(&cinfo);
				image_free(vimage);
				if( is_file )
					fclose(file);
				return NULL;
			}
		}
		for(y = 0; y < h; y += cinfo.rec_outbuf_height)
		{
			jpeg_read_scanlines(&cinfo, line, cinfo.rec_outbuf_height);
			for(i = 0; i < cinfo.rec_outbuf_height; i++)
			{
				for(x = 0; x < w; x++)
				{
					vimage->buf[ofs++] = COL(line[i][x], line[i][x], line[i][x]);
				}
			}
		}
		for(i = 0; i < cinfo.rec_outbuf_height; i++)
		{
			 free(line[i]);
		}
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	if( is_file )
		fclose(file);

	return vimage;
}

void
image_resize_nearest(image * pdest, image * psrc, int32_t width, int32_t height)
{
	int32_t vx, vy, rx, ry;
	pix vcol;

	if((pdest == NULL) || (psrc == NULL))
		return;

	for(vy = 0; vy < height; vy++)
	{
		for(vx = 0; vx < width; vx++)
		{
			rx = ((vx * psrc->width) / width);
			ry = ((vy * psrc->height) / height);
			vcol = get_pix(psrc, rx, ry);
			put_pix_alpha_replace(pdest, vx, vy, vcol);
		}
	}
}

void
image_downsize_rought(image * pdest, image * psrc, int32_t width, int32_t height)
{
	int32_t vx, vy;
	pix vcol;
	int32_t i, j;
	int32_t rx, ry, rx_next, ry_next;
	int red, green, blue, alpha;
	int factor;

	if((pdest == NULL) || (psrc == NULL))
		return;

	for(vy = 0; vy < height; vy++)
	{
		for(vx = 0; vx < width; vx++)
		{

			rx = ((vx * psrc->width) / width);
			ry = ((vy * psrc->height) / height);

			red = green = blue = alpha = 0;

			rx_next = rx + (psrc->width / width);
			ry_next = ry + (psrc->width / width);
			factor = 0;

			for( j = rx; j < rx_next; j++)
			{
				for( i = ry; i < ry_next; i++)
				{
					factor += 1;
					vcol = get_pix(psrc, j, i);

					red   += COL_RED(vcol);
					green += COL_GREEN(vcol);
					blue  += COL_BLUE(vcol);
					alpha += COL_ALPHA(vcol);
				}
			}

			red   /= factor;
			green /= factor;
			blue  /= factor;
			alpha /= factor;

			/* on sature les valeurs */
			red   = (red   > 255) ? 255 : ((red   < 0) ? 0 : red  );
			green = (green > 255) ? 255 : ((green < 0) ? 0 : green);
			blue  = (blue  > 255) ? 255 : ((blue  < 0) ? 0 : blue );
			alpha = (alpha > 255) ? 255 : ((alpha < 0) ? 0 : alpha);

			put_pix_alpha_replace(pdest, vx, vy,
					      COL_FULL((u_int8_t)red, (u_int8_t)green, (u_int8_t)blue, (u_int8_t)alpha));
		}
	}
}

image *
image_resize(image * src_image, int32_t width, int32_t height)
{
	image * dst_image;

	dst_image = image_new(width, height);
	if( !dst_image )
		return NULL;
	if( (src_image->width < width) || (src_image->height < height) )
		image_resize_nearest(dst_image, src_image, width, height);
	else
		image_downsize_rought(dst_image, src_image, width, height);

	return dst_image;
}


unsigned char *
image_save_to_jpeg_buf(image * pimage, int * size)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];
	int row_stride;
	char *data;
	int i, x;
	struct my_dst_mgr dst;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_memory_dest(&cinfo, &dst);
	cinfo.image_width = pimage->width;
	cinfo.image_height = pimage->height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, JPEG_QUALITY, TRUE);
	jpeg_start_compress(&cinfo, TRUE);
	row_stride = cinfo.image_width * 3;
	if((data = malloc(row_stride)) == NULL)
	{
		DPRINTF(E_WARN, L_METADATA, "malloc failed\n");
		return NULL;
	}
	i = 0;
	while(cinfo.next_scanline < cinfo.image_height)
	{
		for(x = 0; x < pimage->width; x++)
		{
			data[x + x + x]   = COL_RED(pimage->buf[i]);
			data[x + x + x + 1] = COL_GREEN(pimage->buf[i]);
			data[x + x + x + 2] = COL_BLUE(pimage->buf[i]);
			i++;
		}
		row_pointer[0] = (unsigned char *)data;
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
	jpeg_finish_compress(&cinfo);
	*size = dst.used;
	free(data);
	jpeg_destroy_compress(&cinfo);

	return dst.buf;
}

int
image_save_to_jpeg_file(image * pimage, const char * path)
{
	int nwritten, size = 0;
	unsigned char * buf;
	FILE * dst_file;

	buf = image_save_to_jpeg_buf(pimage, &size);
	if( !buf )
		return -1;
 	dst_file = fopen(path, "w");
	if( !dst_file )
	{
		free(buf);
		return -1;
	}
	nwritten = fwrite(buf, 1, size, dst_file);
	fclose(dst_file);
	free(buf);

	return (nwritten==size ? 0 : 1);
}
