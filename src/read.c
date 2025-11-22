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
 * information, lead-in/lead-out data, manufacturer-specific information, or other
 * medium-specific metadata. This function uses a hash table lookup for efficient
 * tag retrieval and supports buffer size querying when data pointer is NULL.
 *
 * @param context Pointer to the aaruformat context.
 * @param data Pointer to the buffer to store the tag data. Can be NULL to query tag length.
 * @param tag Tag identifier to read (specific to media type and format).
 * @param length Pointer to the length of the buffer on input; updated with actual tag length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully read the media tag. This is returned when:
 *         - The context is valid and properly initialized
 *         - The requested media tag exists in the image's media tag hash table
 *         - The provided buffer is large enough to contain the tag data
 *         - The tag data is successfully copied to the output buffer
 *         - The length parameter is updated with the actual tag data length
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_MEDIA_TAG_NOT_PRESENT (-11) The requested media tag does not exist. This occurs when:
 *         - The tag identifier is not found in the image's media tag hash table
 *         - The image was created without the requested metadata
 *         - The tag identifier is not supported for this media type
 *         - The length parameter is set to 0 when this error is returned
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer is insufficient. This occurs when:
 *         - The data parameter is NULL (used for length querying)
 *         - The buffer length (*length) is smaller than the required tag data length
 *         - The length parameter is updated with the required size for retry
 *
 * @note Buffer Size Querying:
 *       - Pass data as NULL to query the required buffer size without reading data
 *       - The length parameter will be updated with the required size
 *       - This allows proper buffer allocation before the actual read operation
 *
 * @note Media Tag Types:
 *       - Tags are media-type specific (optical disc, floppy disk, hard disk, etc.)
 *       - Common tags include TOC data, lead-in/out, manufacturer data, defect lists
 *       - Tag availability depends on what was preserved during the imaging process
 *
 * @note Hash Table Lookup:
 *       - Uses efficient O(1) hash table lookup for tag retrieval
 *       - Tag identifiers are integer values specific to the media format
 *       - Hash table is populated during image opening from indexed metadata blocks
 *
 * @warning The function performs a direct memory copy operation. Ensure the output
 *          buffer has sufficient space to prevent buffer overflows.
 *
 * @warning Media tag data is stored as-is from the original medium. No format
 *          conversion or validation is performed on the tag content.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_media_tag(void *context, uint8_t *data, const int32_t tag, uint32_t *length)
{
    const uint32_t initial_length = length == NULL ? 0U : *length;

    TRACE("Entering aaruf_read_media_tag(%p, %p, %d, %u)", context, data, tag, initial_length);

    mediaTagEntry *item;

    if(context == NULL)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_read_media_tag() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(length == NULL)
    {
        FATAL("Invalid length pointer");
        TRACE("Exiting aaruf_read_media_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    const aaruformat_context *ctx = context;

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
 * or ECC/EDC information. It handles block-based deduplication, compression
 * (LZMA/FLAC), and caching for optimal performance. The function supports both
 * DDT v1 and v2 formats for sector-to-block mapping.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address The logical sector address to read from.
 * @param negative Indicates if the sector address is negative.
 * @param data Pointer to buffer where sector data will be stored. Can be NULL to query length.
 * @param length Pointer to variable containing buffer size on input, actual data length on output.
 * @param sector_status Pointer to variable that will receive the sector status flags. Must not be NULL.
 *                      The status indicates the condition of the sector data, such as whether it contains
 *                      errors, was not dumped, or has other quality indicators from the imaging process.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully read the sector data. This is returned when:
 *         - The sector data is successfully retrieved from cache or decompressed from storage
 *         - Block header and data are successfully read and processed
 *         - Decompression (if needed) completes successfully
 *         - The sector data is copied to the output buffer
 *         - The length parameter is updated with the actual sector size
 *
 * @retval AARUF_STATUS_SECTOR_NOT_DUMPED (1) The sector was not dumped during imaging. This occurs when:
 *         - The DDT entry indicates the sector status is SectorStatusNotDumped
 *         - The original imaging process skipped this sector due to read errors
 *         - The length parameter is set to the expected sector size for buffer allocation
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_SECTOR_OUT_OF_BOUNDS (-5) The sector address exceeds image bounds. This occurs when:
 *         - sector_address is greater than or equal to ctx->imageInfo.Sectors
 *         - Attempting to read beyond the logical extent of the imaged medium
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer is insufficient. This occurs when:
 *         - The data parameter is NULL (used for length querying)
 *         - The buffer length (*length) is smaller than the block's sector size
 *         - The length parameter is updated with the required size for retry
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - Cannot allocate memory for block header (if not cached)
 *         - Cannot allocate memory for uncompressed block data
 *         - Cannot allocate memory for compressed data buffer (LZMA/FLAC)
 *         - Cannot allocate memory for decompressed block buffer
 *
 * @retval AARUF_ERROR_CANNOT_READ_HEADER (-6) Block header reading failed. This occurs when:
 *         - Cannot read the complete BlockHeader structure from the image stream
 *         - File I/O errors prevent reading header data at the calculated block offset
 *         - Image file corruption or truncation
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Block data reading failed. This occurs when:
 *         - Cannot read uncompressed block data from the image stream
 *         - Cannot read LZMA properties from compressed blocks
 *         - Cannot read compressed data from LZMA or FLAC blocks
 *         - File I/O errors during block data access
 *
 * @retval AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) Decompression failed. This occurs when:
 *         - LZMA decoder returns a non-zero error code during decompression
 *         - FLAC decoder fails to decompress audio data properly
 *         - Decompressed data size doesn't match the expected block length
 *         - Compression algorithm encounters corrupted or invalid compressed data
 *
 * @retval AARUF_ERROR_UNSUPPORTED_COMPRESSION (-8) Unsupported compression algorithm. This occurs when:
 *         - The block header specifies a compression type not supported by this library
 *         - Future compression algorithms not implemented in this version
 *
 * @retval Other error codes may be propagated from DDT decoding functions:
 *         - From decode_ddt_entry_v1(): AARUF_ERROR_NOT_AARUFORMAT (-1)
 *         - From decode_ddt_entry_v2(): AARUF_ERROR_NOT_AARUFORMAT (-1), AARUF_ERROR_CANNOT_READ_BLOCK (-7),
 *           AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17), AARUF_ERROR_INVALID_BLOCK_CRC (-18)
 *
 * @note DDT Processing:
 *       - Automatically selects DDT v1 or v2 decoding based on ctx->ddtVersion
 *       - DDT decoding provides sector offset within block and block file offset
 *       - Handles deduplication where multiple sectors may reference the same block
 *
 * @note Caching Mechanism:
 *       - Block headers are cached for performance (ctx->blockHeaderCache)
 *       - Decompressed block data is cached to avoid repeated decompression (ctx->blockCache)
 *       - Cache hits eliminate file I/O and decompression overhead
 *       - Cache sizes are determined during context initialization
 *
 * @note Compression Support:
 *       - None: Direct reading of uncompressed block data
 *       - LZMA: Industry-standard compression with properties header
 *       - FLAC: Audio-optimized compression for CD audio blocks
 *       - Each compression type has specific decompression requirements and error conditions
 *
 * @note Buffer Size Querying:
 *       - Pass data as NULL to query the required buffer size without reading data
 *       - The length parameter will be updated with the block's sector size
 *       - Useful for dynamic buffer allocation before the actual read operation
 *
 * @warning This function reads only user data. For complete sector data including
 *          metadata and ECC/EDC information, use aaruf_read_sector_long().
 *
 * @warning Memory allocated for caching is managed by the context. Do not free
 *          cached block data as it may be reused by subsequent operations.
 *
 * @warning Sector addresses are zero-based. The maximum valid address is
 *          ctx->imageInfo.Sectors - 1.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector(void *context, const uint64_t sector_address, bool negative,
                                                uint8_t *data, uint32_t *length, uint8_t *sector_status)
{
    const uint32_t initial_length = length == NULL ? 0U : *length;

    TRACE("Entering aaruf_read_sector(%p, %" PRIu64 ", %d, %p, %u)", context, sector_address, negative, data,
          initial_length);

    aaruformat_context *ctx          = NULL;
    uint64_t            offset       = 0;
    uint64_t            block_offset = 0;
    BlockHeader        *block_header = NULL;
    uint8_t            *block        = NULL;
    size_t              read_bytes   = 0;
    uint8_t             lzma_properties[LZMA_PROPERTIES_LENGTH];
    size_t              lzma_size = 0;
    uint8_t            *cmp_data  = NULL;
    int                 error_no  = 0;
    *sector_status                = SectorStatusNotDumped;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    if(length == NULL)
    {
        FATAL("Invalid length pointer");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(negative && sector_address > ctx->user_data_ddt_header.negative - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(!negative && sector_address > ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(ctx->ddt_version == 1)
    {
        if(negative)
        {
            FATAL("Negative sector addresses not supported in this image");
            return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
        }

        error_no = decode_ddt_entry_v1(ctx, sector_address, &offset, &block_offset, sector_status);
    }
    else if(ctx->ddt_version == 2)
        error_no = decode_ddt_entry_v2(ctx, sector_address, negative, &offset, &block_offset, sector_status);

    if(error_no != AARUF_STATUS_OK)
    {
        FATAL("Error %d decoding DDT entry", error_no);

        TRACE("Exiting aaruf_read_sector() = %d", error_no);
        return error_no;
    }

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(*sector_status == SectorStatusNotDumped)
    {
        *length = ctx->image_info.SectorSize;

        TRACE("Exiting aaruf_read_sector() = AARUF_STATUS_SECTOR_NOT_DUMPED");
        return AARUF_STATUS_SECTOR_NOT_DUMPED;
    }

    // Check if block header is cached
    TRACE("Checking if block header is cached");
    block_header = find_in_cache_uint64(&ctx->block_header_cache, block_offset);

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
        if(fseek(ctx->imageStream, block_offset, SEEK_SET) != 0)
        {
            FATAL("Could not seek to block header");
            free(block_header);

            TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_READ_HEADER");
            return AARUF_ERROR_CANNOT_READ_HEADER;
        }

        read_bytes = fread(block_header, 1, sizeof(BlockHeader), ctx->imageStream);

        if(read_bytes != sizeof(BlockHeader))
        {
            FATAL("Error reading block header");
            free(block_header);

            TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_READ_HEADER");
            return AARUF_ERROR_CANNOT_READ_HEADER;
        }

        TRACE("Adding block header to cache");
        add_to_cache_uint64(&ctx->block_header_cache, block_offset, block_header);
    }
    else if(fseek(ctx->imageStream, block_offset + sizeof(BlockHeader), SEEK_SET) != 0)
    {
        FATAL("Could not seek past cached block header");

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_READ_HEADER");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(data == NULL || *length < block_header->sectorSize)
    {
        TRACE("Buffer too small for sector, required %u bytes", block_header->sectorSize);
        *length = block_header->sectorSize;

        TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Check if block is cached
    TRACE("Checking if block is cached");
    block = find_in_cache_uint64(&ctx->block_cache, block_offset);

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
            if(block_header->cmpLength <= LZMA_PROPERTIES_LENGTH || block_header->length == 0)
            {
                FATAL("Invalid LZMA block lengths (cmpLength=%u, length=%u)", block_header->cmpLength,
                      block_header->length);

                TRACE("Exiting aaruf_read_sector() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

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
    add_to_cache_uint64(&ctx->block_cache, block_offset, block);

    memcpy(data, block + offset * block_header->sectorSize, block_header->sectorSize);
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
 * It validates the media type, locates the specified track by sequence number,
 * calculates the absolute sector address, and delegates to aaruf_read_sector().
 *
 * @param context Pointer to the aaruformat context.
 * @param data Pointer to buffer where sector data will be stored.
 * @param sector_address The sector address relative to the track start.
 * @param length Pointer to variable containing buffer size on input, actual data length on output.
 * @param track The track sequence number to read from.
 * @param sector_status Pointer to variable that will receive the sector status flags. Must not be NULL.
 *                      The status indicates the condition of the sector data, such as whether it contains
 *                      errors, was not dumped, or has other quality indicators from the imaging process.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully read the sector data from the specified track. This is returned when:
 *         - The context is valid and the media type is OpticalDisc
 *         - The specified track sequence number exists in the data tracks array
 *         - The underlying aaruf_read_sector() call succeeds
 *         - The sector data is successfully retrieved and copied to the output buffer
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_INCORRECT_MEDIA_TYPE (-5) The media is not an optical disc. This occurs when:
 *         - ctx->imageInfo.XmlMediaType is not OpticalDisc
 *         - This function is only applicable to CD, DVD, BD, and other optical disc formats
 *
 * @retval AARUF_ERROR_TRACK_NOT_FOUND (-13) The specified track does not exist. This occurs when:
 *         - No track in ctx->dataTracks[] has a sequence number matching the requested track
 *         - The track may not contain data or may not have been imaged
 *         - Only data tracks are searched; audio-only tracks are not included
 *
 * @retval All other error codes from aaruf_read_sector() may be returned:
 *         - AARUF_STATUS_SECTOR_NOT_DUMPED (1) - Sector was not dumped during imaging
 *         - AARUF_ERROR_SECTOR_OUT_OF_BOUNDS (-5) - Calculated absolute sector address exceeds image bounds
 *         - AARUF_ERROR_BUFFER_TOO_SMALL (-10) - Data buffer is NULL or insufficient size
 *         - AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) - Memory allocation fails during sector reading
 *         - AARUF_ERROR_CANNOT_READ_HEADER (-6) - Block header cannot be read from image stream
 *         - AARUF_ERROR_CANNOT_READ_BLOCK (-7) - Block data cannot be read from image stream
 *         - AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) - LZMA or FLAC decompression fails
 *         - AARUF_ERROR_UNSUPPORTED_COMPRESSION (-8) - Block uses unsupported compression
 *
 * @note Track-Relative Addressing:
 *       - The sector_address parameter is relative to the start of the specified track
 *       - The function calculates the absolute sector address as: track.start + sector_address
 *       - Track boundaries are defined by track.start and track.end values
 *
 * @note Data Track Filtering:
 *       - Only tracks in the ctx->dataTracks[] array are searched
 *       - Audio-only tracks without data content are not included in this search
 *       - The track sequence number is the logical track number from the disc TOC
 *
 * @note Function Delegation:
 *       - This function is a wrapper that performs track validation and address calculation
 *       - The actual sector reading is performed by aaruf_read_sector()
 *       - All caching, decompression, and DDT processing occurs in the underlying function
 *
 * @warning This function is only applicable to optical disc media types.
 *          Attempting to use it with block media will result in AARUF_ERROR_INCORRECT_MEDIA_TYPE.
 *
 * @warning The sector_address is relative to the track start, not the disc start.
 *          Ensure correct address calculation when working with multi-track discs.
 *
 * @warning Track sequence numbers may not be contiguous. Always verify track
 *          existence before assuming a track number is valid.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_track_sector(void *context, uint8_t *data, const uint64_t sector_address,
                                                      uint32_t *length, const uint8_t track, uint8_t *sector_status)
{
    const uint32_t initial_length = length == NULL ? 0U : *length;

    TRACE("Entering aaruf_read_track_sector(%p, %p, %" PRIu64 ", %u, %d)", context, data, sector_address,
          initial_length, track);

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    if(length == NULL)
    {
        FATAL("Invalid length pointer");

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->image_info.MetadataMediaType != OpticalDisc)
    {
        FATAL("Incorrect media type %d, expected OpticalDisc", ctx->imageInfo.XmlMediaType);

        TRACE("Exiting aaruf_read_track_sector() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
        return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
    }

    for(int i = 0; i < ctx->number_of_data_tracks; i++)
        if(ctx->data_tracks[i].sequence == track)
            return aaruf_read_sector(context, ctx->data_tracks[i].start + sector_address, false, data, length,
                                     sector_status);

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
 * media with tags, this includes both the user data and tag information. The function
 * handles complex sector reconstruction including ECC correction and format-specific
 * metadata assembly.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address The logical sector address to read from.
 * @param negative Indicates if the sector address is negative.
 * @param data Pointer to buffer where complete sector data will be stored. Can be NULL to query length.
 * @param length Pointer to variable containing buffer size on input, actual data length on output.
 * @param sector_status Pointer to variable that will receive the sector status flags. Must not be NULL.
 *                      The status indicates the condition of the sector data, such as whether it contains
 *                      errors, was not dumped, or has other quality indicators from the imaging process.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully read the complete sector with metadata. This is returned when:
 *         - The sector data is successfully retrieved and reconstructed with all metadata
 *         - For optical discs: sync, header, user data, and ECC/EDC are assembled into 2352 bytes
 *         - For block media: user data and tags are combined according to media type
 *         - ECC reconstruction (if applicable) completes successfully
 *         - All required metadata (prefix/suffix, subheaders, etc.) is available and applied
 *
 * @retval AARUF_STATUS_SECTOR_NOT_DUMPED (1) The sector or metadata was not dumped. This can occur when:
 *         - The underlying sector data was not dumped during imaging
 *         - CD prefix or suffix metadata indicates NotDumped status for the sector
 *         - ECC reconstruction cannot be performed due to missing correction data
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_INCORRECT_MEDIA_TYPE (-5) The media type doesn't support long sector reading. This occurs when:
 *         - ctx->imageInfo.XmlMediaType is not OpticalDisc or BlockMedia
 *         - For BlockMedia: the specific media type is not supported (not Apple, Priam, etc.)
 *         - Media types that don't have extended sector formats
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The buffer is insufficient for complete sector data. This occurs when:
 *         - The data parameter is NULL (used for length querying)
 *         - For optical discs: buffer length is less than 2352 bytes
 *         - For block media: buffer length is less than (user data size + tag size)
 *         - The length parameter is updated with the required size for retry
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - Cannot allocate memory for bare user data during optical disc processing
 *         - Cannot allocate memory for user data during block media processing
 *         - Memory allocation fails in underlying aaruf_read_sector() calls
 *
 * @retval AARUF_ERROR_TRACK_NOT_FOUND (-13) Cannot locate the sector's track. This occurs when:
 *         - For optical discs: the sector address doesn't fall within any data track boundaries
 *         - No track contains the specified sector address (address not in any track.start to track.end range)
 *         - The track list is empty or corrupted
 *
 * @retval AARUF_ERROR_INVALID_TRACK_FORMAT (-14) The track has an unsupported format. This occurs when:
 *         - The track type is not recognized (not Audio, Data, CdMode1, CdMode2*, etc.)
 *         - Internal track format validation fails
 *
 * @retval AARUF_ERROR_REACHED_UNREACHABLE_CODE (-15) Internal logic error. This occurs when:
 *         - Required metadata structures (sectorPrefix, sectorSuffix, etc.) are unexpectedly NULL
 *         - Control flow reaches states that should be impossible with valid image data
 *         - Indicates potential image corruption or library bug
 *
 * @retval All error codes from aaruf_read_sector() may be propagated:
 *         - AARUF_ERROR_SECTOR_OUT_OF_BOUNDS (-5) - Calculated sector address exceeds image bounds
 *         - AARUF_ERROR_CANNOT_READ_HEADER (-6) - Block header cannot be read
 *         - AARUF_ERROR_CANNOT_READ_BLOCK (-7) - Block data cannot be read
 *         - AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) - Decompression fails
 *         - AARUF_ERROR_UNSUPPORTED_COMPRESSION (-8) - Compression algorithm not supported
 *
 * @note Optical Disc Sector Reconstruction:
 *       - Creates full 2352-byte sectors from separate user data, sync, header, and ECC/EDC components
 *       - Supports different CD modes: Mode 1, Mode 2 Form 1, Mode 2 Form 2, Mode 2 Formless
 *       - Handles ECC correction using stored correction data or reconstructed ECC
 *       - Uses prefix/suffix DDTs to determine correction strategy for each sector
 *
 * @note Block Media Tag Assembly:
 *       - Combines user data (typically 512 bytes) with media-specific tags
 *       - Tag sizes vary by media type: Apple (12-20 bytes), Priam (24 bytes)
 *       - Tags contain manufacturer-specific information like spare sector mapping
 *
 * @note ECC Reconstruction Modes:
 *       - Correct: Reconstructs ECC/EDC from user data (no stored correction data needed)
 *       - NotDumped: Indicates the metadata portion was not successfully read during imaging
 *       - Stored: Uses pre-computed correction data stored separately in the image
 *
 * @note Buffer Size Requirements:
 *       - Optical discs: Always 2352 bytes (full raw sector)
 *       - Block media: User data size + tag size (varies by media type)
 *       - Pass data as NULL to query the exact required buffer size
 *
 * @warning For optical discs, this function requires track information to be available.
 *          Images without track data may not support long sector reading.
 *
 * @warning The function performs complex sector reconstruction. Corrupted metadata
 *          or missing correction data may result in incorrect sector assembly.
 *
 * @warning Not all AaruFormat images contain the metadata necessary for long sector
 *          reading. Some images may only support basic sector reading via aaruf_read_sector().
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_long(void *context, const uint64_t sector_address, bool negative,
                                                     uint8_t *data, uint32_t *length, uint8_t *sector_status)
{
    const uint32_t initial_length = length == NULL ? 0U : *length;

    TRACE("Entering aaruf_read_sector_long(%p, %" PRIu64 ", %d, %p, %u)", context, sector_address, data,
          initial_length);

    const aaruformat_context *ctx         = NULL;
    uint32_t                  bare_length = 0;
    uint32_t                  tag_length  = 0;
    uint8_t                  *bare_data   = NULL;
    int32_t                   res         = 0;
    int32_t                   query_status;
    TrackEntry                trk;
    int                       i         = 0;
    bool                      trk_found = false;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    if(length == NULL)
    {
        FATAL("Invalid length pointer");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(negative && sector_address > ctx->user_data_ddt_header.negative - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(!negative && sector_address > ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    uint64_t corrected_sector_address = sector_address;

    // Calculate positive or negative sector
    if(negative)
        corrected_sector_address = ctx->user_data_ddt_header.negative - sector_address;
    else
        corrected_sector_address += ctx->user_data_ddt_header.negative;

    switch(ctx->image_info.MetadataMediaType)
    {
        case OpticalDisc:
            if(ctx->image_info.MediaType == DVDROM || ctx->image_info.MediaType == PS2DVD ||
               ctx->image_info.MediaType == SACD || ctx->image_info.MediaType == PS3DVD ||
               ctx->image_info.MediaType == DVDR || ctx->image_info.MediaType == DVDRW ||
               ctx->image_info.MediaType == DVDPR || ctx->image_info.MediaType == DVDPRW ||
               ctx->image_info.MediaType == DVDPRWDL || ctx->image_info.MediaType == DVDRDL ||
               ctx->image_info.MediaType == DVDPRDL || ctx->image_info.MediaType == DVDRAM ||
               ctx->image_info.MediaType == DVDRWDL || ctx->image_info.MediaType == DVDDownload ||
               ctx->image_info.MediaType == Nuon)
            {
                if(ctx->sector_id == NULL || ctx->sector_ied == NULL || ctx->sector_cpr_mai == NULL ||
                   ctx->sector_edc == NULL)
                    return aaruf_read_sector(context, sector_address, negative, data, length, sector_status);

                if(*length < 2064 || data == NULL)
                {
                    *length = 2064;
                    FATAL("Buffer too small for sector, required %u bytes", *length);

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_BUFFER_TOO_SMALL");
                    return AARUF_ERROR_BUFFER_TOO_SMALL;
                }

                bare_length  = 0;
                query_status = aaruf_read_sector(context, sector_address, negative, NULL, &bare_length, sector_status);

                if(query_status != AARUF_ERROR_BUFFER_TOO_SMALL && query_status != AARUF_STATUS_OK)
                {
                    TRACE("Exiting aaruf_read_sector_long() = %d", query_status);
                    return query_status;
                }

                if(bare_length == 0)
                {
                    FATAL("Invalid bare sector length (0)");

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                    return AARUF_ERROR_INCORRECT_DATA_SIZE;
                }

                TRACE("Allocating memory for bare data");
                bare_data = (uint8_t *)malloc(bare_length);

                if(bare_data == NULL)
                {
                    FATAL("Could not allocate memory for bare data");

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                    return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                }

                res = aaruf_read_sector(context, sector_address, negative, bare_data, &bare_length, sector_status);

                if(res != AARUF_STATUS_OK)
                {
                    *length = 2064;
                    free(bare_data);

                    TRACE("Exiting aaruf_read_sector_long() = %d", res);
                    return res;
                }

                memcpy(data, ctx->sector_id + corrected_sector_address * 4, 4);
                memcpy(data + 4, ctx->sector_ied + corrected_sector_address * 2, 2);
                memcpy(data + 6, ctx->sector_cpr_mai + corrected_sector_address * 6, 6);
                memcpy(data + 12, bare_data, 2048);
                memcpy(data + 2060, ctx->sector_edc + corrected_sector_address * 4, 4);

                *length = 2064;

                free(bare_data);
                return AARUF_STATUS_OK;
            }

            if(*length < 2352 || data == NULL)
            {
                *length = 2352;
                FATAL("Buffer too small for sector, required %u bytes", *length);

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_BUFFER_TOO_SMALL");
                return AARUF_ERROR_BUFFER_TOO_SMALL;
            }

            if((ctx->sector_suffix == NULL || ctx->sector_prefix == NULL) &&
               (ctx->sector_suffix_ddt == NULL || ctx->sector_prefix_ddt == NULL) &&
               (ctx->sector_suffix_ddt2 == NULL || ctx->sector_prefix_ddt2 == NULL))
                return aaruf_read_sector(context, sector_address, negative, data, length, sector_status);

            bare_length  = 0;
            query_status = aaruf_read_sector(context, sector_address, negative, NULL, &bare_length, sector_status);

            if(query_status != AARUF_ERROR_BUFFER_TOO_SMALL && query_status != AARUF_STATUS_OK)
            {
                TRACE("Exiting aaruf_read_sector_long() = %d", query_status);
                return query_status;
            }

            if(bare_length == 0)
            {
                FATAL("Invalid bare sector length (0)");

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            TRACE("Allocating memory for bare data");
            bare_data = (uint8_t *)malloc(bare_length);

            if(bare_data == NULL)
            {
                FATAL("Could not allocate memory for bare data");

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            res = aaruf_read_sector(context, sector_address, negative, bare_data, &bare_length, sector_status);

            if(res != AARUF_STATUS_OK)
            {
                free(bare_data);

                TRACE("Exiting aaruf_read_sector_long() = %d", res);
                return res;
            }

            trk_found = false;

            for(i = 0; i < ctx->number_of_data_tracks; i++)
                if(sector_address >= ctx->data_tracks[i].start && sector_address <= ctx->data_tracks[i].end)
                {
                    trk_found = true;
                    trk       = ctx->data_tracks[i];
                    break;
                }

            if(!trk_found)
            {
                FATAL("Track not found");
                free(bare_data);

                TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_TRACK_NOT_FOUND");
                return AARUF_ERROR_TRACK_NOT_FOUND;
            }

            switch(trk.type)
            {
                case Audio:
                case Data:
                    memcpy(data, bare_data, bare_length);
                    *length = bare_length;
                    free(bare_data);
                    return res;
                case CdMode1:
                    memcpy(data + 16, bare_data, 2048);

                    if(ctx->sector_prefix_ddt2 != NULL)
                    {
                        const uint64_t prefix_ddt_entry = ctx->sector_prefix_ddt2[corrected_sector_address];
                        const uint32_t prefix_status    = prefix_ddt_entry >> 60;
                        const uint64_t prefix_index     = prefix_ddt_entry & 0x0FFFFFFFFFFFFFFF;

                        if(prefix_status == SectorStatusMode1Correct)
                        {
                            aaruf_ecc_cd_reconstruct_prefix(data, trk.type, sector_address);
                            res = AARUF_STATUS_OK;
                        }
                        else if(prefix_status == SectorStatusNotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data, ctx->sector_prefix + prefix_index * 16, 16);
                    }
                    else if(ctx->sector_prefix != NULL)
                        memcpy(data, ctx->sector_prefix + corrected_sector_address * 16, 16);
                    else if(ctx->sector_prefix_ddt != NULL)
                    {
                        if((ctx->sector_prefix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Correct)
                        {
                            aaruf_ecc_cd_reconstruct_prefix(data, trk.type, sector_address);
                            res = AARUF_STATUS_OK;
                        }
                        else if((ctx->sector_prefix_ddt[corrected_sector_address] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data,
                                   ctx->sector_prefix_corrected +
                                       ((ctx->sector_prefix_ddt[corrected_sector_address] & CD_DFIX_MASK) - 1) * 16,
                                   16);
                    }
                    else
                    {
                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_REACHED_UNREACHABLE_CODE");
                        free(bare_data);
                        return AARUF_ERROR_REACHED_UNREACHABLE_CODE;
                    }

                    if(res != AARUF_STATUS_OK)
                    {
                        *length = 2352;
                        free(bare_data);
                        return res;
                    }

                    if(ctx->sector_suffix_ddt2 != NULL)
                    {
                        const uint64_t suffix_ddt_entry = ctx->sector_suffix_ddt2[corrected_sector_address];
                        const uint64_t suffix_status    = suffix_ddt_entry >> 60;
                        const uint64_t suffix_index     = suffix_ddt_entry & 0x0FFFFFFFFFFFFFFF;

                        if(suffix_status == SectorStatusMode1Correct)
                        {
                            aaruf_ecc_cd_reconstruct(ctx->ecc_cd_context, data, trk.type);
                            res = AARUF_STATUS_OK;
                        }
                        else if(suffix_status == SectorStatusNotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data + 2064, ctx->sector_suffix + suffix_index * 288, 288);
                    }
                    else if(ctx->sector_suffix != NULL)
                        memcpy(data + 2064, ctx->sector_suffix + corrected_sector_address * 288, 288);
                    else if(ctx->sector_suffix_ddt != NULL)
                    {
                        if((ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Correct)
                        {
                            aaruf_ecc_cd_reconstruct(ctx->ecc_cd_context, data, trk.type);
                            res = AARUF_STATUS_OK;
                        }
                        else if((ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data + 2064,
                                   ctx->sector_suffix_corrected +
                                       ((ctx->sector_suffix_ddt[corrected_sector_address] & CD_DFIX_MASK) - 1) * 288,
                                   288);
                    }
                    else
                    {
                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_REACHED_UNREACHABLE_CODE");
                        free(bare_data);
                        return AARUF_ERROR_REACHED_UNREACHABLE_CODE;
                    }

                    *length = 2352;
                    free(bare_data);
                    return res;
                case CdMode2Formless:
                case CdMode2Form1:
                case CdMode2Form2:
                    if(ctx->sector_prefix_ddt2 != NULL)
                    {
                        const uint64_t prefix_ddt_entry = ctx->sector_prefix_ddt2[corrected_sector_address];
                        const uint64_t prefix_status    = prefix_ddt_entry >> 60;
                        const uint64_t prefix_index     = prefix_ddt_entry & 0x0FFFFFFFFFFFFFFF;

                        if(prefix_status == SectorStatusMode2Form1Ok || prefix_status == SectorStatusMode2Form2Ok)
                        {
                            aaruf_ecc_cd_reconstruct_prefix(data, trk.type, sector_address);
                            res = AARUF_STATUS_OK;
                        }
                        else if(prefix_status == SectorStatusNotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data, ctx->sector_prefix + prefix_index * 16, 16);
                    }
                    else if(ctx->sector_prefix != NULL)
                        memcpy(data, ctx->sector_prefix + corrected_sector_address * 16, 16);
                    else if(ctx->sector_prefix_ddt != NULL)
                    {
                        if((ctx->sector_prefix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Correct)
                        {
                            aaruf_ecc_cd_reconstruct_prefix(data, trk.type, sector_address);
                            res = AARUF_STATUS_OK;
                        }
                        else if((ctx->sector_prefix_ddt[corrected_sector_address] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            memcpy(data,
                                   ctx->sector_prefix_corrected +
                                       ((ctx->sector_prefix_ddt[corrected_sector_address] & CD_DFIX_MASK) - 1) * 16,
                                   16);
                    }
                    else
                    {
                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_REACHED_UNREACHABLE_CODE");
                        free(bare_data);
                        return AARUF_ERROR_REACHED_UNREACHABLE_CODE;
                    }

                    if(res != AARUF_STATUS_OK)
                    {
                        *length = 2352;
                        free(bare_data);
                        return res;
                    }

                    if(ctx->mode2_subheaders != NULL && ctx->sector_suffix_ddt2 != NULL)
                    {
                        memcpy(data + 16, ctx->mode2_subheaders + corrected_sector_address * 8, 8);
                        const uint64_t suffix_ddt_entry = ctx->sector_suffix_ddt2[corrected_sector_address];
                        const uint64_t suffix_status    = suffix_ddt_entry >> 60;
                        const uint64_t suffix_index     = suffix_ddt_entry & 0x0FFFFFFFFFFFFFFF;

                        if(suffix_status == SectorStatusMode2Form1Ok)
                        {
                            memcpy(data + 24, bare_data, 2048);
                            aaruf_ecc_cd_reconstruct(ctx->ecc_cd_context, data, CdMode2Form1);
                        }
                        else if(suffix_status == SectorStatusMode2Form2Ok ||
                                suffix_status == SectorStatusMode2Form2NoCrc)
                        {
                            memcpy(data + 24, bare_data, 2324);
                            if(suffix_status == SectorStatusMode2Form2Ok)
                                aaruf_ecc_cd_reconstruct(ctx->ecc_cd_context, data, CdMode2Form2);
                        }
                        else if(suffix_status == SectorStatusNotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            // Mode 2 where ECC failed
                            memcpy(data + 24, bare_data, 2328);
                    }
                    else if(ctx->mode2_subheaders != NULL && ctx->sector_suffix_ddt != NULL)
                    {
                        memcpy(data + 16, ctx->mode2_subheaders + corrected_sector_address * 8, 8);

                        if((ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Mode2Form1Ok)
                        {
                            memcpy(data + 24, bare_data, 2048);
                            aaruf_ecc_cd_reconstruct(ctx->ecc_cd_context, data, CdMode2Form1);
                        }
                        else if((ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Mode2Form2Ok ||
                                (ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Mode2Form2NoCrc)
                        {
                            memcpy(data + 24, bare_data, 2324);
                            if((ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == Mode2Form2Ok)
                                aaruf_ecc_cd_reconstruct(ctx->ecc_cd_context, data, CdMode2Form2);
                        }
                        else if((ctx->sector_suffix_ddt[corrected_sector_address] & CD_XFIX_MASK) == NotDumped)
                            res = AARUF_STATUS_SECTOR_NOT_DUMPED;
                        else
                            // Mode 2 where ECC failed
                            memcpy(data + 24, bare_data, 2328);
                    }
                    else if(ctx->mode2_subheaders != NULL)
                    {
                        memcpy(data + 16, ctx->mode2_subheaders + corrected_sector_address * 8, 8);
                        memcpy(data + 24, bare_data, 2328);
                    }
                    else
                        memcpy(data + 16, bare_data, 2336);

                    *length = 2352;
                    free(bare_data);
                    return res;
                default:
                    FATAL("Invalid track type %d", trk.type);
                    free(bare_data);

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INVALID_TRACK_FORMAT");
                    return AARUF_ERROR_INVALID_TRACK_FORMAT;
            }
        case BlockMedia:
            switch(ctx->image_info.MediaType)
            {
                case AppleFileWare:
                case AppleProfile:
                case AppleSonySS:
                case AppleSonyDS:
                case AppleWidget:
                case PriamDataTower:
                    if(ctx->sector_subchannel == NULL)
                        return aaruf_read_sector(context, sector_address, negative, data, length, sector_status);

                    switch(ctx->image_info.MediaType)
                    {
                        case AppleFileWare:
                        case AppleProfile:
                        case AppleWidget:
                            tag_length = 20;
                            break;
                        case AppleSonySS:
                        case AppleSonyDS:
                            tag_length = 12;
                            break;
                        case PriamDataTower:
                            tag_length = 24;
                            break;
                        default:
                            FATAL("Unsupported media type %d", ctx->imageInfo.MediaType);

                            TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                            return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
                    }

                    bare_length = 512;

                    if(*length < tag_length + bare_length || data == NULL)
                    {
                        *length = tag_length + bare_length;
                        FATAL("Buffer too small for sector, required %u bytes", *length);

                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_BUFFER_TOO_SMALL");
                        return AARUF_ERROR_BUFFER_TOO_SMALL;
                    }

                    TRACE("Allocating memory for bare data of size %u bytes", bare_length);
                    bare_data = malloc(bare_length);

                    if(bare_data == NULL)
                    {
                        FATAL("Could not allocate memory for bare data");

                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                    }

                    res = aaruf_read_sector(context, sector_address, negative, bare_data, &bare_length, sector_status);

                    if(res != AARUF_STATUS_OK)
                    {
                        free(bare_data);

                        TRACE("Exiting aaruf_read_sector_long() = %d", res);
                        return res;
                    }

                    if(bare_length != 512)
                    {
                        FATAL("Bare data length is %u, expected 512", bare_length);
                        free(bare_data);

                        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                        return AARUF_ERROR_INCORRECT_DATA_SIZE;
                    }

                    memcpy(data + bare_length, ctx->sector_subchannel + corrected_sector_address * tag_length,
                           tag_length);
                    memcpy(data, bare_data, bare_length);
                    *length = tag_length + bare_length;

                    free(bare_data);

                    TRACE("Exiting aaruf_read_sector_long() = AARUF_STATUS_OK");
                    return AARUF_STATUS_OK;
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

/**
 * @brief Reads a specific sector tag from the AaruFormat image.
 *
 * Reads sector-level metadata tags such as subchannel data, track information,
 * DVD sector metadata, or Apple/Priam proprietary tags from the specified sector.
 * Sector tags are ancillary data stored separately from the main user data and
 * provide format-specific metadata like track flags, ISRC codes, DVD encryption
 * information, or ECC/EDC data. This function validates tag type against media
 * type and ensures the tag data is present in the image before returning it.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address The logical sector address to read the tag from.
 * @param negative Indicates if the sector address is negative (pre-track area).
 * @param buffer Pointer to buffer where tag data will be stored. Can be NULL to query tag length.
 * @param length Pointer to variable containing buffer size on input, actual tag length on output.
 * @param tag Tag identifier specifying which sector tag to read (see SectorTagType enum).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully read the sector tag. This is returned when:
 *         - The context is valid and properly initialized
 *         - The requested tag type exists and is applicable to the media type
 *         - The tag data is present in the image for the specified sector
 *         - The provided buffer is large enough to contain the tag data
 *         - The tag data is successfully copied to the output buffer
 *         - The length parameter is updated with the actual tag data length
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_SECTOR_OUT_OF_BOUNDS (-5) The sector address exceeds image bounds. This occurs when:
 *         - negative is true and sector_address > ctx->userDataDdtHeader.negative - 1
 *         - negative is false and sector_address > ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow - 1
 *         - Attempting to read beyond the logical extent of the imaged medium
 *
 * @retval AARUF_ERROR_INCORRECT_MEDIA_TYPE (-12) Tag type incompatible with media type. This occurs when:
 *         - Optical disc tags (CdTrackFlags, CdTrackIsrc, CdSectorSubchannel, DvdSector*) requested from BlockMedia
 *         - Block media tags (AppleSonyTag, AppleProfileTag, PriamDataTowerTag) requested from OpticalDisc
 *         - Tag type is specific to a media format not present in the current image
 *
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE (-26) The provided buffer has incorrect size. This occurs when:
 *         - The buffer parameter is NULL (used for length querying - NOT an error, updates length and returns this)
 *         - The buffer length (*length) doesn't match the required tag data length
 *         - The length parameter is updated with the required exact size for retry
 *
 * @retval AARUF_ERROR_SECTOR_TAG_NOT_PRESENT (-16) The requested tag is not stored in the image. This occurs when:
 *         - The tag data was not captured during the imaging process
 *         - The storage pointer for the tag is NULL (e.g., ctx->sector_subchannel is NULL)
 *         - The imaging hardware/software did not support reading this tag type
 *         - The tag is optional and was not available on the source medium
 *
 * @retval AARUF_ERROR_TRACK_NOT_FOUND (-13) Track information not found for the sector. This occurs when:
 *         - Requesting CdTrackFlags or CdTrackIsrc for a sector not within any track boundaries
 *         - The sector address doesn't fall within any track's start/end range in ctx->trackEntries[]
 *         - Track metadata was not properly initialized or is corrupted
 *
 * @retval AARUF_ERROR_INVALID_TAG (-27) The tag identifier is not recognized or supported. This occurs when:
 *         - The tag parameter value doesn't match any known SectorTagType enum value
 *         - Future/unsupported tag types not implemented in this library version
 *
 * @note Supported Tag Types and Sizes:
 *       Optical Disc Tags (OpticalDisc media only):
 *       - CdTrackFlags (11): 1 byte - Track control flags (audio/data, copy permitted, pre-emphasis)
 *       - CdTrackIsrc (9): 12 bytes - International Standard Recording Code (no null terminator)
 *       - CdSectorSubchannel (71): 96 bytes - Raw P-W subchannel data
 *       - DvdSectorCprMai (81): 6 bytes - DVD Copyright Management Information (CPR_MAI)
 *       - DvdSectorInformation (16): 1 byte - DVD sector information byte from ID field
 *       - DvdSectorNumber (17): 3 bytes - DVD sector number from ID field
 *       - DvdSectorIed (84): 2 bytes - DVD ID Error Detection code
 *       - DvdSectorEdc (85): 4 bytes - DVD Error Detection Code
 *       - DvdDiscKeyDecrypted (80): 5 bytes - Decrypted DVD title key for the sector
 *
 *       Block Media Tags (BlockMedia only):
 *       - AppleSonyTag (73): 12 bytes - Apple Sony format 12-byte sector tag
 *       - AppleProfileTag (72): 20 bytes - Apple Profile format 20-byte sector tag
 *       - PriamDataTowerTag (74): 24 bytes - Priam DataTower format 24-byte sector tag
 *
 * @note Sector Address Correction:
 *       - The function automatically adjusts sector addresses based on the negative parameter
 *       - Negative sectors are adjusted: corrected = sector_address - ctx->userDataDdtHeader.negative
 *       - Positive sectors are adjusted: corrected = sector_address + ctx->userDataDdtHeader.negative
 *       - Corrected addresses are used for indexing into tag data arrays
 *
 * @note Track-Based Tags (CdTrackFlags, CdTrackIsrc):
 *       - These tags are per-track, not per-sector
 *       - Function searches ctx->trackEntries[] for track containing the sector
 *       - All sectors within a track return the same track-level metadata
 *       - Returns AARUF_ERROR_TRACK_NOT_FOUND if sector is outside all tracks
 *
 * @note DVD Sector Tags:
 *       - DVD sector tags are extracted from the DVD ID field or associated metadata
 *       - DvdSectorInformation and DvdSectorNumber are derived from ctx->sector_id array
 *       - Multiple tags may share the same underlying storage (e.g., ID field breakdown)
 *       - Presence depends on the DVD reading capabilities during imaging
 *
 * @note Buffer Size Querying:
 *       - Pass buffer as NULL to query the required buffer size without reading data
 *       - The length parameter will be updated with the exact required size
 *       - Returns AARUF_ERROR_INCORRECT_DATA_SIZE (not a fatal error in this context)
 *       - Allows proper buffer allocation before the actual read operation
 *
 * @note Tag Data Storage:
 *       - Tag data is stored in dedicated context arrays (ctx->sector_subchannel, ctx->sector_cpr_mai, etc.)
 *       - Each tag type has a specific array with fixed-size entries per sector
 *       - NULL storage pointer indicates tag was not captured/available
 *       - Tag data is indexed using the corrected sector address
 *
 * @warning The buffer must be exactly the required size for each tag type.
 *          Unlike aaruf_read_sector(), this function enforces strict size matching.
 *
 * @warning Tag availability is hardware and media dependent. Always check for
 *          AARUF_ERROR_SECTOR_TAG_NOT_PRESENT and handle gracefully.
 *
 * @warning Track-based tags (CdTrackFlags, CdTrackIsrc) return the same value
 *          for all sectors within a track. Do not assume per-sector uniqueness.
 *
 * @warning Some tags contain binary data without string termination (e.g., ISRC).
 *          Do not treat tag buffers as null-terminated strings without validation.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_tag(const void *context, const uint64_t sector_address,
                                                    const bool negative, uint8_t *buffer, uint32_t *length,
                                                    const int32_t tag)
{
    const uint32_t initial_length = length == NULL ? 0U : *length;

    TRACE("Entering aaruf_read_sector_tag(%p, %" PRIu64 ", %d, %p, %u, %d)", context, sector_address, negative, buffer,
          initial_length, tag);

    const aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    if(length == NULL)
    {
        FATAL("Invalid length pointer");

        TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(negative && sector_address > ctx->user_data_ddt_header.negative - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(!negative && sector_address > ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    uint64_t corrected_sector_address = sector_address;

    // Calculate positive or negative sector
    if(negative)
        corrected_sector_address = ctx->user_data_ddt_header.negative - sector_address;
    else
        corrected_sector_address += ctx->user_data_ddt_header.negative;

    switch(tag)
    {
        case CdTrackFlags:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 1 || buffer == NULL)
            {
                *length = 1;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            for(int i = 0; i < ctx->tracks_header.entries; i++)
                if(sector_address >= ctx->track_entries[i].start && sector_address <= ctx->track_entries[i].end)
                {
                    buffer[0] = ctx->track_entries[i].flags;
                    TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
                    return AARUF_STATUS_OK;
                }

            FATAL("Track not found");
            return AARUF_ERROR_TRACK_NOT_FOUND;
        case CdTrackIsrc:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 12 || buffer == NULL)
            {
                *length = 12;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            for(int i = 0; i < ctx->tracks_header.entries; i++)
                if(sector_address >= ctx->track_entries[i].start && sector_address <= ctx->track_entries[i].end)
                {
                    memcpy(buffer, ctx->track_entries[i].isrc, 12);
                    TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
                    return AARUF_STATUS_OK;
                }

            FATAL("Track not found");
            return AARUF_ERROR_TRACK_NOT_FOUND;
        case CdSectorSubchannelAaru:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 96 || buffer == NULL)
            {
                *length = 96;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_subchannel == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_subchannel + corrected_sector_address * 96, 96);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case DvdCmi:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 1 || buffer == NULL)
            {
                *length = 1;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_cpr_mai == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_cpr_mai + corrected_sector_address * 6, 1);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case DvdSectorInformation:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 1 || buffer == NULL)
            {
                *length = 1;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_id == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_id + corrected_sector_address * 4, 1);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case DvdSectorNumber:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 3 || buffer == NULL)
            {
                *length = 3;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_id == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_id + corrected_sector_address * 4 + 1, 3);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case DvdSectorIedAaru:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 2 || buffer == NULL)
            {
                *length = 2;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_ied == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_ied + corrected_sector_address * 2, 2);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case DvdSectorEdcAaru:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 4 || buffer == NULL)
            {
                *length = 4;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_edc == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_edc + corrected_sector_address * 4, 4);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case DvdTitleKeyDecrypted:
            if(ctx->image_info.MetadataMediaType != OpticalDisc)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 5 || buffer == NULL)
            {
                *length = 5;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_decrypted_title_key == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_decrypted_title_key + corrected_sector_address * 5, 5);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case AppleSonyTagAaru:
            if(ctx->image_info.MetadataMediaType != BlockMedia)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 12 || buffer == NULL)
            {
                *length = 12;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_subchannel == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_subchannel + corrected_sector_address * 12, 12);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case AppleProfileTagAaru:
            if(ctx->image_info.MetadataMediaType != BlockMedia)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 20 || buffer == NULL)
            {
                *length = 20;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_subchannel == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_subchannel + corrected_sector_address * 20, 20);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        case PriamDataTowerTagAaru:
            if(ctx->image_info.MetadataMediaType != BlockMedia)
            {
                FATAL("Invalid media type for tag");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
                return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }

            if(*length != 24 || buffer == NULL)
            {
                *length = 24;
                FATAL("Incorrect tag size");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            if(ctx->sector_subchannel == NULL)
            {
                FATAL("Sector tag not found");
                TRACE("Exiting aaruf_read_sector_tag() = AARUF_ERROR_SECTOR_TAG_NOT_PRESENT");
                return AARUF_ERROR_SECTOR_TAG_NOT_PRESENT;
            }

            memcpy(buffer, ctx->sector_subchannel + corrected_sector_address * 24, 24);
            TRACE("Exiting aaruf_read_sector_tag() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        default:
            TRACE("Do not know how to read sector tag %d", tag);
            return AARUF_ERROR_INVALID_TAG;
    }
}
