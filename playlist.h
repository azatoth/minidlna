/* Playlist handling
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008-2010 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __PLAYLIST_H__
#define __PLAYLIST_H__

int
insert_playlist(const char * path, char * name);

int
fill_playlists(void);

#endif // __PLAYLIST_H__
