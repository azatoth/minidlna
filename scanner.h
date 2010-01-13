/* Media file scanner
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008-2009 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __SCANNER_H__
#define __SCANNER_H__

#define BROWSEDIR_ID	"64"
#define MUSIC_DIR_ID	"1$14"
#define MUSIC_PLIST_ID	"1$F"
#define VIDEO_DIR_ID	"2$15"
#define IMAGE_DIR_ID	"3$16"

extern int valid_cache;

int
is_video(const char * file);

int
is_audio(const char * file);

int
is_image(const char * file);

sqlite_int64
get_next_available_id(const char * table, const char * parentID);

int
insert_directory(const char * name, const char * path, const char * base, const char * parentID, int objectID);

int
insert_file(char * name, const char * path, const char * parentID, int object);

int
CreateDatabase(void);

void
start_scanner();

#endif
