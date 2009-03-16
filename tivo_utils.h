#include "config.h"
#ifdef TIVO_SUPPORT
#include <sqlite3.h>

struct sqlite3PrngType {
  unsigned char isInit;          /* True if initialized */
  unsigned char i, j;            /* State variables */
  unsigned char s[256];          /* State variables */
} sqlite3Prng;

char *
decodeString(char * string, int inplace);

void
TiVoRandomSeedFunc(sqlite3_context *context, int argc, sqlite3_value **argv);
#endif
