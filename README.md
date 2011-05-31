# MiniDLNA

MiniDLNA is an DLNA server with the aim of being fully compilant with DLAN/UPnP-AV clients.

It's been developed by Justin Maggard at http://sourceforge.net/projects/minidlna.

This fork contains a more user friendly build system, and might have some patches applied that upstream doesn't, and offcourse it's git and not the awful CVS used on sourceforge.

# Installation

## Prerequisites 

Following dependices are required (will be checked upon by the build system):

* libexif
* libjpeg
* libid3tag
* libFLAC
* libvorbis
* sqlite3
* libavformat (the ffmpeg libraries)
* libuuid

You also need following programs installed to be able to build:

* scons
* pkg-config

## Build procedures

To build, simply type `scons`. After build is completed, either copy the resulting binary to a bin directory, or run `sudo scons install`

There is an `minidlna.conf` available that can be used directlty.
