/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <aaruformat/consts.h>
#include <aaruformat/structs/data.h>
#include <aaruformat/structs/header.h>
#include <aaruformat/structs/index.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "benchmark.h"

// Open an Aaru format image and read its index
int open_image(const char *path, image_info *info)
{
    memset(info, 0, sizeof(image_info));

    info->file = fopen(path, "rb");
    if(info->file == NULL)
    {
        fprintf(stderr, "Error: Cannot open file %s\n", path);
        return -1;
    }

    // Read header
    AaruHeaderV2 header;
    if(fread(&header, 1, sizeof(AaruHeaderV2), info->file) != sizeof(AaruHeaderV2))
    {
        fprintf(stderr, "Error: Cannot read header\n");
        fclose(info->file);
        return -1;
    }

    // Check magic
    if(header.identifier != AARU_MAGIC)
    {
        fprintf(stderr, "Error: Invalid Aaru format magic\n");
        fclose(info->file);
        return -1;
    }

    info->major_version = header.imageMajorVersion;
    info->minor_version = header.imageMinorVersion;
    info->index_offset  = header.indexOffset;

    if(info->index_offset == 0)
    {
        fprintf(stderr, "Error: No index in image\n");
        fclose(info->file);
        return -1;
    }

    // Seek to index
    if(fseek(info->file, info->index_offset, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot seek to index\n");
        fclose(info->file);
        return -1;
    }

    // Read index header - check which version
    uint32_t index_identifier;
    if(fread(&index_identifier, 1, sizeof(uint32_t), info->file) != sizeof(uint32_t))
    {
        fprintf(stderr, "Error: Cannot read index identifier\n");
        fclose(info->file);
        return -1;
    }

    // Rewind to read full header
    if(fseek(info->file, info->index_offset, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot rewind to index\n");
        fclose(info->file);
        return -1;
    }

    uint64_t entries_count = 0;

    if(index_identifier == 0x32584449)  // IndexBlock2
    {
        IndexHeader2 index_header;
        if(fread(&index_header, 1, sizeof(IndexHeader2), info->file) != sizeof(IndexHeader2))
        {
            fprintf(stderr, "Error: Cannot read IndexHeader2\n");
            fclose(info->file);
            return -1;
        }
        entries_count = index_header.entries;
    }
    else if(index_identifier == 0x33584449)  // IndexBlock3
    {
        IndexHeader3 index_header;
        if(fread(&index_header, 1, sizeof(IndexHeader3), info->file) != sizeof(IndexHeader3))
        {
            fprintf(stderr, "Error: Cannot read IndexHeader3\n");
            fclose(info->file);
            return -1;
        }
        entries_count = index_header.entries;

        // TODO: If we need to handle chained indexes (previous field), we would do it here
        // For now, we just read the main index
    }
    else
    {
        fprintf(stderr, "Error: Unsupported index version (identifier: 0x%08X)\n", index_identifier);
        fclose(info->file);
        return -1;
    }

    info->block_count = entries_count;

    // Allocate and read index entries
    const size_t entries_size = info->block_count * sizeof(IndexEntry);
    info->index_entries       = malloc(entries_size);
    if(info->index_entries == NULL)
    {
        fprintf(stderr, "Error: Cannot allocate memory for index entries\n");
        fclose(info->file);
        return -1;
    }

    if(fread(info->index_entries, 1, entries_size, info->file) != entries_size)
    {
        fprintf(stderr, "Error: Cannot read index entries\n");
        free(info->index_entries);
        fclose(info->file);
        return -1;
    }

    // Calculate total uncompressed size by scanning data blocks
    info->total_uncompressed_size = 0;
    IndexEntry *entries           = (IndexEntry *)info->index_entries;

    for(uint64_t i = 0; i < info->block_count; i++)
    {
        if(entries[i].blockType == 0x4B4C4244)  // DataBlock
        {
            // Seek to block and read header
            if(fseek(info->file, entries[i].offset, SEEK_SET) != 0) continue;

            BlockHeader block_header;
            if(fread(&block_header, 1, sizeof(BlockHeader), info->file) != sizeof(BlockHeader)) continue;

            info->total_uncompressed_size += block_header.length;
        }
    }

    return 0;
}

// Close image
void close_image(image_info *info)
{
    if(info->file != NULL)
    {
        fclose(info->file);
        info->file = NULL;
    }

    if(info->index_entries != NULL)
    {
        free(info->index_entries);
        info->index_entries = NULL;
    }
}
