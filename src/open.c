/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "internal.h"
#include "utarray.h"

void *aaruf_open(const char *filepath)
{
    aaruformatContext   *ctx       = NULL;
    int                  errorNo   = 0;
    size_t               readBytes = 0;
    long                 pos       = 0;
    uint8_t             *data      = NULL;
    int                  i = 0, j = 0;
    ChecksumHeader       checksum_header;
    ChecksumEntry const *checksum_entry = NULL;
    uint32_t             signature      = 0;
    UT_array            *index_entries  = NULL;

    ctx = (aaruformatContext *)malloc(sizeof(aaruformatContext));
    memset(ctx, 0, sizeof(aaruformatContext));

    if(ctx == NULL)
    {
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }

    ctx->imageStream = fopen(filepath, "rb");

    if(ctx->imageStream == NULL)
    {
        errorNo = errno;
        free(ctx);
        errno = errorNo;

        return NULL;
    }

    fseek(ctx->imageStream, 0, SEEK_SET);
    readBytes = fread(&ctx->header, 1, sizeof(AaruHeader), ctx->imageStream);

    if(readBytes != sizeof(AaruHeader))
    {
        free(ctx);
        errno = AARUF_ERROR_FILE_TOO_SMALL;

        return NULL;
    }

    if(ctx->header.identifier != DIC_MAGIC && ctx->header.identifier != AARU_MAGIC)
    {
        free(ctx);
        errno = AARUF_ERROR_NOT_AARUFORMAT;

        return NULL;
    }

    if(ctx->header.imageMajorVersion > AARUF_VERSION)
    {
        free(ctx);
        errno = AARUF_ERROR_INCOMPATIBLE_VERSION;

        return NULL;
    }

    fprintf(stderr, "libaaruformat: Opening image version %d.%d\n", ctx->header.imageMajorVersion,
            ctx->header.imageMinorVersion);

    ctx->readableSectorTags = (bool *)malloc(sizeof(bool) * MaxSectorTag);

    if(ctx->readableSectorTags == NULL)
    {
        free(ctx);
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        return NULL;
    }

    memset(ctx->readableSectorTags, 0, sizeof(bool) * MaxSectorTag);

    ctx->imageInfo.Application        = ctx->header.application;
    ctx->imageInfo.ApplicationVersion = (uint8_t *)malloc(32);
    if(ctx->imageInfo.ApplicationVersion != NULL)
    {
        memset(ctx->imageInfo.ApplicationVersion, 0, 32);
        sprintf((char *)ctx->imageInfo.ApplicationVersion, "%d.%d", ctx->header.applicationMajorVersion,
                ctx->header.applicationMinorVersion);
    }
    ctx->imageInfo.Version = (uint8_t *)malloc(32);
    if(ctx->imageInfo.Version != NULL)
    {
        memset(ctx->imageInfo.Version, 0, 32);
        sprintf((char *)ctx->imageInfo.Version, "%d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    }
    ctx->imageInfo.MediaType = ctx->header.mediaType;

    // Read the index header
    pos = fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);
    if(pos < 0)
    {
        free(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    pos = ftell(ctx->imageStream);
    if(pos != ctx->header.indexOffset)
    {
        free(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    readBytes = fread(&signature, 1, sizeof(uint32_t), ctx->imageStream);

    if(readBytes != sizeof(uint32_t) || (signature != IndexBlock && signature != IndexBlock2))
    {
        free(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    if(signature == IndexBlock)
        index_entries = process_index_v1(ctx);
    else if(signature == IndexBlock2)
        index_entries = process_index_v2(ctx);

    if(index_entries == NULL)
    {
        fprintf(stderr, "Could not process index.\n");
        utarray_free(index_entries);
        free(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    fprintf(stderr, "libaaruformat: Index at %" PRIu64 " contains %d entries\n", ctx->header.indexOffset,
            utarray_len(index_entries));

    for(i = 0; i < utarray_len(index_entries); i++)
    {
        IndexEntry *entry = (IndexEntry *)utarray_eltptr(index_entries, i);
        fprintf(stderr, "libaaruformat: Block type %4.4s with data type %d is indexed to be at %" PRIu64 "\n",
                (char *)&entry->blockType, entry->dataType, entry->offset);
    }

    bool foundUserDataDdt    = false;
    ctx->imageInfo.ImageSize = 0;
    for(i = 0; i < utarray_len(index_entries); i++)
    {
        IndexEntry *entry = (IndexEntry *)utarray_eltptr(index_entries, i);
        pos               = fseek(ctx->imageStream, entry->offset, SEEK_SET);

        if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
        {
            fprintf(stderr,
                    "libaaruformat: Could not seek to %" PRIu64 " as indicated by index entry %d, continuing...\n",
                    entry->offset, i);

            continue;
        }

        switch(entry->blockType)
        {
            case DataBlock:
                errorNo = process_data_block(ctx, entry);

                if(errorNo != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    free(ctx);
                    errno = errorNo;

                    return NULL;
                }

                break;

            case DeDuplicationTable:
                errorNo = process_ddt_v1(ctx, entry, &foundUserDataDdt);

                if(errorNo != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    free(ctx);
                    errno = errorNo;

                    return NULL;
                }

                break;
            case GeometryBlock:
                process_geometry_block(ctx, entry);

                break;
            case MetadataBlock:
                process_metadata_block(ctx, entry);

                break;
            case TracksBlock:
                process_tracks_block(ctx, entry);

                break;
            case CicmBlock:
                process_cicm_block(ctx, entry);

                break;
                // Dump hardware block
            case DumpHardwareBlock:
                process_dumphw_block(ctx, entry);

                break;
            case ChecksumBlock:
                readBytes = fread(&checksum_header, 1, sizeof(ChecksumHeader), ctx->imageStream);

                if(readBytes != sizeof(ChecksumHeader))
                {
                    memset(&checksum_header, 0, sizeof(ChecksumHeader));
                    fprintf(stderr, "libaaruformat: Could not read checksums block header, continuing...\n");
                    break;
                }

                if(checksum_header.identifier != ChecksumBlock)
                {
                    memset(&checksum_header, 0, sizeof(ChecksumHeader));
                    fprintf(stderr, "libaaruformat: Incorrect identifier for checksum block at position %" PRIu64 "\n",
                            entry->offset);
                }

                data = (uint8_t *)malloc(checksum_header.length);

                if(data == NULL)
                {
                    memset(&checksum_header, 0, sizeof(ChecksumHeader));
                    fprintf(stderr, "libaaruformat: Could not allocate memory for checksum block, continuing...\n");
                    break;
                }

                readBytes = fread(data, 1, checksum_header.length, ctx->imageStream);

                if(readBytes != checksum_header.length)
                {
                    memset(&checksum_header, 0, sizeof(ChecksumHeader));
                    free(data);
                    fprintf(stderr, "libaaruformat: Could not read checksums block, continuing...\n");
                    break;
                }

                pos = 0;
                for(j = 0; j < checksum_header.entries; j++)
                {
                    checksum_entry = (ChecksumEntry *)(&data[pos]);
                    pos += sizeof(ChecksumEntry);

                    if(checksum_entry->type == Md5)
                    {
                        memcpy(ctx->checksums.md5, &data[pos], MD5_DIGEST_LENGTH);
                        ctx->checksums.hasMd5 = true;
                    }
                    else if(checksum_entry->type == Sha1)
                    {
                        memcpy(ctx->checksums.sha1, &data[pos], SHA1_DIGEST_LENGTH);
                        ctx->checksums.hasSha1 = true;
                    }
                    else if(checksum_entry->type == Sha256)
                    {
                        memcpy(ctx->checksums.sha256, &data[pos], SHA256_DIGEST_LENGTH);
                        ctx->checksums.hasSha256 = true;
                    }
                    else if(checksum_entry->type == SpamSum)
                    {
                        ctx->checksums.spamsum = malloc(checksum_entry->length + 1);

                        if(ctx->checksums.spamsum != NULL)
                        {
                            memcpy(ctx->checksums.spamsum, &data[pos], checksum_entry->length);
                            ctx->checksums.hasSpamSum = true;
                        }

                        ctx->checksums.spamsum[checksum_entry->length] = 0;
                    }

                    pos += checksum_entry->length;
                }

                checksum_entry = NULL;
                free(data);

                break;
            default:
                fprintf(stderr,
                        "libaaruformat: Unhandled block type %4.4s with data type %d is indexed to be at %" PRIu64 "\n",
                        (char *)&entry->blockType, entry->dataType, entry->offset);
                break;
        }
    }

    utarray_free(index_entries);

    if(!foundUserDataDdt)
    {
        fprintf(stderr, "libaaruformat: Could not find user data deduplication table, aborting...\n");
        aaruf_close(ctx);
        return NULL;
    }

    ctx->imageInfo.CreationTime         = ctx->header.creationTime;
    ctx->imageInfo.LastModificationTime = ctx->header.lastWrittenTime;
    ctx->imageInfo.XmlMediaType         = aaruf_get_xml_mediatype(ctx->header.mediaType);

    if(ctx->geometryBlock.identifier != GeometryBlock && ctx->imageInfo.XmlMediaType == BlockMedia)
    {
        ctx->imageInfo.Cylinders       = (uint32_t)(ctx->imageInfo.Sectors / 16 / 63);
        ctx->imageInfo.Heads           = 16;
        ctx->imageInfo.SectorsPerTrack = 63;
    }

    // Initialize caches
    ctx->blockHeaderCache.cache     = NULL;
    ctx->blockHeaderCache.max_items = MAX_CACHE_SIZE / (ctx->imageInfo.SectorSize * (1 << ctx->shift));
    ctx->blockCache.cache           = NULL;
    ctx->blockCache.max_items       = ctx->blockHeaderCache.max_items;

    // TODO: Cache tracks and sessions?

    // Initialize ECC for Compact Disc
    ctx->eccCdContext = (CdEccContext *)aaruf_ecc_cd_init();

    ctx->magic               = AARU_MAGIC;
    ctx->libraryMajorVersion = LIBAARUFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBAARUFORMAT_MINOR_VERSION;

    return ctx;
}