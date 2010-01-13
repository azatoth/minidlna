/* Utility functions
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008-2009 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __UTILS_H__
#define __UTILS_H__

int
ends_with(const char * haystack, const char * needle);

char *
trim(char *str);

char *
strstrc(const char *s, const char *p, const char t);

char *
modifyString(char * string, const char * before, const char * after, short like);

char *
escape_tag(const char *tag);

void
strip_ext(char * name);

int
make_dir(char * path, mode_t mode);

int
is_video(const char * file);

int
is_audio(const char * file);

int
is_image(const char * file);

int
is_playlist(const char * file);

int
resolve_unknown_type(const char * path, enum media_types dir_type);

#endif
