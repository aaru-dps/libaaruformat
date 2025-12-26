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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>
#include <aaruformat/consts.h>
#include <aaruformat/enums.h>
#include <aaruformat/structs/ddt.h>
#include <aaruformat/structs/header.h>
#include <aaruformat/structs/index.h>

#include "aaru.h"
#include "aaruformattool.h"
#include "ddt_alpha20.h"
#include "uthash.h"

// Hash table structure for index entries
typedef struct
{
    uint64_t       offset;     // key - file offset of the block
    uint32_t       blockType;  // block type identifier
    uint16_t       dataType;   // data type identifier
    UT_hash_handle hh;         // makes this structure hashable
} IndexEntryHash;

int upgrade_ddt_to_alpha21(const char *path)
{
    printf("upgrade-ddt-to-alpha21 command called with path: %s\n", path);

    // Open the file
    FILE *fp = fopen(path, "rb");
    if(fp == NULL)
    {
        printf("ERROR: Failed to open file '%s': %s\n", path, strerror(errno));
        return errno;
    }

    // Read the initial header to check version
    AaruHeader initial_header;
    size_t     read_bytes = fread(&initial_header, 1, sizeof(AaruHeader), fp);

    if(read_bytes != sizeof(AaruHeader))
    {
        printf("ERROR: Failed to read header from file (read %zu bytes, expected %zu)\n", read_bytes,
               sizeof(AaruHeader));
        fclose(fp);
        return EIO;
    }

    // Check magic identifier
    if(initial_header.identifier != AARU_MAGIC && initial_header.identifier != DIC_MAGIC)
    {
        printf("ERROR: File is not an AaruFormat image (invalid magic identifier)\n");
        fclose(fp);
        return EINVAL;
    }

    printf("Detected AaruFormat image version %d.%d\n", initial_header.imageMajorVersion,
           initial_header.imageMinorVersion);

    // Check if it's a version 2 image
    if(initial_header.imageMajorVersion != AARUF_VERSION_V2)
    {
        printf("ERROR: This command only works with AaruFormat v2 images\n");
        printf("       Found version %d.%d, expected version 2.x\n", initial_header.imageMajorVersion,
               initial_header.imageMinorVersion);
        fclose(fp);
        return EINVAL;
    }

    // Read the full v2 header
    fseek(fp, 0, SEEK_SET);
    AaruHeaderV2 header;
    read_bytes = fread(&header, 1, sizeof(AaruHeaderV2), fp);

    if(read_bytes != sizeof(AaruHeaderV2))
    {
        printf("ERROR: Failed to read complete v2 header from file (read %zu bytes, expected %zu)\n", read_bytes,
               sizeof(AaruHeaderV2));
        fclose(fp);
        return EIO;
    }

    printf("Successfully loaded AaruFormat v2 header\n");
    printf("  Index offset: %llu bytes\n", (unsigned long long)header.indexOffset);
    printf("  Media type: %u (%s)\n", header.mediaType, media_type_to_string(header.mediaType));
    printf("  Block alignment shift: %u\n", header.blockAlignmentShift);
    printf("  Data shift: %u\n", header.dataShift);
    printf("  Table shift: %u\n", header.tableShift);

    // Display important warnings
    printf("\n");
    printf("================================================================================\n");
    printf("                           *** IMPORTANT WARNING ***\n");
    printf("================================================================================\n");
    printf("\n");
    printf("This command is designed ONLY for images created with:\n");
    printf("  - Aaru alpha 16 or earlier (libaaruformat alpha 20 or earlier)\n");
    printf("\n");
    printf("CRITICAL LIMITATIONS:\n");
    printf("\n");
    printf("  1. There is NO WAY to automatically determine if this image actually needs\n");
    printf("     an upgrade. The file format version cannot distinguish between images\n");
    printf("     created with Aaru alpha 16/libaaruformat alpha 20 and newer versions.\n");
    printf("\n");
    printf("  2. Attempting to upgrade images created with NEWER versions (Aaru alpha 17+\n");
    printf("     or libaaruformat alpha 21+) WILL CORRUPT THEM and make them UNREADABLE.\n");
    printf("\n");
    printf("  3. There is NO GUARANTEE of success. The upgrade process may fail or\n");
    printf("     produce an invalid image even with correct input.\n");
    printf("\n");
    printf("  4. This command is provided as a LAST RESORT ONLY.\n");
    printf("\n");
    printf("RECOMMENDED SOLUTION:\n");
    printf("\n");
    printf("  The IDEAL and SAFEST solution is to re-dump the media using a current\n");
    printf("  version of Aaru or libaaruformat. This ensures data integrity and full\n");
    printf("  compatibility with the latest format improvements.\n");
    printf("\n");
    printf("Before proceeding:\n");
    printf("  - Make sure you have a BACKUP of the original image file\n");
    printf("  - Verify the image was created with Aaru alpha 16/libaaruformat alpha 20\n");
    printf("    or earlier\n");
    printf("  - Consider re-dumping the media if possible\n");
    printf("\n");
    printf("================================================================================\n");
    printf("\n");

    // Compare structure sizes
    ddt_v2_header_alpha20 old_header;
    DdtHeader2            new_header;

    printf("Structure size comparison:\n");
    printf("  Alpha20 DDT v2 header: %zu bytes\n", sizeof(old_header));
    printf("  Current library DdtHeader2: %zu bytes\n", sizeof(new_header));

    if(sizeof(old_header) != sizeof(new_header)) { printf("  Structure sizes differ - upgrade needed!\n"); }
    else
    {
        printf("  Structure sizes are identical.\n");
    }

    // Ask for user confirmation
    printf("\n");
    printf("Do you want to proceed with the upgrade? (yes/no): ");
    fflush(stdout);

    char response[256];
    if(fgets(response, sizeof(response), stdin) == NULL)
    {
        printf("\nERROR: Failed to read user input\n");
        fclose(fp);
        return EIO;
    }

    // Trim newline
    size_t len = strlen(response);
    if(len > 0 && response[len - 1] == '\n') response[len - 1] = '\0';

    // Check response
    if(strcmp(response, "yes") != 0 && strcmp(response, "YES") != 0 && strcmp(response, "y") != 0 &&
       strcmp(response, "Y") != 0)
    {
        printf("\nUpgrade cancelled by user.\n");
        fclose(fp);
        return 0;
    }

    printf("\nProceeding with upgrade...\n");

    // Seek to index position
    if(fseek(fp, header.indexOffset, SEEK_SET) != 0)
    {
        printf("ERROR: Failed to seek to index offset %llu: %s\n", (unsigned long long)header.indexOffset,
               strerror(errno));
        fclose(fp);
        return EIO;
    }

    // Read index header (v3)
    IndexHeader3 index_header;
    read_bytes = fread(&index_header, 1, sizeof(IndexHeader3), fp);

    if(read_bytes != sizeof(IndexHeader3))
    {
        printf("ERROR: Failed to read index header (read %zu bytes, expected %zu)\n", read_bytes, sizeof(IndexHeader3));
        fclose(fp);
        return EIO;
    }

    // Verify it's an IndexBlock3
    if(index_header.identifier != IndexBlock3)
    {
        printf("ERROR: Expected IndexBlock3 identifier, found 0x%08X\n", index_header.identifier);
        fclose(fp);
        return EINVAL;
    }

    printf("Reading index with %llu entries...\n", (unsigned long long)index_header.entries);

    // Allocate memory for index entries
    IndexEntry *entries = (IndexEntry *)malloc(sizeof(IndexEntry) * index_header.entries);
    if(entries == NULL)
    {
        printf("ERROR: Failed to allocate memory for %llu index entries\n", (unsigned long long)index_header.entries);
        fclose(fp);
        return ENOMEM;
    }

    // Read all index entries
    read_bytes = fread(entries, sizeof(IndexEntry), index_header.entries, fp);
    if(read_bytes != index_header.entries)
    {
        printf("ERROR: Failed to read index entries (read %zu, expected %llu)\n", read_bytes,
               (unsigned long long)index_header.entries);
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Build hash table from index entries
    IndexEntryHash *index_hash = NULL;

    for(uint64_t i = 0; i < index_header.entries; i++)
    {
        IndexEntryHash *entry = (IndexEntryHash *)malloc(sizeof(IndexEntryHash));
        if(entry == NULL)
        {
            printf("ERROR: Failed to allocate memory for hash table entry %llu\n", (unsigned long long)i);
            // Clean up
            IndexEntryHash *current, *tmp;
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return ENOMEM;
        }

        entry->offset    = entries[i].offset;
        entry->blockType = entries[i].blockType;
        entry->dataType  = entries[i].dataType;

        HASH_ADD(hh, index_hash, offset, sizeof(uint64_t), entry);
    }

    printf("Successfully loaded %llu index entries into hash table\n", (unsigned long long)index_header.entries);

    // Count DDT blocks
    uint64_t        ddt_count = 0;
    IndexEntryHash *current, *tmp;
    HASH_ITER(hh, index_hash, current, tmp)
    {
        if(current->blockType == DeDuplicationTable2) { ddt_count++; }
    }

    printf("Found %llu DDT v2 blocks to upgrade\n", (unsigned long long)ddt_count);

    // Process DDT blocks - find and process the userdata DDT2 block
    ddt_v2_header_alpha20 *old_ddt_header      = NULL;
    uint8_t               *ddt_payload         = NULL;
    uint64_t               userdata_ddt_offset = 0;

    HASH_ITER(hh, index_hash, current, tmp)
    {
        if(current->blockType == DeDuplicationTable2)
        {
            // Check if this is the userdata DDT (others don't need upgrade)
            if(current->dataType == UserData)
            {
                printf("Processing userdata DDT v2 block at offset %llu...\n", (unsigned long long)current->offset);
                userdata_ddt_offset = current->offset;

                // Seek to the DDT block
                if(fseek(fp, current->offset, SEEK_SET) != 0)
                {
                    printf("ERROR: Failed to seek to DDT block at offset %llu: %s\n",
                           (unsigned long long)current->offset, strerror(errno));
                    free(entries);
                    fclose(fp);
                    return EIO;
                }

                // Allocate and read the old DDT header
                old_ddt_header = (ddt_v2_header_alpha20 *)malloc(sizeof(ddt_v2_header_alpha20));
                if(old_ddt_header == NULL)
                {
                    printf("ERROR: Failed to allocate memory for old DDT header\n");
                    free(entries);
                    fclose(fp);
                    return ENOMEM;
                }

                read_bytes = fread(old_ddt_header, 1, sizeof(ddt_v2_header_alpha20), fp);
                if(read_bytes != sizeof(ddt_v2_header_alpha20))
                {
                    printf("ERROR: Failed to read old DDT header (read %zu bytes, expected %zu)\n", read_bytes,
                           sizeof(ddt_v2_header_alpha20));
                    free(old_ddt_header);
                    free(entries);
                    fclose(fp);
                    return EIO;
                }

                printf("  Old DDT header read successfully\n");
                printf("    Identifier: 0x%08X\n", old_ddt_header->identifier);
                printf("    Type: %u\n", old_ddt_header->type);
                printf("    Compression: %u\n", old_ddt_header->compression);
                printf("    Entries: %llu\n", (unsigned long long)old_ddt_header->entries);
                printf("    Compressed length: %llu bytes\n", (unsigned long long)old_ddt_header->cmpLength);
                printf("    Uncompressed length: %llu bytes\n", (unsigned long long)old_ddt_header->length);

                // Allocate memory for the compressed DDT payload
                ddt_payload = (uint8_t *)malloc(old_ddt_header->cmpLength);
                if(ddt_payload == NULL)
                {
                    printf("ERROR: Failed to allocate %llu bytes for DDT payload\n",
                           (unsigned long long)old_ddt_header->cmpLength);
                    free(old_ddt_header);
                    free(entries);
                    fclose(fp);
                    return ENOMEM;
                }

                // Read the DDT payload
                read_bytes = fread(ddt_payload, 1, old_ddt_header->cmpLength, fp);
                if(read_bytes != old_ddt_header->cmpLength)
                {
                    printf("ERROR: Failed to read DDT payload (read %zu bytes, expected %llu)\n", read_bytes,
                           (unsigned long long)old_ddt_header->cmpLength);
                    free(ddt_payload);
                    free(old_ddt_header);
                    free(entries);
                    fclose(fp);
                    return EIO;
                }

                printf("  DDT payload read successfully (%llu bytes)\n", (unsigned long long)old_ddt_header->cmpLength);

                // Remove this entry from the index hash
                printf("  Removing userdata DDT entry from index...\n");
                HASH_DEL(index_hash, current);
                free(current);

                printf("  Userdata DDT block processed and removed from index\n");
                break;  // Only process the first userdata DDT found
            }
            else
            {
                printf("Skipping non-userdata DDT v2 block (dataType=%u) at offset %llu\n", current->dataType,
                       (unsigned long long)current->offset);
            }
        }
    }

    if(old_ddt_header == NULL)
    {
        printf("ERROR: No userdata DDT v2 block found in index\n");
        printf("       This image does not contain a userdata DDT block that needs upgrading.\n");

        // Clean up
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EINVAL;
    }

    printf("\n================================================================================\n");
    printf("                          DDT UPGRADE PROCESSING\n");
    printf("================================================================================\n\n");

    // Create new DDT header and copy fields from old one
    DdtHeader2 new_ddt_header;
    memset(&new_ddt_header, 0, sizeof(DdtHeader2));

    new_ddt_header.identifier          = old_ddt_header->identifier;
    new_ddt_header.type                = old_ddt_header->type;
    new_ddt_header.compression         = old_ddt_header->compression;
    new_ddt_header.levels              = old_ddt_header->levels;
    new_ddt_header.tableLevel          = old_ddt_header->tableLevel;
    new_ddt_header.previousLevelOffset = old_ddt_header->previousLevelOffset;
    new_ddt_header.negative            = old_ddt_header->negative;
    new_ddt_header.blocks              = old_ddt_header->blocks;
    new_ddt_header.overflow            = old_ddt_header->overflow;
    new_ddt_header.start               = old_ddt_header->start;
    new_ddt_header.blockAlignmentShift = old_ddt_header->blockAlignmentShift;
    new_ddt_header.dataShift           = old_ddt_header->dataShift;
    new_ddt_header.tableShift          = old_ddt_header->tableShift;
    new_ddt_header.entries             = old_ddt_header->entries;
    new_ddt_header.cmpLength           = old_ddt_header->cmpLength;
    new_ddt_header.length              = old_ddt_header->length;
    new_ddt_header.cmpCrc64            = old_ddt_header->cmpCrc64;
    new_ddt_header.crc64               = old_ddt_header->crc64;

    printf("Copied old DDT header fields to new header structure\n");

    // Check for DVD media types with specific negative/overflow configuration
    bool is_dvd_type = (header.mediaType == DVDROM || header.mediaType == DVDR || header.mediaType == DVDRW ||
                        header.mediaType == DVDPR || header.mediaType == DVDPRW || header.mediaType == DVDPRWDL ||
                        header.mediaType == DVDRDL || header.mediaType == DVDPRDL || header.mediaType == DVDRWDL ||
                        header.mediaType == DVDDownload || header.mediaType == PS2DVD || header.mediaType == PS3DVD ||
                        header.mediaType == Nuon);

    if(is_dvd_type && new_ddt_header.negative == 0 && new_ddt_header.overflow == 15000)
    {
        printf("\n*** INVALID BLOCK COUNT DETECTED ***\n");
        printf("Media type: %u (%s)\n", header.mediaType, media_type_to_string(header.mediaType));
        printf("Current values:\n");
        printf("  negative sectors: %u\n", new_ddt_header.negative);
        printf("  overflow sectors: %u\n", new_ddt_header.overflow);
        printf("  blocks: %llu\n", (unsigned long long)new_ddt_header.blocks);
        printf("\nThis configuration is invalid for DVD media.\n");
        printf("To fix: overflow should be 0, and blocks should be reduced by 0x30000 + 15000\n\n");
        printf("Do you want to fix the block count? (yes/no): ");
        fflush(stdout);

        char response[256];
        if(fgets(response, sizeof(response), stdin) == NULL)
        {
            printf("\nERROR: Failed to read user input\n");
            free(ddt_payload);
            free(old_ddt_header);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return EIO;
        }

        size_t len = strlen(response);
        if(len > 0 && response[len - 1] == '\n') response[len - 1] = '\0';

        if(strcmp(response, "yes") != 0 && strcmp(response, "YES") != 0 && strcmp(response, "y") != 0 &&
           strcmp(response, "Y") != 0)
        {
            printf("\nUser declined to fix block count. Upgrade cancelled.\n");
            free(ddt_payload);
            free(old_ddt_header);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return 0;
        }

        // Fix the block count
        new_ddt_header.overflow = 0;
        new_ddt_header.blocks   = new_ddt_header.blocks - 0x30000 - 15000;

        printf("\nFixed block count:\n");
        printf("  negative sectors: %u\n", new_ddt_header.negative);
        printf("  overflow sectors: %u\n", new_ddt_header.overflow);
        printf("  blocks: %llu\n\n", (unsigned long long)new_ddt_header.blocks);
    }

    // Check for DVD-RAM
    if(header.mediaType == DVDRAM)
    {
        printf("\nERROR: Cannot upgrade DVD-RAM media type\n");
        printf("       DVD-RAM images cannot be upgraded with this tool.\n");
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EINVAL;
    }

    // Check for negative=0 and overflow>0 (other than the DVD case handled above)
    if(new_ddt_header.negative == 0 && new_ddt_header.overflow > 0 && !is_dvd_type)
    {
        printf("\nERROR: Invalid sector configuration\n");
        printf("       negative sectors: %u\n", new_ddt_header.negative);
        printf("       overflow sectors: %u\n", new_ddt_header.overflow);
        printf("\n       When negative sectors is 0 and overflow sectors is > 0,\n");
        printf("       the image cannot be reliably upgraded.\n");
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EINVAL;
    }

    // Get current file size to determine where to write new DDT
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        printf("ERROR: Failed to seek to end of file: %s\n", strerror(errno));
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    int64_t file_size = ftell(fp);
    if(file_size < 0)
    {
        printf("ERROR: Failed to get file size: %s\n", strerror(errno));
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Calculate aligned position for new DDT block
    uint64_t alignment      = 1ULL << header.blockAlignmentShift;
    uint64_t new_ddt_offset = ((uint64_t)file_size + alignment - 1) & ~(alignment - 1);

    printf("Writing new DDT block:\n");
    printf("  Current file size: %lld bytes\n", (long long)file_size);
    printf("  Block alignment: %llu bytes (2^%u)\n", (unsigned long long)alignment, header.blockAlignmentShift);
    printf("  New DDT offset: %llu bytes\n", (unsigned long long)new_ddt_offset);

    // Reopen file in read-write mode for appending
    fclose(fp);
    fp = fopen(path, "r+b");
    if(fp == NULL)
    {
        printf("ERROR: Failed to reopen file in write mode: %s\n", strerror(errno));
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        return errno;
    }

    // Seek to aligned position
    if(fseek(fp, new_ddt_offset, SEEK_SET) != 0)
    {
        printf("ERROR: Failed to seek to new DDT position: %s\n", strerror(errno));
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Write new DDT header
    size_t written = fwrite(&new_ddt_header, 1, sizeof(DdtHeader2), fp);
    if(written != sizeof(DdtHeader2))
    {
        printf("ERROR: Failed to write new DDT header (wrote %zu bytes, expected %zu)\n", written, sizeof(DdtHeader2));
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    printf("  New DDT header written (%zu bytes)\n", written);

    // Write DDT payload (unchanged)
    written = fwrite(ddt_payload, 1, old_ddt_header->cmpLength, fp);
    if(written != old_ddt_header->cmpLength)
    {
        printf("ERROR: Failed to write DDT payload (wrote %zu bytes, expected %llu)\n", written,
               (unsigned long long)old_ddt_header->cmpLength);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    printf("  DDT payload written (%zu bytes)\n", written);

    // Add new DDT entry to index hash
    IndexEntryHash *new_entry = (IndexEntryHash *)malloc(sizeof(IndexEntryHash));
    if(new_entry == NULL)
    {
        printf("ERROR: Failed to allocate memory for new index entry\n");
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return ENOMEM;
    }

    new_entry->offset    = new_ddt_offset;
    new_entry->blockType = DeDuplicationTable2;
    new_entry->dataType  = UserData;

    HASH_ADD(hh, index_hash, offset, sizeof(uint64_t), new_entry);

    printf("  New DDT entry added to index at offset %llu\n", (unsigned long long)new_ddt_offset);

    printf("\nNew DDT header details:\n");
    printf("  Structure size: %zu bytes (vs alpha20: %zu bytes)\n", sizeof(DdtHeader2), sizeof(ddt_v2_header_alpha20));
    printf("  negative: %u (32-bit)\n", new_ddt_header.negative);
    printf("  blocks: %llu\n", (unsigned long long)new_ddt_header.blocks);
    printf("  overflow: %u (32-bit)\n", new_ddt_header.overflow);

    // Count entries in updated index hash
    uint64_t updated_entry_count = HASH_COUNT(index_hash);
    printf("\n================================================================================\n");
    printf("                       WRITING UPDATED INDEX\n");
    printf("================================================================================\n\n");
    printf("Updated index contains %llu entries\n", (unsigned long long)updated_entry_count);

    // Allocate array for updated index entries
    IndexEntry *updated_entries = (IndexEntry *)malloc(sizeof(IndexEntry) * updated_entry_count);
    if(updated_entries == NULL)
    {
        printf("ERROR: Failed to allocate memory for updated index entries\n");
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return ENOMEM;
    }

    // Copy index entries from hash to array
    uint64_t idx = 0;
    HASH_ITER(hh, index_hash, current, tmp)
    {
        updated_entries[idx].blockType = current->blockType;
        updated_entries[idx].dataType  = current->dataType;
        updated_entries[idx].offset    = current->offset;
        idx++;
    }

    // Get current file size to determine where to write new index
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        printf("ERROR: Failed to seek to end of file: %s\n", strerror(errno));
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    file_size = ftell(fp);
    if(file_size < 0)
    {
        printf("ERROR: Failed to get file size: %s\n", strerror(errno));
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Calculate aligned position for new index
    uint64_t new_index_offset = ((uint64_t)file_size + alignment - 1) & ~(alignment - 1);

    printf("Writing new index:\n");
    printf("  Current file size: %lld bytes\n", (long long)file_size);
    printf("  Block alignment: %llu bytes (2^%u)\n", (unsigned long long)alignment, header.blockAlignmentShift);
    printf("  New index offset: %llu bytes\n", (unsigned long long)new_index_offset);

    // Seek to aligned position for new index
    if(fseek(fp, new_index_offset, SEEK_SET) != 0)
    {
        printf("ERROR: Failed to seek to new index position: %s\n", strerror(errno));
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Prepare new index header
    IndexHeader3 new_index_header;
    new_index_header.identifier = IndexBlock3;
    new_index_header.entries    = updated_entry_count;
    new_index_header.previous   = index_header.previous;

    // Calculate CRC64 of index entries (as done in close.c)
    crc64_ctx *index_crc64_context = aaruf_crc64_init();
    if(index_crc64_context != NULL && updated_entry_count > 0)
    {
        size_t index_data_size = updated_entry_count * sizeof(IndexEntry);
        aaruf_crc64_update(index_crc64_context, updated_entries, index_data_size);
        aaruf_crc64_final(index_crc64_context, &new_index_header.crc64);
        printf("  Calculated index CRC64: 0x%016llX\n", (unsigned long long)new_index_header.crc64);
    }
    else
    {
        new_index_header.crc64 = 0;
        printf("  Index CRC64 set to 0 (empty index or CRC context unavailable)\n");
    }

    // Write new index header
    written = fwrite(&new_index_header, 1, sizeof(IndexHeader3), fp);
    if(written != sizeof(IndexHeader3))
    {
        printf("ERROR: Failed to write new index header (wrote %zu bytes, expected %zu)\n", written,
               sizeof(IndexHeader3));
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    printf("  New index header written (%zu bytes)\n", written);

    // Write index entries
    written = fwrite(updated_entries, sizeof(IndexEntry), updated_entry_count, fp);
    if(written != updated_entry_count)
    {
        printf("ERROR: Failed to write index entries (wrote %zu, expected %llu)\n", written,
               (unsigned long long)updated_entry_count);
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    printf("  Index entries written (%llu entries, %zu bytes)\n", (unsigned long long)updated_entry_count,
           updated_entry_count * sizeof(IndexEntry));

    // Update header with new index offset
    printf("\n================================================================================\n");
    printf("                       UPDATING FILE HEADER\n");
    printf("================================================================================\n\n");
    printf("Updating header index offset:\n");
    printf("  Old index offset: %llu bytes\n", (unsigned long long)header.indexOffset);
    printf("  New index offset: %llu bytes\n", (unsigned long long)new_index_offset);

    header.indexOffset = new_index_offset;

    // Seek to start of file to write updated header
    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        printf("ERROR: Failed to seek to start of file: %s\n", strerror(errno));
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Write updated header
    written = fwrite(&header, 1, sizeof(AaruHeaderV2), fp);
    if(written != sizeof(AaruHeaderV2))
    {
        printf("ERROR: Failed to write updated header (wrote %zu bytes, expected %zu)\n", written,
               sizeof(AaruHeaderV2));
        free(updated_entries);
        free(ddt_payload);
        free(old_ddt_header);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    printf("  Updated header written (%zu bytes)\n", written);

    // Flush to ensure everything is written
    if(fflush(fp) != 0) { printf("WARNING: Failed to flush file: %s\n", strerror(errno)); }

    // Clean up
    free(updated_entries);
    free(ddt_payload);
    free(old_ddt_header);
    HASH_ITER(hh, index_hash, current, tmp)
    {
        HASH_DEL(index_hash, current);
        free(current);
    }
    free(entries);
    fclose(fp);

    printf("\n================================================================================\n");
    printf("                    UPGRADE COMPLETED SUCCESSFULLY\n");
    printf("================================================================================\n\n");
    printf("Summary of changes:\n");
    printf("  1. New DDT v2 block written at offset %llu (%zu + %llu bytes)\n", (unsigned long long)new_ddt_offset,
           sizeof(DdtHeader2), (unsigned long long)old_ddt_header->cmpLength);
    printf("  2. Updated index written at offset %llu (%zu + %zu bytes)\n", (unsigned long long)new_index_offset,
           sizeof(IndexHeader3), updated_entry_count * sizeof(IndexEntry));
    printf("  3. File header updated with new index offset\n");
    printf("\nThe image has been successfully upgraded to alpha21 format.\n");
    printf("You can now use this image with current versions of Aaru/libaaruformat.\n\n");

    return 0;
}
