/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "aaruformat.h"
#include "erasure_internal.h"
#include "internal.h"
#include "log.h"
#include "uthash.h"

/**
 * @brief Processes a data block from the image stream.
 *
 * Reads a data block from the image, decompresses if needed, and updates the context with its contents.
 * This function handles various types of data blocks including compressed (LZMA) and uncompressed data,
 * performs CRC validation, and stores the processed data in the appropriate context fields.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the data block.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully processed the data block. This is returned when:
 *         - The block is processed successfully and all validations pass
 *         - A NoData block type is encountered (these are skipped)
 *         - A UserData block type is encountered (these update sector size but are otherwise skipped)
 *         - Block validation fails but processing continues (non-fatal errors like CRC mismatches)
 *         - Memory allocation failures occur (processing continues with other blocks)
 *         - Block reading failures occur (processing continues with other blocks)
 *         - Unknown compression types are encountered (block is skipped)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Failed to seek to the block position in the image stream.
 *         This occurs when fseek() fails or the file position doesn't match the expected offset.
 *
 * @retval AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) LZMA decompression failed. This can happen when:
 *         - The LZMA decoder returns an error code
 *         - The decompressed data size doesn't match the expected block length
 *
 * @note Most validation and reading errors are treated as non-fatal and result in AARUF_STATUS_OK
 *       being returned while the problematic block is skipped. This allows processing to continue
 *       with other blocks in the image.
 *
 * @note The function performs the following validations:
 *       - Block identifier matches the expected block type
 *       - Block data type matches the expected data type
 *       - CRC64 checksum validation (with version-specific byte order handling)
 *       - Proper decompression for LZMA-compressed blocks
 *
 * @warning Memory allocated for block data is stored in the context and should be freed when
 *          the context is destroyed. The function may replace existing data in the context.
 */

int32_t process_data_block(aaruformat_context *ctx, IndexEntry *entry)
{
    TRACE("Entering process_data_block(%p, %p)", ctx, entry);
    BlockHeader    block_header;
    int            pos           = 0;
    size_t         read_bytes    = 0;
    size_t         lzma_size     = 0;
    uint8_t       *cmp_data      = NULL;
    uint8_t       *cst_data      = NULL;
    mediaTagEntry *old_media_tag = NULL;
    mediaTagEntry *media_tag     = NULL;
    uint8_t       *data          = NULL;
    uint8_t        lzma_properties[LZMA_PROPERTIES_LENGTH];

    // Check if the context, index entry, and image stream are valid
    if(ctx == NULL || entry == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Seek to block
    pos = aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET);
    if(pos < 0 || aaruf_ftell(ctx->imageStream) != (aaru_off_t)entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Even if those two checks shall have been done before

    // NOP block, skip
    if(entry->dataType == kDataTypeNone || (entry->dataType == kDataTypeUserData && ctx->header.biggestSectorSize > 0))
    {
        TRACE("NoData block found, skipping");
        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    TRACE("Reading block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&block_header, 1, sizeof(BlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(BlockHeader))
    {
        FATAL("Could not read block header at %" PRIu64, entry->offset);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    ctx->image_info.ImageSize += block_header.cmpLength;

    // Unused, skip
    if(entry->dataType == kDataTypeUserData)
    {
        if(block_header.sectorSize > ctx->image_info.SectorSize)
        {
            TRACE("Setting sector size to %" PRIu64 " bytes", block_header.sectorSize);
            ctx->image_info.SectorSize = block_header.sectorSize;
        }

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(block_header.identifier != entry->blockType)
    {
        TRACE("Incorrect identifier for data block at position %" PRIu64, entry->offset);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(block_header.type != entry->dataType)
    {
        TRACE("Expected block with data type %4.4s at position %" PRIu64 " but found data type %4.4s",
              (char *)&entry->blockType, entry->offset, (char *)&block_header.type);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    TRACE("Found data block with type %4.4s at position %" PRIu64, (char *)&entry->blockType, entry->offset);

    if(block_header.compression == kCompressionLzma || block_header.compression == kCompressionLzmaCst)
    {
        int error_no = 0;
        if(block_header.compression == kCompressionLzmaCst && block_header.type != kDataTypeCdSubchannel)
        {
            TRACE("Invalid compression type %u for block with data type %u, continuing...", block_header.compression,
                  block_header.type);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        if(block_header.cmpLength < LZMA_PROPERTIES_LENGTH)
        {
            TRACE("Compressed block length %" PRIu32 " too small for LZMA properties, continuing...",
                  block_header.cmpLength);
            TRACE("Exiting process_data_block() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
            return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
        }

        lzma_size = block_header.cmpLength - LZMA_PROPERTIES_LENGTH;

        cmp_data = (lzma_size == 0) ? NULL : (uint8_t *)malloc(lzma_size);
        if(lzma_size != 0 && cmp_data == NULL)
        {
            TRACE("Cannot allocate memory for block, continuing...");

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        if(block_header.length != 0)
        {
            data = (uint8_t *)malloc(block_header.length);
            if(data == NULL)
            {
                TRACE("Cannot allocate memory for block, continuing...");
                free(cmp_data);

                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }
        }
        else
            data = NULL;

        read_bytes = fread(lzma_properties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
        if(read_bytes != LZMA_PROPERTIES_LENGTH)
        {
            TRACE("Could not read LZMA properties, continuing...");
            free(cmp_data);
            free(data);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        if(lzma_size != 0)
        {
            read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
            if(read_bytes != lzma_size)
            {
                TRACE("Could not read compressed block, continuing...");
                free(cmp_data);
                free(data);

                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }
        }

        if(block_header.length != 0)
        {
            read_bytes = block_header.length;
            error_no   = aaruf_lzma_decode_buffer(data, &read_bytes, cmp_data, &lzma_size, lzma_properties,
                                                  LZMA_PROPERTIES_LENGTH);

            if(error_no != 0)
            {
                TRACE("Got error %d from LZMA, continuing...", error_no);
                free(cmp_data);
                free(data);

                TRACE("Exiting process_data_block() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }

            if(read_bytes != block_header.length)
            {
                TRACE("Error decompressing block, expected %" PRIu32 " bytes but got %zu bytes, continuing...",
                      block_header.length, read_bytes);
                free(cmp_data);
                free(data);

                TRACE("Exiting process_data_block() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }
        }
        else if(lzma_size != 0)
        {
            TRACE("Compressed payload present for zero-length block, continuing...");
            free(cmp_data);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        if(block_header.compression == kCompressionLzmaCst && block_header.length != 0)
        {
            cst_data = (uint8_t *)malloc(block_header.length);
            if(cst_data == NULL)
            {
                TRACE("Cannot allocate memory for block, continuing...");
                free(cmp_data);
                free(data);

                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }

            aaruf_cst_untransform(data, cst_data, block_header.length);
            free(data);
            data     = cst_data;
            cst_data = NULL;
        }

        free(cmp_data);
    }
    else if(block_header.compression == kCompressionZstd || block_header.compression == kCompressionZstdCst)
    {
        if(block_header.compression == kCompressionZstdCst && block_header.type != kDataTypeCdSubchannel)
        {
            TRACE("Invalid compression type %u for block with data type %u, continuing...", block_header.compression,
                  block_header.type);
            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        cmp_data = (block_header.cmpLength == 0) ? NULL : (uint8_t *)malloc(block_header.cmpLength);
        if(block_header.cmpLength != 0 && cmp_data == NULL)
        {
            TRACE("Cannot allocate memory for compressed block, continuing...");
            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        if(block_header.length != 0)
        {
            data = (uint8_t *)malloc(block_header.length);
            if(data == NULL)
            {
                TRACE("Cannot allocate memory for block, continuing...");
                free(cmp_data);
                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }
        }
        else
            data = NULL;

        if(block_header.cmpLength != 0)
        {
            read_bytes = fread(cmp_data, 1, block_header.cmpLength, ctx->imageStream);
            if(read_bytes != block_header.cmpLength)
            {
                TRACE("Could not read compressed block, continuing...");
                free(cmp_data);
                free(data);
                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }
        }

        if(block_header.length != 0)
        {
            size_t decoded = aaruf_zstd_decode_buffer(data, block_header.length, cmp_data, block_header.cmpLength);

            if(decoded != block_header.length)
            {
                TRACE("Error decompressing zstd block, expected %" PRIu32 " bytes but got %zu bytes, continuing...",
                      block_header.length, decoded);
                free(cmp_data);
                free(data);
                TRACE("Exiting process_data_block() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            }
        }

        free(cmp_data);

        if(block_header.compression == kCompressionZstdCst && block_header.length != 0)
        {
            cst_data = (uint8_t *)malloc(block_header.length);
            if(cst_data == NULL)
            {
                TRACE("Cannot allocate memory for CST untransform, continuing...");
                free(data);
                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }

            aaruf_cst_untransform(data, cst_data, block_header.length);
            free(data);
            data     = cst_data;
            cst_data = NULL;
        }
    }
    else if(block_header.compression == kCompressionNone)
    {
        if(block_header.length != 0)
        {
            data = (uint8_t *)malloc(block_header.length);
            if(data == NULL)
            {
                TRACE("Cannot allocate memory for block, continuing...");

                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }
        }
        else
            data = NULL;

        read_bytes = (block_header.length == 0) ? 0 : fread(data, 1, block_header.length, ctx->imageStream);

        if(block_header.length != 0 && read_bytes != block_header.length)
        {
            free(data);
            TRACE("Could not read block, continuing...");

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }
    }
    else
    {
        TRACE("Found unknown compression type %d, continuing...", block_header.compression);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(block_header.length > 0)
    {
        uint64_t crc64 = aaruf_crc64_data(data, block_header.length);

        // Due to how C# wrote it, it is effectively reversed
        if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

        if(crc64 != block_header.crc64)
        {
            TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64, crc64, block_header.crc64);
            free(data);
            data = NULL;

            /* Attempt erasure coding recovery */
            uint8_t *recovered = NULL;
            uint32_t rec_size  = 0;
            if(ec_recover_meta_block(ctx, entry->offset, &recovered, &rec_size) == AARUF_STATUS_OK && recovered)
            {
                TRACE("EC recovery succeeded for block at offset %" PRIu64, entry->offset);
                /* Re-parse: recovered contains raw on-disk bytes (BlockHeader + payload) */
                if(rec_size > sizeof(BlockHeader))
                {
                    BlockHeader rec_hdr;
                    memcpy(&rec_hdr, recovered, sizeof(BlockHeader));
                    uint32_t payload_offset = sizeof(BlockHeader);

                    if(rec_hdr.compression == kCompressionLzma && rec_hdr.cmpLength > LZMA_PROPERTIES_LENGTH)
                        payload_offset += LZMA_PROPERTIES_LENGTH;

                    uint8_t *rec_payload = recovered + payload_offset;
                    uint32_t rec_payload_size = rec_size - payload_offset;

                    /* Decompress if needed */
                    if(rec_hdr.compression == kCompressionNone)
                    {
                        data = (uint8_t *)malloc(rec_hdr.length);
                        if(data) memcpy(data, rec_payload, rec_hdr.length);
                    }
                    else if(rec_hdr.compression == kCompressionLzma)
                    {
                        data = (uint8_t *)malloc(rec_hdr.length);
                        if(data)
                        {
                            size_t out_sz = rec_hdr.length;
                            size_t src_sz = rec_payload_size;
                            aaruf_lzma_decode_buffer(data, &out_sz, rec_payload, &src_sz,
                                                     recovered + sizeof(BlockHeader), LZMA_PROPERTIES_LENGTH);
                        }
                    }
                    else if(rec_hdr.compression == kCompressionZstd)
                    {
                        data = (uint8_t *)malloc(rec_hdr.length);
                        if(data)
                            aaruf_zstd_decode_buffer(data, rec_hdr.length, rec_payload, rec_payload_size);
                    }

                    /* Update block_header with recovered values */
                    if(data) memcpy(&block_header, &rec_hdr, sizeof(BlockHeader));
                }
                free(recovered);
            }

            if(!data)
            {
                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }
        }
    }

    // Check if it's not a media tag, but a sector tag, and fill the appropriate table then
    switch(entry->dataType)
    {
        case kDataTypeCdSectorPrefix:
        case kDataTypeCdSectorPrefixCorrected:
            if(entry->dataType == kDataTypeCdSectorPrefixCorrected) { ctx->sector_prefix_corrected = data; }
            else
            {
                ctx->sector_prefix        = data;
                ctx->sector_prefix_length = block_header.length;
            }

            ctx->readableSectorTags[kSectorTagCdSync]   = true;
            ctx->readableSectorTags[kSectorTagCdHeader] = true;

            break;
        case kDataTypeCdSectorSuffix:
        case kDataTypeCdSectorSuffixCorrected:
            if(entry->dataType == kDataTypeCdSectorSuffixCorrected)
                ctx->sector_suffix_corrected = data;
            else
            {
                ctx->sector_suffix        = data;
                ctx->sector_suffix_length = block_header.length;
            }

            ctx->readableSectorTags[kSectorTagCdSubHeader] = true;
            ctx->readableSectorTags[kSectorTagCdEcc]       = true;
            ctx->readableSectorTags[kSectorTagCdEccP]      = true;
            ctx->readableSectorTags[kSectorTagCdEccQ]      = true;
            ctx->readableSectorTags[kSectorTagCdEdc]       = true;
            break;
        case kDataTypeCdSubchannel:
            ctx->sector_subchannel                          = data;
            ctx->readableSectorTags[kSectorTagCdSubchannel] = true;
            break;
        case kDataTypeAppleProfileTag:
            ctx->sector_subchannel                          = data;
            ctx->readableSectorTags[kSectorTagAppleProfile] = true;
            break;
        case kDataTypeAppleSonyTag:
            ctx->sector_subchannel                            = data;
            ctx->readableSectorTags[kSectorTagPriamDataTower] = true;
            break;
        case kDataTypePriamDataTowerTag:
            ctx->sector_subchannel                       = data;
            ctx->readableSectorTags[kSectorTagAppleSony] = true;
            break;
        case kDataTypeCdSubHeader:
            ctx->mode2_subheaders = data;
            break;
        case kDataTypeDvdSectorId:
            ctx->sector_id                                          = data;
            ctx->readableSectorTags[kSectorTagDvdSectorNumber]      = true;
            ctx->readableSectorTags[kSectorTagDvdSectorInformation] = true;
            break;
        case kDataTypeDvdSectorIed:
            ctx->sector_ied                                 = data;
            ctx->readableSectorTags[kSectorTagDvdSectorIed] = true;
            break;
        case kDataTypeDvdSectorCprMai:
            ctx->sector_cpr_mai                       = data;
            ctx->readableSectorTags[kSectorTagDvdCmi] = true;
            break;
        case kDataTypeDvdSectorEdc:
            ctx->sector_edc                                 = data;
            ctx->readableSectorTags[kSectorTagDvdSectorEdc] = true;
            break;
        case kDataTypeDvdTitleKeyDecrypted:
            ctx->sector_decrypted_title_key                         = data;
            ctx->readableSectorTags[kSectorTagDvdTitleKeyDecrypted] = true;
            break;
        case kDataTypeBdSectorEdc:
            ctx->sector_edc                                = data;
            ctx->readableSectorTags[kSectorTagBdSectorEdc] = true;
            break;
        default:
            media_tag = (mediaTagEntry *)malloc(sizeof(mediaTagEntry));

            if(media_tag == NULL)
            {
                TRACE("Cannot allocate memory for media tag entry.");
                free(data);
                data = NULL;
                break;
            }
            memset(media_tag, 0, sizeof(mediaTagEntry));

            media_tag->type   = aaruf_get_media_tag_type_for_datatype(block_header.type);
            media_tag->data   = data;
            media_tag->length = block_header.length;

            HASH_REPLACE_INT(ctx->mediaTags, type, media_tag, old_media_tag);

            if(old_media_tag != NULL)
            {
                TRACE("Replaced media tag with type %d", old_media_tag->type);
                free(old_media_tag->data);
                free(old_media_tag);
                old_media_tag = NULL;
            }

            break;
    }

    TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}