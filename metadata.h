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

typedef struct metadata_s {
	char *title;
	char *artist;
	char *album;
	char *genre;
	char *comment;
	char *channels;
	char *bitrate;
	char *frequency;
	char *bps;
	char *resolution;
	char *duration;
	char *mime;
	char *dlna_pn;
} metadata_t;

int
ends_with(const char * haystack, const char * needle);

char *
modifyString(char * string, const char * before, const char * after, short like);

sqlite_int64
GetFolderMetadata(const char * name, const char * artist);

sqlite_int64
GetAudioMetadata(const char * path, char * name);

sqlite_int64
GetImageMetadata(const char * path, char * name);

sqlite_int64
GetVideoMetadata(const char * path, char * name);

#endif
