#include "config.h"
#ifdef TIVO_SUPPORT
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* This function based on byRequest */
char *
decodeString(char * string, int inplace)
{
	if( !string )
		return NULL;
	int alloc = (int)strlen(string)+1;
	char *ns;
	unsigned char in;
	int strindex=0;
	long hex;

	if( !inplace )
	{
		if( !(ns = malloc(alloc)) )
			return NULL;
	}

	while(--alloc > 0)
	{
		in = *string;
		if((in == '%') && isxdigit(string[1]) && isxdigit(string[2]))
		{
			/* this is two hexadecimal digits following a '%' */
			char hexstr[3];
			char *ptr;
			hexstr[0] = string[1];
			hexstr[1] = string[2];
			hexstr[2] = 0;

			hex = strtol(hexstr, &ptr, 16);

			in = (unsigned char)hex; /* this long is never bigger than 255 anyway */
			if( inplace )
			{
				*string = in;
				memmove(string+1, string+3, alloc-2);
			}
			else
			{
				string+=2;
			}
			alloc-=2;
		}
		if( !inplace )
			ns[strindex++] = in;
		string++;
	}
	if( inplace )
	{
		return string;
	}
	else
	{
		ns[strindex] = '\0'; /* terminate it */
		return ns;
	}
}
#endif
