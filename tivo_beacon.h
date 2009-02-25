#include "config.h"
#ifdef ENABLE_TIVO
/*
 *  * A saved copy of a beacon from another tivo or another server
 *   */
struct aBeacon
{
   time_t               lastSeen;
   char*                machine;
   char*                identity;
   char*                platform;
   char*                swversion;
   char*                method;
   char*                services;
   struct aBeacon*      next;
};


uint32_t getBcastAddress( void );

int
OpenAndConfTivoBeaconSocket();

void
sendBeaconMessage(int fd, struct sockaddr_in * client, int len, int broadcast);
#endif
