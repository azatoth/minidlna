/* Image manipulation functions
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2009 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#include <sys/types.h>

typedef u_int32_t pix;

typedef struct {
	int32_t width;
	int32_t height;
	pix     *buf;
} image;

void
image_free(image *pimage);

int
image_get_jpeg_date_xmp(const char * path, char ** date);

int
image_get_jpeg_resolution(const char * path, int * width, int * height);

image *
image_new_from_jpeg(const char * path, int is_file, const char * ptr, int size);

image *
image_resize(image * src_image, int32_t width, int32_t height);

unsigned char *
image_save_to_jpeg_buf(image * pimage, int * size);

int
image_save_to_jpeg_file(image * pimage, const char * path);
