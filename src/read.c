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

#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "internal.h"
#include "log.h"

/**
 * @brief Reads a media tag from the AaruFormat image.
 *
 * Reads the specified media tag from the image and stores it in the provided buffer.
 * Media tags contain metadata information about the storage medium such as disc
 * information, lead-in/lead-out data, or manufacturer-specific information.
 *
 * @param context Pointer to the aaruformat context.
 * @param data Pointer to the buffer to store the tag data. Can be NULL to query tag length.
 * @param tag Tag identifier to read.
 * @param length Pointer to the length of the buffer on input; updated with actual tag length on output.
 *
 * @return AARUF_STATUS_OK on success,
 *         AARUF_ERROR_NOT_AARUFORMAT if context is NULL or invalid (magic number mismatch),
 *         AARUF_ERROR_MEDIA_TAG_NOT_PRESENT if the requested media tag identifier does not exist in the image,
 *         AARUF_ERROR_BUFFER_TOO_SMALL if data is NULL or provided buffer size is insufficient for the tag data.
 */
int32_t aaruf_read_media_tag(void *context, uint8_t *data, int32_t tag, uint32_t *length)
{
    TRACE("Entering aaruf_read_media_tag(%p, %p, %d, %u)", context, data, tag, *length);

    aaruformatContext *ctx;
    mediaTagEntry     *item;

    if(context == NULL)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_read_media_tag() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_read_media_tag() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    TRACE("Finding media tag %d", tag);
    HASH_FIND_INT(ctx->mediaTags, &tag, item);

    if(item == NULL)
    {
        TRACE("Media tag not found");
        *length = 0;

        TRACE("Exiting aaruf_read_media_tag() = AARUF_ERROR_MEDIA_TAG_NOT_PRESENT");
        return AARUF_ERROR_MEDIA_TAG_NOT_PRESENT;
    }

    if(data == NULL || *length < item->length)
    {
        TRACE("Buffer too small for media tag %d, required %u bytes", tag, item->length);
        *length = item->length;

        TRACE("Exiting aaruf_read_media_tag() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = item->length;
    memcpy(data, item->data, item->length);

    TRACE("Media tag %d read successfully, length %u", tag, *length);
    TRACE("Exiting aaruf_read_media_tag() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Reads a sector from the AaruFormat image.
 *
 * Reads user data from the specified sector address in the image. This function
 * reads only the user data portion of the sector, without any additional metadata
 * or ECC/EDC information.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address The logical sector address to read from.
 * @param data Pointer to buffer where sector data will be stored. Can be NULL to query length.
 * @param length Pointer to variable containing buffer size on input, actual data length on output.
 *
 * @return AARUF_STATUS_OK on success,
 *         AARUF_STATUS_SECTOR_NOT_DUMPED if sector was not dumped during imaging,
 *         AARUF_ERROR_NOT_AARUFORMAT if context is NULL or invalid (magic number mismatch),
 *         AARUF_ERROR_SECTOR_OUT_OF_BOUNDS if sector address exceeds image sector count,
 *         AARUF_ERROR_BUFFER_TOO_SMALL if data is NULL or buffer size is insufficient,
 *         AARUF_ERROR_NOT_ENOUGH_MEMORY if memory allocation fails for block operations,
 *         AARUF_ERROR_CANNOT_READ_HEADER if block header cannot be read from image stream,
 *         AARUF_ERROR_CANNOT_READ_BLOCK if block data cannot be read from image stream,
 *         AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK if LZMA or FLAC decompression fails,
 *         AARUF_ERROR_UNSUPPORTED_COMPRESSION if block uses unsupported compression algorithm,
 *         or other error codes from DDT decoding functions.
 */
int32_t aaruf_read_sector(void *context, uint64_t sector_address, uint8_t *data, uint32_t *length)
{
    TRACE("Entering aaruf_read_sector(%p, %" PRIu64 ", %p, %u)", context, sector_address, data, *length);

    aaruformatContext *ctx          = NULL;
    uint64_t           offset       = 0;
    uint64_t           block_offset = 0;
    BlockHeader       *block_header = NULL;
    uint8_t           *block        = NULL;
    size_t             read_bytes   = 0;
    uint8_t            lzma_properties[LZMA_PROPERTIES_LENGTH];
    size_t             lzma_size     = 0;
    uint8_t           *cmp_data      = NULL;
    int                error_no      = 0;
    uint8_t            sector_status = 0;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(sector_address > ctx->imageInfo.Sectors - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(ctx->ddtVersion == 1)
        error_no = decode_ddt_entry_v1(ctx, sector_address, &offset, &block_offset, &sector_status);
    else if(ctx->ddtVersion == 2)
        error_no = decode_ddt_entry_v2(ctx, sector_address, &offset, &block_offset, &sector_status);

    if(error_no != AARUF_STATUS_OK)
    {
        FATAL("Error %d decoding DDT entry", error_no);

        TRACE("Exiting aaruf_read_sector() = %d", error_no);
        return error_no;
    }

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(sector_status == SectorStatusNotDumped)
    {
        *length = ctx->imageInfo.SectorSize;

        TRACE("Exiting aaruf_read_sector() = AARUF_STATUS_SECTOR_NOT_DUMPED");
        return AARUF_STATUS_SECTOR_NOT_DUMPED;
    }

    // Check if block header is cached
    TRACE("Checking if block header is cached");
    block_header = find_in_cache_uint64(&ctx->blockHeaderCache, block_offset);

    // Read block header
    if(block_header == NULL)
    {
        TRACE("Allocating memory for block header");
        block_header = malloc(sizeof(BlockHeader));
        if(block_header == NULL)
        {
            FATAL("Not enough memory for block header");

            TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }

        TRACE("Reading block header");
        fseek(ctx->imageStream, block_offset, SEEK_SET);
        read_bytes = fread(block_header, 1, sizeof(BlockHeader), ctx->imageStream);

        if(read_bytes != sizeof(BlockHeader))
        {
            FATAL("Error reading block header");

            TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_READ_HEADER");
            return AARUF_ERROR_CANNOT_READ_HEADER;
        }

        TRACE("Adding block header to cache");
        add_to_cache_uint64(&ctx->blockHeaderCache, block_offset, block_header);
    }
    else
        fseek(ctx->imageStream, block_offset + sizeof(BlockHeader), SEEK_SET);  // Advance as if reading the header

    if(data == NULL || *length < block_header->sectorSize)
    {
        TRACE("Buffer too small for sector, required %u bytes", block_header->sectorSize);
        *length = block_header->sectorSize;

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Check if block is cached
    TRACE("Checking if block is cached");
    block = find_in_cache_uint64(&ctx->blockCache, block_offset);

    if(block != NULL)
    {
        TRACE("Getting data from cache");
        memcpy(data, block + offset * block_header->sectorSize, block_header->sectorSize);
        *length = block_header->sectorSize;

        TRACE("Exiting aaruf_read_sector() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    // Decompress block
    switch(block_header->compression)
    {
        case None:
            TRACE("Allocating memory for block");
            block = (uint8_t *)malloc(block_header->length);
            if(block == NULL)
            {
                FATAL("Not enough memory for block");

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            TRACE("Reading block into memory");
            read_bytes = fread(block, 1, block_header->length, ctx->imageStream);

            if(read_bytes != block_header->length)
            {
                FATAL("Could not read block");
                free(block);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_READ_BLOCK");
                return AARUF_ERROR_CANNOT_READ_BLOCK;
            }

            break;
        case Lzma:
            lzma_size = block_header->cmpLength - LZMA_PROPERTIES_LENGTH;
            TRACE("Allocating memory for compressed data of size %zu bytes", lzma_size);
            cmp_data = malloc(lzma_size);

            if(cmp_data == NULL)
            {
                FATAL("Cannot allocate memory for block...");

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            TRACE("Allocating memory for block of size %zu bytes", block_header->length);
            block = malloc(block_header->length);
            if(block == NULL)
            {
                FATAL("Cannot allocate memory for block...");
                free(cmp_data);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            read_bytes = fread(lzma_properties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);

            if(read_bytes != LZMA_PROPERTIES_LENGTH)
            {
                FATAL("Could not read LZMA properties...");
                free(block);
                free(cmp_data);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
            if(read_bytes != lzma_size)
            {
                FATAL("Could not read compressed block...");
                free(cmp_data);
                free(block);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            TRACE("Decompressing block of size %zu bytes", block_header->length);
            read_bytes = block_header->length;
            error_no   = aaruf_lzma_decode_buffer(block, &read_bytes, cmp_data, &lzma_size, lzma_properties,
                                                  LZMA_PROPERTIES_LENGTH);

            if(error_no != 0)
            {
                FATAL("Got error %d from LZMA...", error_no);
                free(cmp_data);
                free(block);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            if(read_bytes != block_header->length)
            {
                FATAL("Error decompressing block, should be {0} bytes but got {1} bytes...");
                free(cmp_data);
                free(block);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            free(cmp_data);

            break;
        case Flac:
            TRACE("Allocating memory for compressed data of size %zu bytes", block_header->cmpLength);
            cmp_data = malloc(block_header->cmpLength);

            if(cmp_data == NULL)
            {
                FATAL("Cannot allocate memory for block...");

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            TRACE("Allocating memory for block of size %zu bytes", block_header->length);
            block = malloc(block_header->length);
            if(block == NULL)
            {
                FATAL("Cannot allocate memory for block...");
                free(cmp_data);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            TRACE("Reading compressed data into memory");
            read_bytes = fread(cmp_data, 1, block_header->cmpLength, ctx->imageStream);
            if(read_bytes != block_header->cmpLength)
            {
                FATAL("Could not read compressed block...");
                free(cmp_data);
                free(block);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            TRACE("Decompressing block of size %zu bytes", block_header->length);
            read_bytes =
                aaruf_flac_decode_redbook_buffer(block, block_header->length, cmp_data, block_header->cmpLength);

            if(read_bytes != block_header->length)
            {
                FATAL("Error decompressing block, should be {0} bytes but got {1} bytes...");
                free(cmp_data);
                free(block);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            free(cmp_data);

            break;
        default:
            FATAL("Unsupported compression %d", block_header->compression);
            TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_UNSUPPORTED_COMPRESSION");
            return AARUF_ERROR_UNSUPPORTED_COMPRESSION;
    }

    // Add block to cache
    TRACE("Adding block to cache");
    add_to_cache_uint64(&ctx->blockCache, block_offset, block);

    memcpy(data, block + (offset * block_header->sectorSize), block_header->sectorSize);
    *length = block_header->sectorSize;

    TRACE("Exiting aaruf_read_sector() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Reads a sector from a specific track in the AaruFormat image.
 *
 * Reads user data from the specified sector address within a particular track.
 * This function is specifically designed for optical disc images where sectors
 * are organized by tracks. The sector address is relative to the start of the track.
 *
 * @param context Pointer to the aaruformat context.
 * @param data Pointer to buffer where sector data will be stored.
 * @param sector_address The sector address relative to the track start.
 * @param length Pointer to variable containing buffer size on input, actual data length on output.
 * @param track The track number to read from.
 *
 * @return AARUF_STATUS_OK on success,
 *         AARUF_STATUS_SECTOR_NOT_DUMPED if sector was not dumped during imaging,
 *         AARUF_ERROR_NOT_AARUFORMAT if context is NULL or invalid (magic number mismatch),
 *         AARUF_ERROR_INCORRECT_MEDIA_TYPE if media is not an optical disc,
 *         AARUF_ERROR_TRACK_NOT_FOUND if the specified track sequence does not exist,
 *         AARUF_ERROR_SECTOR_OUT_OF_BOUNDS if calculated sector address exceeds image bounds,
 *         AARUF_ERROR_BUFFER_TOO_SMALL if data buffer is NULL or insufficient size,
 *         AARUF_ERROR_NOT_ENOUGH_MEMORY if memory allocation fails during sector reading,
 *         AARUF_ERROR_CANNOT_READ_HEADER if block header cannot be read from image stream,
 *         AARUF_ERROR_CANNOT_READ_BLOCK if block data cannot be read from image stream,
 *         AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK if LZMA or FLAC decompression fails,
 *         AARUF_ERROR_UNSUPPORTED_COMPRESSION if block uses unsupported compression,
 *         or other error codes from underlying aaruf_read_sector function.
 */
int32_t aaruf_read_track_sector(void *context, uint8_t *data, uint64_t sectorAddress, uint32_t *length, uint8_t track)
{
    TRACE("Entering aaruf_read_track_sector(%p, %p, %" PRIu64 ", %u, %d)", context, data, sectorAddress, *length,
          track);

    aaruformatContext *ctx;
    int                i;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->imageInfo.XmlMediaType != OpticalDisc)
    {
        FATAL("Incorrect media type %d, expected OpticalDisc", ctx->imageInfo.XmlMediaType);

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
        return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
    }

    for(i = 0; i < ctx->numberOfDataTracks; i++)
        if(ctx->dataTracks[i].sequence == track)
            return aaruf_read_sector(context, ctx->dataTracks[i].start + sectorAddress, data, length);

    TRACE("Track %d not found", track);
    TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_TRACK_NOT_FOUND");
    return AARUF_ERROR_TRACK_NOT_FOUND;
}

/**
 * @brief Reads a complete sector with all metadata from the AaruFormat image.
 *
 * Reads the complete sector data including user data, ECC/EDC, subchannel data,
 * and other metadata depending on the media type. For optical discs, this returns
 * a full 2352-byte sector with sync, header, user data, and ECC/EDC. For block
 * media with tags, this includes both the user data and tag information.
 *
 * @param context Pointer to the aaruformat context.
 * @param sectorAddress The logical sector address to read from.
 * @param data Pointer to buffer where complete sector data will be stored. Can be NULL to query length.
 * @param length Pointer to variable containing buffer size on input, actual data length on output.
 *
 * @return AARUF_STATUS_OK on success,
 *         AARUF_STATUS_SECTOR_NOT_DUMPED if sector was not dumped during imaging,
 *         AARUF_ERROR_NOT_AARUFORMAT if context is NULL or invalid (magic number mismatch),
 *         AARUF_ERROR_BUFFER_TOO_SMALL if data is NULL or buffer size insufficient for complete sector,
 *         AARUF_ERROR_NOT_ENOUGH_MEMORY if memory allocation fails for bare data operations,
 *         AARUF_ERROR_TRACK_NOT_FOUND if sector's track cannot be found in track list,
 *         AARUF_ERROR_INCORRECT_MEDIA_TYPE if media type doesn't support long sector reading,
 *         AARUF_ERROR_INVALID_TRACK_FORMAT if track has an unsupported or invalid format,
 *         AARUF_ERROR_REACHED_UNREACHABLE_CODE if internal logic reaches unexpected state,
 *         AARUF_ERROR_SECTOR_OUT_OF_BOUNDS if sector address exceeds image bounds (from aaruf_read_sector),
 *         AARUF_ERROR_CANNOT_READ_HEADER if block header cannot be read (from aaruf_read_sector),
 *         AARUF_ERROR_CANNOT_READ_BLOCK if block data cannot be read (from aaruf_read_sector),
 *         AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK if decompression fails (from aaruf_read_sector),
 *         AARUF_ERROR_UNSUPPORTED_COMPRESSION if compression algorithm not supported (from aaruf_read_sector),
 *         or other error codes from underlying aaruf_read_sector function calls.
 */
int32_t aaruf_read_sector_long(void *context, uint64_t sectorAddress, uint8_t *data, uint32_t *length)
{
    TRACE("Entering aaruf_read_sector_long(%p, %" PRIu64 ", %p, %u)", context, sectorAddress, data, *length);

    aaruformatContext *ctx        = NULL;
    uint32_t           bareLength = 0;
    uint32_t           tagLength  = 0;
    uint8_t           *bareData   = NULL;
    int32_t            res        = 0;
    TrackEntry         trk;
    int                i        = 0;
    bool               trkFound = false;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    switch(ctx->imageInfo.XmlMediaType)
    {
        case OpticalDisc:
            if(*length < 2352 || data == NULL)
            {
                *length = 2352;
                FATAL("Buffer too small for sector, required %u bytes", *length);

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_BUFFER_TOO_SMALL");
                return AARUF_ERROR_BUFFER_TOO_SMALL;
            }
            if((ctx->sectorSuffix == NULL || ctx->sectorPrefix == NULL) &&
               (ctx->sectorSuffixCorrected == NULL || ctx->sectorPrefixCorrected == NULL))
                return aaruf_read_sector(context, sectorAddress, data, length);

            bareLength = 0;
            aaruf_read_sector(context, sectorAddress, NULL, &bareLength);

            TRACE("Allocating memory for bare data");
            bareData = (uint8_t *)malloc(bareLength);

            if(bareData == NULL)
            {
                FATAL("Could not allocate memory for bare data");

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            res = aaruf_read_sector(context, sectorAddress, bareData, &bareLength);

            if(res < AARUF_STATUS_OK)
            {
                free(bareData);

                TRACE("Exiting aaruf_read_sector_long() = %d", res);
                return res;
            }

            trkFound = false;

            for(i = 0; i < ctx->numberOfDataTracks; i++)
                if(sectorAddress >= ctx->dataTracks[i].start && sectorAddress <= ctx->dataTracks[i].end)
                {
                    trkFound = true;
                    trk      = ctx->dataTracks[i];
                    break;
                }

            if(!trkFound)
            {
                FATAL("Track not found");

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_TRACK_NOT_FOUND");
                return AARUF_ERROR_TRACK_NOT_FOUND;
            }

            switch(trk.type)
            {
                case Audio:
                case Data:
                    memcpy(data, bareData, bareLength);
                    return res;
                case CdMode1:
                    memcpy(data + 16, bareData, 2048);

                    if(ctx->sectorPrefix != NULL)
                        memcpy(data, ctx->sectorPrefix + (sectorAddress * 16), 16);
                    else if(ctx->sectorPrefixDdt != NULL)
                    {
                        if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == Correct)
                        {
                            aaruf_ecc_cd_reconstruct_prefix(data, trk.type, sectorAddress);
                            res = AARUF_STATUS_OK;
                        }
                        else if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data,
                                   ctx->sectorPrefixCorrected +
                                       ((ctx->sectorPrefixDdt[sectorAddress] & CD_DFIX_MASK) - 1) * 16,
                                   16);
                    }
                    else
                    {
                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_REACHED_UNREACHABLE_CODE");
                        return AARUF_ERROR_REACHED_UNREACHABLE_CODE;
                    }

                    if(res != AARUF_STATUS_OK) return res;

                    if(ctx->sectorSuffix != NULL)
                        memcpy(data + 2064, ctx->sectorSuffix + sectorAddress * 288, 288);
                    else if(ctx->sectorSuffixDdt != NULL)
                    {
                        if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Correct)
                        {
                            aaruf_ecc_cd_reconstruct(ctx->eccCdContext, data, trk.type);
                            res = AARUF_STATUS_OK;
                        }
                        else if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data + 2064,
                                   ctx->sectorSuffixCorrected +
                                       ((ctx->sectorSuffixDdt[sectorAddress] & CD_DFIX_MASK) - 1) * 288,
                                   288);
                    }
                    else
                    {
                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_REACHED_UNREACHABLE_CODE");
                        return AARUF_ERROR_REACHED_UNREACHABLE_CODE;
                    }

                    return res;
                case CdMode2Formless:
                case CdMode2Form1:
                case CdMode2Form2:
                    if(ctx->sectorPrefix != NULL)
                        memcpy(data, ctx->sectorPrefix + sectorAddress * 16, 16);
                    else if(ctx->sectorPrefixDdt != NULL)
                    {
                        if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == Correct)
                        {
                            aaruf_ecc_cd_reconstruct_prefix(data, trk.type, sectorAddress);
                            res = AARUF_STATUS_OK;
                        }
                        else if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data,
                                   ctx->sectorPrefixCorrected +
                                       ((ctx->sectorPrefixDdt[sectorAddress] & CD_DFIX_MASK) - 1) * 16,
                                   16);
                    }
                    else
                    {
                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_REACHED_UNREACHABLE_CODE");
                        return AARUF_ERROR_REACHED_UNREACHABLE_CODE;
                    }

                    if(res != AARUF_STATUS_OK) return res;

                    if(ctx->mode2Subheaders != NULL && ctx->sectorSuffixDdt != NULL)
                    {
                        memcpy(data + 16, ctx->mode2Subheaders + sectorAddress * 8, 8);

                        if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form1Ok)
                        {
                            memcpy(data + 24, bareData, 2048);
                            aaruf_ecc_cd_reconstruct(ctx->eccCdContext, data, CdMode2Form1);
                        }
                        else if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form2Ok ||
                                (ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form2NoCrc)
                        {
                            memcpy(data + 24, bareData, 2324);
                            if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form2Ok)
                                aaruf_ecc_cd_reconstruct(ctx->eccCdContext, data, CdMode2Form2);
                        }
                        else if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            // Mode 2 where ECC failed
                            memcpy(data + 24, bareData, 2328);
                    }
                    else if(ctx->mode2Subheaders != NULL)
                    {
                        memcpy(data + 16, ctx->mode2Subheaders + sectorAddress * 8, 8);
                        memcpy(data + 24, bareData, 2328);
                    }
                    else
                        memcpy(data + 16, bareData, 2336);

                    return res;
                default:
                    FATAL("Invalid track type %d", trk.type);
                    free(bareData);

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INVALID_TRACK_FORMAT");
                    return AARUF_ERROR_INVALID_TRACK_FORMAT;
            }
        case BlockMedia:
            switch(ctx->imageInfo.MediaType)
            {
                case AppleFileWare:
                case AppleProfile:
                case AppleSonySS:
                case AppleSonyDS:
                case AppleWidget:
                case PriamDataTower:
                    if(ctx->sectorSubchannel == NULL) return aaruf_read_sector(context, sectorAddress, data, length);

                    switch(ctx->imageInfo.MediaType)
                    {
                        case AppleFileWare:
                        case AppleProfile:
                        case AppleWidget:
                            tagLength = 20;
                            break;
                        case AppleSonySS:
                        case AppleSonyDS:
                            tagLength = 12;
                            break;
                        case PriamDataTower:
                            tagLength = 24;
                            break;
                        default:
                            FATAL("Unsupported media type %d", ctx->imageInfo.MediaType);

                            TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                            return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
                    }

                    bareLength = 512;

                    if(*length < tagLength + bareLength || data == NULL)
                    {
                        *length = tagLength + bareLength;
                        FATAL("Buffer too small for sector, required %u bytes", *length);

                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_BUFFER_TOO_SMALL");
                        return AARUF_ERROR_BUFFER_TOO_SMALL;
                    }

                    TRACE("Allocating memory for bare data of size %u bytes", bareLength);
                    bareData = malloc(bareLength);

                    if(bareData == NULL)
                    {
                        FATAL("Could not allocate memory for bare data");

                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                    }

                    res = aaruf_read_sector(context, sectorAddress, bareData, &bareLength);

                    if(bareLength != 512)
                    {
                        FATAL("Bare data length is %u, expected 512", bareLength);
                        free(bareData);

                        TRACE("Exiting aaruf_read_sector_long() = %d", res);
                        return res;
                    }

                    memcpy(data, ctx->sectorSubchannel + sectorAddress * tagLength, tagLength);
                    memcpy(data, bareData, 512);

                    free(bareData);

                    TRACE("Exiting aaruf_read_sector_long() = %d", res);
                    return res;
                default:
                    FATAL("Incorrect media type %d for long sector reading", ctx->imageInfo.MediaType);

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                    return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }
        default:
            FATAL("Incorrect media type %d for long sector reading", ctx->imageInfo.MediaType);

            TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
            return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
    }
}
