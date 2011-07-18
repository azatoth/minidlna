/* MiniDLNA project
 *
 * http://sourceforge.net/projects/minidlna/
 *
 * MiniDLNA media server
 * Copyright (C) 2008-2009  Justin Maggard
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 *
 * Portions of the code from the MiniUPnP project:
 *
 * Copyright (c) 2006-2007, Thomas Bernard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of the author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __UPNPGLOBALVARS_H__
#define __UPNPGLOBALVARS_H__

#include <time.h>

#include "minidlnatypes.h"
#include "config.h"

#include <sqlite3.h>

#define MINIDLNA_VERSION "1.0.21"

#ifdef NETGEAR
# define SERVER_NAME "ReadyDLNA"
#else
# define SERVER_NAME "MiniDLNA"
#endif

#define CLIENT_CACHE_SLOTS 20
#define USE_FORK 1
#define DB_VERSION 5

#ifdef ENABLE_NLS
#define _(string) gettext(string)
#else
#define _(string) (string)
#endif

#ifndef PNPX
#define PNPX 0
#endif

#define RESOURCE_PROTOCOL_INFO_VALUES \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN," \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_HD_50_AC3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_HD_60_AC3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_HP_HD_AC3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_HD_AAC_MULT5_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_HD_AC3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_HD_MPEG1_L3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_SD_AAC_MULT5_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_SD_AC3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_SD_MPEG1_L3_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_PAL;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_NA_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_NA_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_EU_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mpeg:DLNA.ORG_PN=MPEG1;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_AAC_MULT5;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_AC3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF15_AAC_520;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF30_AAC_940;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L31_HD_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L32_HD_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L3L_SD_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_HP_HD_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_HD_1080i_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_HD_720p_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_ASP_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_SP_VGA_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_50_AC3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_50_AC3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_60_AC3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_60_AC3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HP_HD_AC3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AAC_MULT5;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AAC_MULT5_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AC3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AC3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_MPEG1_L3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_MPEG1_L3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AAC_MULT5;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AAC_MULT5_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AC3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AC3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_MPEG1_L3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_MPEG1_L3_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_EU;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_EU_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA_T;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVSPLL_BASE;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVSPML_BASE;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVSPML_MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVMED_BASE;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVMED_FULL;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVMED_PRO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVHIGH_FULL;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVHIGH_PRO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/3gpp:DLNA.ORG_PN=MPEG4_P2_3GPP_SP_L0B_AAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:video/3gpp:DLNA.ORG_PN=MPEG4_P2_3GPP_SP_L0B_AMR;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMABASE;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMAFULL;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMAPRO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMALSL;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMALSL_MULT5;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/mp4:DLNA.ORG_PN=AAC_ISO_320;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/3gpp:DLNA.ORG_PN=AAC_ISO_320;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/mp4:DLNA.ORG_PN=AAC_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/mp4:DLNA.ORG_PN=AAC_MULT5_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM;DLNA.ORG_OP=01;DLNA.ORG_CI=0," \
	"http-get:*:image/jpeg:*," \
	"http-get:*:video/avi:*," \
	"http-get:*:video/divx:*," \
	"http-get:*:video/x-matroska:*," \
	"http-get:*:video/mpeg:*," \
	"http-get:*:video/mp4:*," \
	"http-get:*:video/x-ms-wmv:*," \
	"http-get:*:video/x-msvideo:*," \
	"http-get:*:video/x-flv:*," \
	"http-get:*:video/x-tivo-mpeg:*," \
	"http-get:*:video/quicktime:*," \
	"http-get:*:audio/mp4:*," \
	"http-get:*:audio/x-wav:*," \
	"http-get:*:audio/x-flac:*," \
	"http-get:*:application/ogg:*"

/* statup time */
extern time_t startup_time;

extern struct runtime_vars_s runtime_vars;
/* runtime boolean flags */
extern int runtime_flags;
#define INOTIFY_MASK          0x0001
#define TIVO_MASK             0x0002
#define DLNA_STRICT_MASK      0x0004

#define SETFLAG(mask)	runtime_flags |= mask
#define GETFLAG(mask)	runtime_flags & mask
#define CLEARFLAG(mask)	runtime_flags &= ~mask

extern const char * pidfilename;

extern char uuidvalue[];

#define MODELNAME_MAX_LEN (64)
extern char modelname[];

#define MODELNUMBER_MAX_LEN (16)
extern char modelnumber[];

#define SERIALNUMBER_MAX_LEN (16)
extern char serialnumber[];

#define PRESENTATIONURL_MAX_LEN (64)
extern char presentationurl[];

#if PNPX
extern char pnpx_hwid[];
#endif

/* lan addresses */
/* MAX_LAN_ADDR : maximum number of interfaces
 * to listen to SSDP traffic */
#define MAX_LAN_ADDR (4)
extern int n_lan_addr;
extern struct lan_addr_s lan_addr[];

/* UPnP-A/V [DLNA] */
extern sqlite3 *db;
extern char dlna_no_conv[];
#define FRIENDLYNAME_MAX_LEN (64)
extern char friendly_name[];
extern char db_path[];
extern char log_path[];
extern struct media_dir_s * media_dirs;
extern struct album_art_name_s * album_art_names;
extern struct client_cache_s clients[CLIENT_CACHE_SLOTS];
extern short int scanning;
extern volatile short int quitting;
extern volatile uint32_t updateID;

#endif
