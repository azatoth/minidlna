/* Media file scanner
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __SCANNER_H__
#define __SCANNER_H__

#define BROWSEDIR_ID	"64"
#define MUSIC_DIR_ID	"1$14"
#define VIDEO_DIR_ID	"2$15"
#define IMAGE_DIR_ID	"3$16"

sqlite_int64
get_next_available_id(const char * table, const char * parentID);

int
insert_directory(const char * name, const char * path, const char * base, const char * parentID, int objectID);

int
insert_file(char * name, const char * path, const char * parentID, int object);

int
CreateDatabase(void);

void
ScanDirectory(const char * dir, const char * parent, enum media_types type);

#endif
