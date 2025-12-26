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

    if(ddt_count == 0)
    {
        printf("WARNING: No DDT v2 blocks found in index\n");
        printf("         Nothing to upgrade.\n");

        // Clean up
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return 0;
    }

    // Arrays to store all DDT blocks that need upgrading
    typedef struct
    {
        ddt_v2_header_alpha20 *old_header;
        uint8_t               *payload;
        uint64_t               old_offset;
        uint64_t               new_offset;
        uint16_t               dataType;
    } DdtUpgradeInfo;

    DdtUpgradeInfo *ddt_upgrades = (DdtUpgradeInfo *)calloc(ddt_count, sizeof(DdtUpgradeInfo));
    if(ddt_upgrades == NULL)
    {
        printf("ERROR: Failed to allocate memory for DDT upgrade tracking\n");
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return ENOMEM;
    }

    // Process all DDT2 blocks
    uint64_t processed_count = 0;
    HASH_ITER(hh, index_hash, current, tmp)
    {
        if(current->blockType == DeDuplicationTable2)
        {
            printf("Processing DDT v2 block (dataType=%s) at offset %llu...\n", data_type_to_string(current->dataType),
                   (unsigned long long)current->offset);

            DdtUpgradeInfo *info = &ddt_upgrades[processed_count];
            info->old_offset     = current->offset;
            info->dataType       = current->dataType;

            // Seek to the DDT block
            if(fseek(fp, current->offset, SEEK_SET) != 0)
            {
                printf("ERROR: Failed to seek to DDT block at offset %llu: %s\n", (unsigned long long)current->offset,
                       strerror(errno));
                // Clean up already processed DDTs
                for(uint64_t i = 0; i < processed_count; i++)
                {
                    free(ddt_upgrades[i].old_header);
                    free(ddt_upgrades[i].payload);
                }
                free(ddt_upgrades);
                HASH_ITER(hh, index_hash, current, tmp)
                {
                    HASH_DEL(index_hash, current);
                    free(current);
                }
                free(entries);
                fclose(fp);
                return EIO;
            }

            // Allocate and read the old DDT header
            info->old_header = (ddt_v2_header_alpha20 *)malloc(sizeof(ddt_v2_header_alpha20));
            if(info->old_header == NULL)
            {
                printf("ERROR: Failed to allocate memory for old DDT header\n");
                // Clean up
                for(uint64_t i = 0; i < processed_count; i++)
                {
                    free(ddt_upgrades[i].old_header);
                    free(ddt_upgrades[i].payload);
                }
                free(ddt_upgrades);
                HASH_ITER(hh, index_hash, current, tmp)
                {
                    HASH_DEL(index_hash, current);
                    free(current);
                }
                free(entries);
                fclose(fp);
                return ENOMEM;
            }

            read_bytes = fread(info->old_header, 1, sizeof(ddt_v2_header_alpha20), fp);
            if(read_bytes != sizeof(ddt_v2_header_alpha20))
            {
                printf("ERROR: Failed to read old DDT header (read %zu bytes, expected %zu)\n", read_bytes,
                       sizeof(ddt_v2_header_alpha20));
                free(info->old_header);
                // Clean up
                for(uint64_t i = 0; i < processed_count; i++)
                {
                    free(ddt_upgrades[i].old_header);
                    free(ddt_upgrades[i].payload);
                }
                free(ddt_upgrades);
                HASH_ITER(hh, index_hash, current, tmp)
                {
                    HASH_DEL(index_hash, current);
                    free(current);
                }
                free(entries);
                fclose(fp);
                return EIO;
            }

            printf("  Old DDT header read successfully\n");
            printf("    Identifier: 0x%08X\n", info->old_header->identifier);
            printf("    Type: %u\n", info->old_header->type);
            printf("    Compression: %u\n", info->old_header->compression);
            printf("    Entries: %llu\n", (unsigned long long)info->old_header->entries);
            printf("    Compressed length: %llu bytes\n", (unsigned long long)info->old_header->cmpLength);
            printf("    Uncompressed length: %llu bytes\n", (unsigned long long)info->old_header->length);

            // Allocate memory for the compressed DDT payload
            info->payload = (uint8_t *)malloc(info->old_header->cmpLength);
            if(info->payload == NULL)
            {
                printf("ERROR: Failed to allocate %llu bytes for DDT payload\n",
                       (unsigned long long)info->old_header->cmpLength);
                free(info->old_header);
                // Clean up
                for(uint64_t i = 0; i < processed_count; i++)
                {
                    free(ddt_upgrades[i].old_header);
                    free(ddt_upgrades[i].payload);
                }
                free(ddt_upgrades);
                HASH_ITER(hh, index_hash, current, tmp)
                {
                    HASH_DEL(index_hash, current);
                    free(current);
                }
                free(entries);
                fclose(fp);
                return ENOMEM;
            }

            // Read the DDT payload
            read_bytes = fread(info->payload, 1, info->old_header->cmpLength, fp);
            if(read_bytes != info->old_header->cmpLength)
            {
                printf("ERROR: Failed to read DDT payload (read %zu bytes, expected %llu)\n", read_bytes,
                       (unsigned long long)info->old_header->cmpLength);
                free(info->payload);
                free(info->old_header);
                // Clean up
                for(uint64_t i = 0; i < processed_count; i++)
                {
                    free(ddt_upgrades[i].old_header);
                    free(ddt_upgrades[i].payload);
                }
                free(ddt_upgrades);
                HASH_ITER(hh, index_hash, current, tmp)
                {
                    HASH_DEL(index_hash, current);
                    free(current);
                }
                free(entries);
                fclose(fp);
                return EIO;
            }

            printf("  DDT payload read successfully (%llu bytes)\n", (unsigned long long)info->old_header->cmpLength);

            // Remove this entry from the index hash
            printf("  Removing DDT entry from index...\n");
            HASH_DEL(index_hash, current);
            free(current);

            printf("  DDT block processed and removed from index\n\n");
            processed_count++;
        }
    }

    if(processed_count == 0)
    {
        printf("ERROR: No DDT v2 blocks were processed\n");
        free(ddt_upgrades);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EINVAL;
    }

    printf("Processed %llu DDT v2 block(s)\n", (unsigned long long)processed_count);

    // Find UserData DDT for media type checks (if it exists)
    ddt_v2_header_alpha20 *userdata_ddt_header = NULL;
    for(uint64_t i = 0; i < processed_count; i++)
    {
        if(ddt_upgrades[i].dataType == UserData)
        {
            userdata_ddt_header = ddt_upgrades[i].old_header;
            break;
        }
    }

    if(userdata_ddt_header == NULL)
    {
        printf("ERROR: No UserData DDT v2 block found in this image.\n");
        printf("       Images without a UserData DDT are invalid/corrupted.\n");
        printf("       Cannot proceed with upgrade.\n");

        // Clean up
        for(uint64_t i = 0; i < processed_count; i++)
        {
            free(ddt_upgrades[i].old_header);
            free(ddt_upgrades[i].payload);
        }
        free(ddt_upgrades);
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

    // Check for DVD media types with specific negative/overflow configuration
    // Only check on UserData DDT if one exists
    bool is_dvd_type = (header.mediaType == DVDROM || header.mediaType == DVDR || header.mediaType == DVDRW ||
                        header.mediaType == DVDPR || header.mediaType == DVDPRW || header.mediaType == DVDPRWDL ||
                        header.mediaType == DVDRDL || header.mediaType == DVDPRDL || header.mediaType == DVDRWDL ||
                        header.mediaType == DVDDownload || header.mediaType == PS2DVD || header.mediaType == PS3DVD ||
                        header.mediaType == Nuon);

    if(userdata_ddt_header && is_dvd_type && userdata_ddt_header->negative == 0 &&
       userdata_ddt_header->overflow == 15000)
    {
        printf("\n*** INVALID BLOCK COUNT DETECTED ***\n");
        printf("Media type: %u (%s)\n", header.mediaType, media_type_to_string(header.mediaType));
        printf("Current values (from UserData DDT):\n");
        printf("  negative sectors: %u\n", userdata_ddt_header->negative);
        printf("  overflow sectors: %u\n", userdata_ddt_header->overflow);
        printf("  blocks: %llu\n", (unsigned long long)userdata_ddt_header->blocks);
        printf("\nThis configuration is invalid for DVD media.\n");
        printf("To fix: overflow should be 0, and blocks should be reduced by 0x30000 + 15000\n\n");
        printf("Do you want to fix the block count? (yes/no): ");
        fflush(stdout);

        char response[256];
        if(fgets(response, sizeof(response), stdin) == NULL)
        {
            printf("\nERROR: Failed to read user input\n");
            for(uint64_t i = 0; i < processed_count; i++)
            {
                free(ddt_upgrades[i].old_header);
                free(ddt_upgrades[i].payload);
            }
            free(ddt_upgrades);
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
            for(uint64_t i = 0; i < processed_count; i++)
            {
                free(ddt_upgrades[i].old_header);
                free(ddt_upgrades[i].payload);
            }
            free(ddt_upgrades);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return 0;
        }

        // Fix the block count on ALL DDT blocks
        printf("\nApplying block count fix to all DDT blocks:\n");
        for(uint64_t i = 0; i < processed_count; i++)
        {
            ddt_v2_header_alpha20 *ddt_hdr = ddt_upgrades[i].old_header;

            // Only update if the fields are not 0
            if(ddt_hdr->blocks != 0 || ddt_hdr->negative != 0 || ddt_hdr->overflow != 0)
            {
                printf("  DDT block %llu (dataType=%s):\n", (unsigned long long)(i + 1),
                       data_type_to_string(ddt_upgrades[i].dataType));
                printf("    Before: blocks=%llu, negative=%u, overflow=%u\n", (unsigned long long)ddt_hdr->blocks,
                       ddt_hdr->negative, ddt_hdr->overflow);

                if(ddt_hdr->overflow != 0) ddt_hdr->overflow = 0;
                if(ddt_hdr->blocks != 0) ddt_hdr->blocks = ddt_hdr->blocks - 0x30000 - 15000;

                printf("    After:  blocks=%llu, negative=%u, overflow=%u\n", (unsigned long long)ddt_hdr->blocks,
                       ddt_hdr->negative, ddt_hdr->overflow);
            }
        }
        printf("\n");
    }

    // Check for DVD-RAM
    if(header.mediaType == DVDRAM)
    {
        printf("\nERROR: Cannot upgrade DVD-RAM media type\n");
        printf("       DVD-RAM images cannot be upgraded with this tool.\n");
        for(uint64_t i = 0; i < processed_count; i++)
        {
            free(ddt_upgrades[i].old_header);
            free(ddt_upgrades[i].payload);
        }
        free(ddt_upgrades);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EINVAL;
    }

    // Check for negative=0 and overflow>0 on UserData DDT (other than the DVD case handled above)
    if(userdata_ddt_header && userdata_ddt_header->negative == 0 && userdata_ddt_header->overflow > 0 && !is_dvd_type)
    {
        printf("\nERROR: Invalid sector configuration (from UserData DDT)\n");
        printf("       negative sectors: %u\n", userdata_ddt_header->negative);
        printf("       overflow sectors: %u\n", userdata_ddt_header->overflow);
        printf("\n       When negative sectors is 0 and overflow sectors is > 0,\n");
        printf("       the image cannot be reliably upgraded.\n");
        for(uint64_t i = 0; i < processed_count; i++)
        {
            free(ddt_upgrades[i].old_header);
            free(ddt_upgrades[i].payload);
        }
        free(ddt_upgrades);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EINVAL;
    }

    // Get current file size to determine where to write new DDTs
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        printf("ERROR: Failed to seek to end of file: %s\n", strerror(errno));
        for(uint64_t i = 0; i < processed_count; i++)
        {
            free(ddt_upgrades[i].old_header);
            free(ddt_upgrades[i].payload);
        }
        free(ddt_upgrades);
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
        for(uint64_t i = 0; i < processed_count; i++)
        {
            free(ddt_upgrades[i].old_header);
            free(ddt_upgrades[i].payload);
        }
        free(ddt_upgrades);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        fclose(fp);
        return EIO;
    }

    // Calculate alignment
    uint64_t alignment = 1ULL << header.blockAlignmentShift;

    // Reopen file in read-write mode for appending
    fclose(fp);
    fp = fopen(path, "r+b");
    if(fp == NULL)
    {
        printf("ERROR: Failed to reopen file in write mode: %s\n", strerror(errno));
        for(uint64_t i = 0; i < processed_count; i++)
        {
            free(ddt_upgrades[i].old_header);
            free(ddt_upgrades[i].payload);
        }
        free(ddt_upgrades);
        HASH_ITER(hh, index_hash, current, tmp)
        {
            HASH_DEL(index_hash, current);
            free(current);
        }
        free(entries);
        return errno;
    }

    printf("\nWriting upgraded DDT blocks:\n");

    // Write all DDT blocks at aligned positions
    for(uint64_t i = 0; i < processed_count; i++)
    {
        DdtUpgradeInfo *info = &ddt_upgrades[i];

        // Get current file size for this DDT
        if(fseek(fp, 0, SEEK_END) != 0)
        {
            printf("ERROR: Failed to seek to end of file: %s\n", strerror(errno));
            for(uint64_t j = 0; j < processed_count; j++)
            {
                free(ddt_upgrades[j].old_header);
                free(ddt_upgrades[j].payload);
            }
            free(ddt_upgrades);
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
            for(uint64_t j = 0; j < processed_count; j++)
            {
                free(ddt_upgrades[j].old_header);
                free(ddt_upgrades[j].payload);
            }
            free(ddt_upgrades);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return EIO;
        }

        // Calculate aligned position for this DDT block
        uint64_t new_ddt_offset = ((uint64_t)file_size + alignment - 1) & ~(alignment - 1);
        info->new_offset        = new_ddt_offset;

        printf("  DDT block %llu (dataType=%s):\n", (unsigned long long)(i + 1), data_type_to_string(info->dataType));
        printf("    Old offset: %llu bytes\n", (unsigned long long)info->old_offset);
        printf("    New offset: %llu bytes\n", (unsigned long long)new_ddt_offset);

        // Seek to aligned position
        if(fseek(fp, new_ddt_offset, SEEK_SET) != 0)
        {
            printf("ERROR: Failed to seek to new DDT position: %s\n", strerror(errno));
            for(uint64_t j = 0; j < processed_count; j++)
            {
                free(ddt_upgrades[j].old_header);
                free(ddt_upgrades[j].payload);
            }
            free(ddt_upgrades);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return EIO;
        }

        // Create new DDT header from old one
        DdtHeader2 new_ddt_header;
        memset(&new_ddt_header, 0, sizeof(DdtHeader2));

        new_ddt_header.identifier          = info->old_header->identifier;
        new_ddt_header.type                = info->old_header->type;
        new_ddt_header.compression         = info->old_header->compression;
        new_ddt_header.levels              = info->old_header->levels;
        new_ddt_header.tableLevel          = info->old_header->tableLevel;
        new_ddt_header.previousLevelOffset = info->old_header->previousLevelOffset;
        new_ddt_header.negative            = info->old_header->negative;
        new_ddt_header.blocks              = info->old_header->blocks;
        new_ddt_header.overflow            = info->old_header->overflow;
        new_ddt_header.start               = info->old_header->start;
        new_ddt_header.blockAlignmentShift = info->old_header->blockAlignmentShift;
        new_ddt_header.dataShift           = info->old_header->dataShift;
        new_ddt_header.tableShift          = info->old_header->tableShift;
        new_ddt_header.entries             = info->old_header->entries;
        new_ddt_header.cmpLength           = info->old_header->cmpLength;
        new_ddt_header.length              = info->old_header->length;
        new_ddt_header.cmpCrc64            = info->old_header->cmpCrc64;
        new_ddt_header.crc64               = info->old_header->crc64;

        // Write new DDT header
        size_t written = fwrite(&new_ddt_header, 1, sizeof(DdtHeader2), fp);
        if(written != sizeof(DdtHeader2))
        {
            printf("ERROR: Failed to write new DDT header (wrote %zu bytes, expected %zu)\n", written,
                   sizeof(DdtHeader2));
            for(uint64_t j = 0; j < processed_count; j++)
            {
                free(ddt_upgrades[j].old_header);
                free(ddt_upgrades[j].payload);
            }
            free(ddt_upgrades);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return EIO;
        }

        // Write DDT payload (unchanged)
        written = fwrite(info->payload, 1, info->old_header->cmpLength, fp);
        if(written != info->old_header->cmpLength)
        {
            printf("ERROR: Failed to write DDT payload (wrote %zu bytes, expected %llu)\n", written,
                   (unsigned long long)info->old_header->cmpLength);
            for(uint64_t j = 0; j < processed_count; j++)
            {
                free(ddt_upgrades[j].old_header);
                free(ddt_upgrades[j].payload);
            }
            free(ddt_upgrades);
            HASH_ITER(hh, index_hash, current, tmp)
            {
                HASH_DEL(index_hash, current);
                free(current);
            }
            free(entries);
            fclose(fp);
            return EIO;
        }

        printf("    Header written: %zu bytes\n", sizeof(DdtHeader2));
        printf("    Payload written: %llu bytes\n", (unsigned long long)info->old_header->cmpLength);

        // Add new DDT entry to index hash
        IndexEntryHash *new_entry = (IndexEntryHash *)malloc(sizeof(IndexEntryHash));
        if(new_entry == NULL)
        {
            printf("ERROR: Failed to allocate memory for new index entry\n");
            for(uint64_t j = 0; j < processed_count; j++)
            {
                free(ddt_upgrades[j].old_header);
                free(ddt_upgrades[j].payload);
            }
            free(ddt_upgrades);
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
        new_entry->dataType  = info->dataType;

        HASH_ADD(hh, index_hash, offset, sizeof(uint64_t), new_entry);
        printf("    Added to index\n\n");
    }

    printf("All %llu DDT block(s) upgraded successfully\n", (unsigned long long)processed_count);

    // Clean up DDT upgrade tracking
    for(uint64_t i = 0; i < processed_count; i++)
    {
        free(ddt_upgrades[i].old_header);
        free(ddt_upgrades[i].payload);
    }
    free(ddt_upgrades);

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
        aaruf_crc64_update(index_crc64_context, (const uint8_t *)updated_entries, index_data_size);
        aaruf_crc64_final(index_crc64_context, &new_index_header.crc64);
        printf("  Calculated index CRC64: 0x%016llX\n", (unsigned long long)new_index_header.crc64);
    }
    else
    {
        new_index_header.crc64 = 0;
        printf("  Index CRC64 set to 0 (empty index or CRC context unavailable)\n");
    }

    // Write new index header
    size_t written = fwrite(&new_index_header, 1, sizeof(IndexHeader3), fp);
    if(written != sizeof(IndexHeader3))
    {
        printf("ERROR: Failed to write new index header (wrote %zu bytes, expected %zu)\n", written,
               sizeof(IndexHeader3));
        free(updated_entries);
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
    printf("  1. %llu DDT v2 block(s) upgraded and written\n", (unsigned long long)processed_count);
    printf("  2. Updated index written at offset %llu (%zu + %zu bytes)\n", (unsigned long long)new_index_offset,
           sizeof(IndexHeader3), updated_entry_count * sizeof(IndexEntry));
    printf("  3. File header updated with new index offset\n");
    printf("\nThe image has been successfully upgraded to alpha21 format.\n");
    printf("You can now use this image with current versions of Aaru/libaaruformat.\n\n");

    return 0;
}
