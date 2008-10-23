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
#define MUSIC_DIR_ID	"1$20"
#define VIDEO_DIR_ID	"2$21"
#define IMAGE_DIR_ID	"3$22"

void
ScanDirectory(const char * dir, const char * parent);

#endif
