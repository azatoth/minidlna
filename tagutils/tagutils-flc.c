//=========================================================================
// FILENAME	: tagutils-flc.c
// DESCRIPTION	: FLAC metadata reader
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

static int
_get_flctags(char *filename, struct song_metadata *psong)
{
	FLAC__Metadata_SimpleIterator *iterator = 0;
	FLAC__StreamMetadata *block;
	int block_number;
	int i;
	int err = 0;

	if(!(iterator = FLAC__metadata_simple_iterator_new()))
	{
		DPRINTF(E_FATAL, L_SCANNER, "Out of memory while FLAC__metadata_simple_iterator_new()\n");
		return -1;
	}

	block_number = 0;
	if(!FLAC__metadata_simple_iterator_init(iterator, filename, true, true))
	{
		DPRINTF(E_ERROR, L_SCANNER, "Cannot extract tag from %s\n", filename);
		return -1;
	}

	do {
		if(!(block = FLAC__metadata_simple_iterator_get_block(iterator)))
		{
			DPRINTF(E_ERROR, L_SCANNER, "Cannot extract tag from %s\n", filename);
			err = -1;
			goto _exit;
		}

		switch(block->type)
		{
		case FLAC__METADATA_TYPE_STREAMINFO:
			psong->samplerate = block->data.stream_info.sample_rate;
			psong->channels = block->data.stream_info.channels;
			break;

		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			for(i = 0; i < block->data.vorbis_comment.num_comments; i++)
			{
				vc_scan(psong,
					(char*)block->data.vorbis_comment.comments[i].entry,
					block->data.vorbis_comment.comments[i].length);
			}
			break;
		default:
			break;
		}
		FLAC__metadata_object_delete(block);
	}
	while(FLAC__metadata_simple_iterator_next(iterator));

 _exit:
	if(iterator)
		FLAC__metadata_simple_iterator_delete(iterator);

	return err;
}

static int
_get_flcfileinfo(char *filename, struct song_metadata *psong)
{
	psong->lossless = 1;
	psong->vbr_scale = 1;

	return 0;
}
