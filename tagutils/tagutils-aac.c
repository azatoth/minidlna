//=========================================================================
// FILENAME	: tagutils-aac.c
// DESCRIPTION	: AAC metadata reader
//=========================================================================
// Copyright (c) 2008- NETGEAR, Inc. All Rights Reserved.
//=========================================================================

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This file is derived from mt-daap project.
 */

// _mac_to_unix_time
static time_t
_mac_to_unix_time(int t)
{
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv, &tz);

	return (t - (365L * 66L * 24L * 60L * 60L + 17L * 60L * 60L * 24L) +
		(tz.tz_minuteswest * 60));
}



// _aac_findatom:
static long
_aac_findatom(FILE *fin, long max_offset, char *which_atom, int *atom_size)
{
	long current_offset = 0;
	int size;
	char atom[4];

	while(current_offset < max_offset)
	{
		if(fread((void*)&size, 1, sizeof(int), fin) != sizeof(int))
			return -1;

		size = ntohl(size);

		if(size <= 7)
			return -1;

		if(fread(atom, 1, 4, fin) != 4)
			return -1;

		if(strncasecmp(atom, which_atom, 4) == 0)
		{
			*atom_size = size;
			return current_offset;
		}

		fseek(fin, size - 8, SEEK_CUR);
		current_offset += size;
	}

	return -1;
}

// _get_aactags
static int
_get_aactags(char *file, struct song_metadata *psong)
{
	FILE *fin;
	long atom_offset;
	unsigned int atom_length;

	long current_offset = 0;
	int current_size;
	char current_atom[4];
	char *current_data;
	int genre;
	int len;

	if(!(fin = fopen(file, "rb")))
	{
		DPRINTF(E_ERROR, L_SCANNER, "Cannot open file %s for reading\n", file);
		return -1;
	}

	fseek(fin, 0, SEEK_SET);

	atom_offset = _aac_lookforatom(fin, "moov:udta:meta:ilst", &atom_length);
	if(atom_offset != -1)
	{
		while(current_offset < atom_length)
		{
			if(fread((void*)&current_size, 1, sizeof(int), fin) != sizeof(int))
				break;

			current_size = ntohl(current_size);

			if(current_size <= 7)  // something not right
				break;

			if(fread(current_atom, 1, 4, fin) != 4)
				break;

			len = current_size - 7; // too short
			if(len < 22)
				len = 22;

			current_data = (char*)malloc(len); // extra byte
			memset(current_data, 0x00, len);

			if(fread(current_data, 1, current_size - 8, fin) != current_size - 8)
				break;

			if(!memcmp(current_atom, "\xA9" "nam", 4))
				psong->title = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "ART", 4) ||
				!memcmp(current_atom, "\xA9" "art", 4))
				psong->contributor[ROLE_ARTIST] = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "alb", 4))
				psong->album = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "cmt", 4))
				psong->comment = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "dir", 4))
				psong->contributor[ROLE_CONDUCTOR] = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "wrt", 4))
				psong->contributor[ROLE_COMPOSER] = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "grp", 4))
				psong->grouping = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "gen", 4))
				psong->genre = strdup((char*)&current_data[16]);
			else if(!memcmp(current_atom, "\xA9" "day", 4))
				psong->year = atoi((char*)&current_data[16]);
			else if(!memcmp(current_atom, "tmpo", 4))
				psong->bpm = (current_data[16] << 8) | current_data[17];
			else if(!memcmp(current_atom, "trkn", 4))
			{
				psong->track = (current_data[18] << 8) | current_data[19];
				psong->total_tracks = (current_data[20] << 8) | current_data[21];
			}
			else if(!memcmp(current_atom, "disk", 4))
			{
				psong->disc = (current_data[18] << 8) | current_data[19];
				psong->total_discs = (current_data[20] << 8) | current_data[21];
			}
			else if(!memcmp(current_atom, "gnre", 4))
			{
				genre = current_data[17] - 1;
				if((genre < 0) || (genre > WINAMP_GENRE_UNKNOWN))
					genre = WINAMP_GENRE_UNKNOWN;
				psong->genre = strdup(winamp_genre[genre]);
			}
			else if(!memcmp(current_atom, "cpil", 4))
			{
				psong->compilation = current_data[16];
			}
			else if(!memcmp(current_atom, "covr", 4))
			{
				psong->image_size = current_size - 8 - 16;
				if((psong->image = malloc(psong->image_size)))
					memcpy(psong->image, current_data+16, psong->image_size);
				else
					DPRINTF(E_ERROR, L_SCANNER, "Out of memory [%s]\n", file);
			}

			free(current_data);
			current_offset += current_size;
		}
	}

	fclose(fin);

	if(atom_offset == -1)
		return -1;

	return 0;
}

// aac_lookforatom
static off_t
_aac_lookforatom(FILE *aac_fp, char *atom_path, unsigned int *atom_length)
{
	long atom_offset;
	off_t file_size;
	char *cur_p, *end_p;
	char atom_name[5];

	fseek(aac_fp, 0, SEEK_END);
	file_size = ftell(aac_fp);
	rewind(aac_fp);

	end_p = atom_path;
	while(*end_p != '\0')
	{
		end_p++;
	}
	atom_name[4] = '\0';
	cur_p = atom_path;

	while(cur_p)
	{
		if((end_p - cur_p) < 4)
		{
			return -1;
		}
		strncpy(atom_name, cur_p, 4);
		atom_offset = _aac_findatom(aac_fp, file_size, atom_name, (int*)atom_length);
		if(atom_offset == -1)
		{
			return -1;
		}
		cur_p = strchr(cur_p, ':');
		if(cur_p != NULL)
		{
			cur_p++;

			if(!strcmp(atom_name, "meta"))
			{
				fseek(aac_fp, 4, SEEK_CUR);
			}
			else if(!strcmp(atom_name, "stsd"))
			{
				fseek(aac_fp, 8, SEEK_CUR);
			}
			else if(!strcmp(atom_name, "mp4a"))
			{
				fseek(aac_fp, 28, SEEK_CUR);
			}
		}
	}

	// return position of 'size:atom'
	return ftell(aac_fp) - 8;
}

// _get_aacfileinfo
int
_get_aacfileinfo(char *file, struct song_metadata *psong)
{
	FILE *infile;
	long atom_offset;
	int atom_length;
	int sample_size;
	int samples;
	unsigned int bitrate;
	off_t file_size;
	int ms;
	unsigned char buffer[2];
	int time = 0;
	aac_object_type_t profile_id = 0;

	psong->vbr_scale = -1;
	psong->channels = 2; // A "normal" default in case we can't find this information

	if(!(infile = fopen(file, "rb")))
	{
		DPRINTF(E_ERROR, L_SCANNER, "Could not open %s for reading\n", file);
		return -1;
	}

	fseek(infile, 0, SEEK_END);
	file_size = ftell(infile);
	fseek(infile, 0, SEEK_SET);

	// move to 'mvhd' atom
	atom_offset = _aac_lookforatom(infile, "moov:mvhd", (unsigned int*)&atom_length);
	if(atom_offset != -1)
	{
		fseek(infile, 8, SEEK_CUR);
		fread((void *)&time, sizeof(int), 1, infile);
		time = ntohl(time);
		// slimserver prefer to use filesystem time
		//psong->time_modified = _mac_to_unix_time(time);
		fread((void*)&sample_size, 1, sizeof(int), infile);
		fread((void*)&samples, 1, sizeof(int), infile);

		sample_size = ntohl(sample_size);
		samples = ntohl(samples);

		// avoid overflowing on large sample_sizes (90000)
		ms = 1000;
		while((ms > 9) && (!(sample_size % 10)))
		{
			sample_size /= 10;
			ms /= 10;
		}

		// unit = ms
		psong->song_length = (int)((samples * ms) / sample_size);
	}

	psong->bitrate = 0;

	// get samplerate from 'mp4a' (not from 'mdhd')
	atom_offset = _aac_lookforatom(infile, "moov:trak:mdia:minf:stbl:stsd:mp4a", (unsigned int*)&atom_length);
	if(atom_offset != -1)
	{
		fseek(infile, atom_offset + 32, SEEK_SET);

		fread(buffer, sizeof(unsigned char), 2, infile);

		psong->samplerate = (buffer[0] << 8) | (buffer[1]);

		fseek(infile, 2, SEEK_CUR);

		// get bitrate fomr 'esds'
		atom_offset = _aac_findatom(infile, atom_length - (ftell(infile) - atom_offset), "esds", &atom_length);

		if(atom_offset != -1)
		{
			fseek(infile, atom_offset + 26, SEEK_CUR); // +22 is max bitrate, +26 is average bitrate
			fread((void *)&bitrate, sizeof(unsigned int), 1, infile);
			psong->bitrate = ntohl(bitrate);

			fseek(infile, 5, SEEK_CUR); // 5 bytes past bitrate is setup data
			fread((void *)&buffer, 2, 1, infile);
			profile_id = (buffer[0] >> 3); // first 5 bits of setup data is the Audo Profile ID
			/* Frequency index: (((buffer[0] & 0x7) << 1) | (buffer[1] >> 7))) */
			samples = ((buffer[1] >> 3) & 0xF);
			psong->channels = (samples == 7 ? 8 : samples);
		}
	}

	atom_offset = _aac_lookforatom(infile, "mdat", (unsigned int*)&atom_length);
	psong->audio_size = atom_length - 8;
	psong->audio_offset = atom_offset;

	if(!psong->bitrate)
	{
		DPRINTF(E_DEBUG, L_SCANNER, "No 'esds' atom. Guess bitrate.\n");
		if((atom_offset != -1) && (psong->song_length))
		{
			psong->bitrate = atom_length * 1000 / psong->song_length / 128;
		}
		/* If this is an obviously wrong bitrate, try something different */
		if((psong->bitrate < 16000) && (psong->song_length))
		{
			psong->bitrate = (file_size * 8) / (psong->song_length / 1000);
		}
	}

	//DPRINTF(E_DEBUG, L_METADATA, "Profile ID: %u\n", profile_id);
	switch( profile_id )
	{
		case AAC_LC:
		case AAC_LC_ER:
			if( psong->samplerate < 8000 || psong->samplerate > 48000 )
			{
				DPRINTF(E_DEBUG, L_METADATA, "Unsupported AAC: sample rate is not 8000 < %d < 48000\n",
				                              psong->samplerate);
				break;
			}
			/* AAC @ Level 1/2 */
			if( psong->channels <= 2 && psong->bitrate <= 320000 )
				asprintf(&(psong->dlna_pn), "AAC_ISO_320");
			else if( psong->channels <= 2 && psong->bitrate <= 576000 )
				asprintf(&(psong->dlna_pn), "AAC_ISO");
			else if( psong->channels <= 6 && psong->bitrate <= 1440000 )
				asprintf(&(psong->dlna_pn), "AAC_MULT5_ISO");
			else
				DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC: %d channels, %d bitrate\n",
				                             psong->channels, psong->bitrate);
			break;
		default:
			DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC type [%d]\n", profile_id);
			break;
	}

	fclose(infile);
	return 0;
}
