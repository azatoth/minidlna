/* Metadata extraction
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __METADATA_H__
#define __METADATA_H__

char *
modifyString(char * string, const char * before, const char * after, short like);

sqlite_int64
GetAudioMetadata(const char * path, char * name);

sqlite_int64
GetImageMetadata(const char * path, char * name);

sqlite_int64
GetVideoMetadata(const char * path, char * name);

#endif
