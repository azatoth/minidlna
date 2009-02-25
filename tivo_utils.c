#include "config.h"
#ifdef ENABLE_TIVO
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* This function stolen from byRequest */
char *
decodeString(const char * string)
{
	if( !string )
		return NULL;

	int alloc = (int)strlen(string)+1;
	char *ns = malloc(alloc);
	unsigned char in;
	int strindex=0;
	long hex;

	if( !ns )
		return NULL;

	while(--alloc > 0)
	{
		in = *string;
		if(('%' == in) && isxdigit(string[1]) && isxdigit(string[2]))
		{
			/* this is two hexadecimal digits following a '%' */
			char hexstr[3];
			char *ptr;
			hexstr[0] = string[1];
			hexstr[1] = string[2];
			hexstr[2] = 0;

			hex = strtol(hexstr, &ptr, 16);

			in = (unsigned char)hex; /* this long is never bigger than 255 anyway */
			string+=2;
			alloc-=2;
		}

		ns[strindex++] = in;
		string++;
	}
	ns[strindex]=0; /* terminate it */
	return ns;
}
#endif
