/* Utility functions
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __UTILS_H__
#define __UTILS_H__
#include <gd.h>

int
ends_with(const char * haystack, const char * needle);

char *
trim(char *str);

char *
modifyString(char * string, const char * before, const char * after, short like);

void
strip_ext(char * name);

int
make_dir(char * path, mode_t mode);

/* A GD resize function with a good balance of quality and speed */
void
boxfilter_resize(gdImagePtr dst, gdImagePtr src,
                 int dstX, int dstY, int srcX, int srcY,
                 int dstW, int dstH, int srcW, int srcH);
#endif
