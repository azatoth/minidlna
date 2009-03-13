/*  MiniDLNA media server
 *  Copyright (C) 2008  Justin Maggard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <gd.h>

int
ends_with(const char * haystack, const char * needle)
{
	const char *found = strcasestr(haystack, needle);
	return (found && found[strlen(needle)] == '\0');
}

char *
trim(char *str)
{
        if (!str)
                return(NULL);
        int i;
        for (i=0; i <= strlen(str) && (isspace(str[i]) || str[i] == '"'); i++) {
		str++;
	}
        for (i=(strlen(str)-1); i >= 0 && (isspace(str[i]) || str[i] == '"'); i--) {
                str[i] = '\0';
        }
        return str;
}

char *
modifyString(char * string, const char * before, const char * after, short like)
{
	int oldlen, newlen, chgcnt = 0;
	char *s, *p, *t;

	oldlen = strlen(before);
	newlen = strlen(after);
	if( newlen > oldlen )
	{
		s = string;
		while( (p = strstr(s, before)) )
		{
			chgcnt++;
			s = p+oldlen;
		}
		string = realloc(string, strlen(string)+((newlen-oldlen)*chgcnt)+1);
	}

	s = string;
	while( s )
	{
		p = strcasestr(s, before);
		if( !p )
			return string;
		if( like )
		{
			t = p+oldlen;
			while( isspace(*t) )
				t++;
			if( *t == '"' )
				while( *++t != '"' )
					continue;
			memmove(t+1, t, strlen(t)+1);
			*t = '%';
		}
		memmove(p + newlen, p + oldlen, strlen(p + oldlen) + 1);
		memcpy(p, after, newlen);
		s = p + newlen;
	}
	if( newlen < oldlen )
		string = realloc(string, strlen(string)+1);

	return string;
}

void
strip_ext(char * name)
{
	char * period;

	period = rindex(name, '.');
	if( period )
		*period = '\0';
}

/* Code basically stolen from busybox */
int
make_dir(char * path, mode_t mode)
{
	char * s = path;
	char c;
	struct stat st;

	do {
		c = 0;

		/* Bypass leading non-'/'s and then subsequent '/'s. */
		while (*s) {
			if (*s == '/') {
				do {
					++s;
				} while (*s == '/');
				c = *s;     /* Save the current char */
				*s = 0;     /* and replace it with nul. */
				break;
			}
			++s;
		}

		if (mkdir(path, mode) < 0) {
			/* If we failed for any other reason than the directory
			 * already exists, output a diagnostic and return -1.*/
			if (errno != EEXIST
			    || (stat(path, &st) < 0 || !S_ISDIR(st.st_mode))) {
				break;
			}
		}
	        if (!c)
			return 0;

		/* Remove any inserted nul from the path. */
		*s = c;

	} while (1);

	printf("make_dir: cannot create directory '%s'", path);
	return -1;
}

/* Use our own boxfilter resizer, because gdCopyImageResampled is slow,
 * and gdCopyImageResized looks horrible when you downscale much. */
#define N_FRAC 8
#define MASK_FRAC ((1 << N_FRAC) - 1)
#define ROUND2(v) (((v) + (1 << (N_FRAC - 1))) >> N_FRAC)
#define DIV(x, y) ( ((x) << (N_FRAC - 3)) / ((y) >> 3) )
void
boxfilter_resize(gdImagePtr dst, gdImagePtr src,
                 int dstX, int dstY, int srcX, int srcY,
                 int dstW, int dstH, int srcW, int srcH)
{
	int x, y;
	int sy1, sy2, sx1, sx2;

	if(!dst->trueColor)
	{
		gdImageCopyResized(dst, src, dstX, dstY, srcX, srcY, dstW, dstH,
				   srcW, srcH);
		return;
	}
	for(y = dstY; y < (dstY + dstH); y++)
	{
		sy1 = (((y - dstY) * srcH) << N_FRAC) / dstH;
		sy2 = (((y - dstY + 1) * srcH) << N_FRAC) / dstH;
		for(x = dstX; x < (dstX + dstW); x++)
		{
			int sx, sy;
			int spixels = 0;
			int red = 0, green = 0, blue = 0, alpha = 0;
			sx1 = (((x - dstX) * srcW) << N_FRAC) / dstW;
			sx2 = (((x - dstX + 1) * srcW) << N_FRAC) / dstW;
			sy = sy1;
			do {
				int yportion;
				if((sy >> N_FRAC) == (sy1 >> N_FRAC))
				{
					yportion = (1 << N_FRAC) - (sy & MASK_FRAC);
					if(yportion > sy2 - sy1)
					{
						yportion = sy2 - sy1;
					}
					sy = sy & ~MASK_FRAC;
				}
				else if(sy == (sy2 & ~MASK_FRAC))
				{
					yportion = sy2 & MASK_FRAC;
				}
				else
				{
					yportion = (1 << N_FRAC);
				}
				sx = sx1;
				do {
					int xportion;
					int pcontribution;
					int p;
					if((sx >> N_FRAC) == (sx1 >> N_FRAC))
					{
						xportion = (1 << N_FRAC) - (sx & MASK_FRAC);
						if(xportion > sx2 - sx1)
						{
							xportion = sx2 - sx1;
						}
						sx = sx & ~MASK_FRAC;
					}
					else if(sx == (sx2 & ~MASK_FRAC))
					{
						xportion = sx2 & MASK_FRAC;
					}
					else
					{
						xportion = (1 << N_FRAC);
					}

					if(xportion && yportion)
					{
						pcontribution = (xportion * yportion) >> N_FRAC;
						p = gdImageGetTrueColorPixel(src, ROUND2(sx) + srcX, ROUND2(sy) + srcY);
						if(pcontribution == (1 << N_FRAC))
						{
							// optimization for down-scaler, which many pixel has pcontribution=1
							red += gdTrueColorGetRed(p) << N_FRAC;
							green += gdTrueColorGetGreen(p) << N_FRAC;
							blue += gdTrueColorGetBlue(p) << N_FRAC;
							alpha += gdTrueColorGetAlpha(p) << N_FRAC;
							spixels += (1 << N_FRAC);
						}
						else
						{
							red += gdTrueColorGetRed(p) * pcontribution;
							green += gdTrueColorGetGreen(p) * pcontribution;
							blue += gdTrueColorGetBlue(p) * pcontribution;
							alpha += gdTrueColorGetAlpha(p) * pcontribution;
							spixels += pcontribution;
						}
					}
					sx += (1 << N_FRAC);
				}
				while(sx < sx2);
				sy += (1 << N_FRAC);
			}
			while(sy < sy2);
			if(spixels != 0)
			{
				red = DIV(red, spixels);
				green = DIV(green, spixels);
				blue = DIV(blue, spixels);
				alpha = DIV(alpha, spixels);
			}
			/* Clamping to allow for rounding errors above */
			if(red > (255 << N_FRAC))
				red = (255 << N_FRAC);
			if(green > (255 << N_FRAC))
				green = (255 << N_FRAC);
			if(blue > (255 << N_FRAC))
				blue = (255 << N_FRAC);
			if(alpha > (gdAlphaMax << N_FRAC))
				alpha = (gdAlphaMax << N_FRAC);
			gdImageSetPixel(dst, x, y,
					gdTrueColorAlpha(ROUND2(red), ROUND2(green), ROUND2(blue), ROUND2(alpha)));
		}
	}
}

