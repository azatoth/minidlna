/* Album art extraction, caching, and scaling
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __ALBUMART_H__
#define __ALBUMART_H__

void
update_if_album_art(const char * path);

sqlite_int64
find_album_art(const char * path, const char * image_data, int image_size);

#endif
