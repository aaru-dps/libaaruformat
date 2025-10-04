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
/**
 * @file close.c
 * @brief Implements image finalization and resource cleanup for libaaruformat.
 *
 * This translation unit contains the logic that flushes any remaining in-memory
 * structures (deduplication tables, checksum information, track metadata, MODE 2
 * subheaders, sector prefix data and the global index) to the on-disk Aaru image
 * when closing a context opened for writing. It also performs orderly teardown of
 * dynamically allocated resources regardless of read or write mode.
 *
 * Helper (static) functions perform discrete serialization steps; the public
 * entry point is ::aaruf_close(). All write helpers assume that the caller has
 * already validated the context magic and (for write mode) written the initial
 * provisional header at offset 0. Functions return libaaruformat status codes
 * (AARUF_STATUS_OK on success or an AARUF_ERROR_* constant) except for
 * ::aaruf_close(), which returns 0 on success and -1 on failure while setting
 * errno.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <aaruformat.h>

#include "internal.h"
#include "log.h"

/**
 * @brief Flush a cached secondary (child) DeDuplication Table (DDT) to the image.
 *
 * When working with a multi-level DDT (i.e. primary table with tableShift > 0), a single
 * secondary table may be cached in memory while sectors belonging to its range are written.
 * This function serializes the currently cached secondary table (if any) at the end of the
 * file, aligning the write position to the DDT block alignment, and updates the corresponding
 * primary table entry to point to the new block-aligned location. The primary table itself is
 * then re-written in-place (only its data array portion) to persist the updated pointer. The
 * index is updated by removing any previous index entry for the same secondary table offset
 * and inserting a new one for the freshly written table.
 *
 * CRC64 is computed for the serialized table contents and stored in both crc64 and cmpCrc64
 * fields of the written DdtHeader2 (no compression is applied).
 *
 * On return the cached secondary table buffers and bookkeeping fields (cachedSecondaryDdtSmall,
 * cachedSecondaryDdtBig, cachedDdtOffset) are cleared.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 * @return AARUF_STATUS_OK on success, or AARUF_ERROR_CANNOT_WRITE_HEADER if the
 *         secondary table or updated primary table cannot be flushed.
 * @retval AARUF_STATUS_OK Success or no cached secondary DDT needed flushing.
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER Failed writing secondary table or rewriting primary table.
 * @note If no cached secondary DDT is pending (detected via tableShift and cache pointers),
 *       the function is a no-op returning AARUF_STATUS_OK.
 * @internal
 */
static int32_t write_cached_secondary_ddt(aaruformatContext *ctx)
{
    // Write cached secondary table to file end and update primary table entry with its position
    // Check if we have a cached table that needs to be written (either it has an offset or exists in memory)
    bool has_cached_secondary_ddt =
        ctx->userDataDdtHeader.tableShift > 0 &&
        (ctx->cachedDdtOffset != 0 || ctx->cachedSecondaryDdtSmall != NULL || ctx->cachedSecondaryDdtBig != NULL);

    if(!has_cached_secondary_ddt) return AARUF_STATUS_OK;

    TRACE("Writing cached secondary DDT table to file");

    fseek(ctx->imageStream, 0, SEEK_END);
    long end_of_file = ftell(ctx->imageStream);

    // Align the position according to block alignment shift
    uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(end_of_file & alignment_mask)
    {
        // Calculate the next aligned position
        uint64_t aligned_position = end_of_file + alignment_mask & ~alignment_mask;

        // Seek to the aligned position and pad with zeros if necessary
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        end_of_file = aligned_position;

        TRACE("Aligned DDT write position from %ld to %" PRIu64 " (alignment shift: %d)",
              ftell(ctx->imageStream) - (aligned_position - end_of_file), aligned_position,
              ctx->userDataDdtHeader.blockAlignmentShift);
    }

    // Prepare DDT header for the cached table
    DdtHeader2 ddt_header          = {0};
    ddt_header.identifier          = DeDuplicationTable2;
    ddt_header.type                = UserData;
    ddt_header.compression         = None;
    ddt_header.levels              = ctx->userDataDdtHeader.levels;
    ddt_header.tableLevel          = ctx->userDataDdtHeader.tableLevel + 1;
    ddt_header.previousLevelOffset = ctx->primaryDdtOffset;
    ddt_header.negative            = ctx->userDataDdtHeader.negative;
    ddt_header.overflow            = ctx->userDataDdtHeader.overflow;
    ddt_header.blockAlignmentShift = ctx->userDataDdtHeader.blockAlignmentShift;
    ddt_header.dataShift           = ctx->userDataDdtHeader.dataShift;
    ddt_header.tableShift          = 0;  // Secondary tables are single level
    ddt_header.sizeType            = ctx->userDataDdtHeader.sizeType;

    uint64_t items_per_ddt_entry = 1 << ctx->userDataDdtHeader.tableShift;
    ddt_header.blocks            = items_per_ddt_entry;
    ddt_header.entries           = items_per_ddt_entry;
    ddt_header.start             = ctx->cachedDdtPosition * items_per_ddt_entry;

    // Calculate data size
    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ddt_header.length = items_per_ddt_entry * sizeof(uint16_t);
    else
        ddt_header.length = items_per_ddt_entry * sizeof(uint32_t);

    ddt_header.cmpLength = ddt_header.length;

    // Calculate CRC64 of the data
    crc64_ctx *crc64_context = aaruf_crc64_init();
    if(crc64_context != NULL)
    {
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtSmall, ddt_header.length);
        else
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtBig, ddt_header.length);

        uint64_t crc64;
        aaruf_crc64_final(crc64_context, &crc64);
        ddt_header.crc64    = crc64;
        ddt_header.cmpCrc64 = crc64;
    }

    // Write header
    if(fwrite(&ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
    {
        // Write data
        size_t written_bytes = 0;
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            written_bytes = fwrite(ctx->cachedSecondaryDdtSmall, ddt_header.length, 1, ctx->imageStream);
        else
            written_bytes = fwrite(ctx->cachedSecondaryDdtBig, ddt_header.length, 1, ctx->imageStream);

        if(written_bytes == 1)
        {
            // Update primary table entry to point to new location
            uint64_t new_secondary_table_block_offset = end_of_file >> ctx->userDataDdtHeader.blockAlignmentShift;

            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                ctx->userDataDdtMini[ctx->cachedDdtPosition] = (uint16_t)new_secondary_table_block_offset;
            else
                ctx->userDataDdtBig[ctx->cachedDdtPosition] = (uint32_t)new_secondary_table_block_offset;

            // Update index: remove old entry for cached DDT and add new one
            TRACE("Updating index for cached secondary DDT");

            // Remove old index entry for the cached DDT
            if(ctx->cachedDdtOffset != 0)
            {
                TRACE("Removing old index entry for DDT at offset %" PRIu64, ctx->cachedDdtOffset);
                IndexEntry *entry = NULL;

                // Find and remove the old index entry
                for(unsigned int k = 0; k < utarray_len(ctx->indexEntries); k++)
                {
                    entry = (IndexEntry *)utarray_eltptr(ctx->indexEntries, k);
                    if(entry && entry->offset == ctx->cachedDdtOffset && entry->blockType == DeDuplicationTable2)
                    {
                        TRACE("Found old DDT index entry at position %u, removing", k);
                        utarray_erase(ctx->indexEntries, k, 1);
                        break;
                    }
                }
            }

            // Add new index entry for the newly written secondary DDT
            IndexEntry new_ddt_entry;
            new_ddt_entry.blockType = DeDuplicationTable2;
            new_ddt_entry.dataType  = UserData;
            new_ddt_entry.offset    = end_of_file;

            utarray_push_back(ctx->indexEntries, &new_ddt_entry);
            TRACE("Added new DDT index entry at offset %" PRIu64, end_of_file);

            // Write the updated primary table back to its original position in the file
            long saved_pos = ftell(ctx->imageStream);
            fseek(ctx->imageStream, ctx->primaryDdtOffset + sizeof(DdtHeader2), SEEK_SET);

            size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                            ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                            : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

            size_t primary_written_bytes = 0;
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                primary_written_bytes = fwrite(ctx->userDataDdtMini, primary_table_size, 1, ctx->imageStream);
            else
                primary_written_bytes = fwrite(ctx->userDataDdtBig, primary_table_size, 1, ctx->imageStream);

            if(primary_written_bytes != 1)
            {
                TRACE("Could not flush primary DDT table to file.");
                return AARUF_ERROR_CANNOT_WRITE_HEADER;
            }

            fseek(ctx->imageStream, saved_pos, SEEK_SET);
        }
        else
            TRACE("Failed to write cached secondary DDT data");
    }
    else
        TRACE("Failed to write cached secondary DDT header");

    // Free the cached table
    if(ctx->cachedSecondaryDdtSmall != NULL)
    {
        free(ctx->cachedSecondaryDdtSmall);
        ctx->cachedSecondaryDdtSmall = NULL;
    }
    if(ctx->cachedSecondaryDdtBig != NULL)
    {
        free(ctx->cachedSecondaryDdtBig);
        ctx->cachedSecondaryDdtBig = NULL;
    }
    ctx->cachedDdtOffset = 0;

    // Set position
    fseek(ctx->imageStream, 0, SEEK_END);

    return AARUF_STATUS_OK;
}

/**
 * @brief Write (flush) the multi-level primary DDT table header and data back to its file offset.
 *
 * This function is applicable only when a multi-level DDT is in use (tableShift > 0). It updates
 * the header fields (identifier, type, compression, CRC, lengths) and writes first the header and
 * then the entire primary table data block at ctx->primaryDdtOffset. The function also pushes an
 * IndexEntry for the primary DDT into the in-memory index array so that later write_index_block()
 * will serialize it.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 * @return AARUF_STATUS_OK on success; AARUF_ERROR_CANNOT_WRITE_HEADER if either the header or the
 *         table body cannot be written. Returns AARUF_STATUS_OK immediately if no primary table
 *         should be written (single-level DDT or table buffers absent).
 * @retval AARUF_STATUS_OK Success or nothing to do (no multi-level primary table present).
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER Failed writing header or primary table data.
 * @internal
 */
static int32_t write_primary_ddt(aaruformatContext *ctx)
{
    // Write the cached primary DDT table back to its position in the file
    if(ctx->userDataDdtHeader.tableShift <= 0 || ctx->userDataDdtMini == NULL && ctx->userDataDdtBig == NULL)
        return AARUF_STATUS_OK;

    TRACE("Writing cached primary DDT table back to file");

    // Calculate CRC64 of the primary DDT table data first
    crc64_ctx *crc64_context = aaruf_crc64_init();
    if(crc64_context != NULL)
    {
        size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                        ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                        : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtMini, primary_table_size);
        else
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtBig, primary_table_size);

        uint64_t crc64;
        aaruf_crc64_final(crc64_context, &crc64);

        // Properly populate all header fields for multi-level DDT primary table
        ctx->userDataDdtHeader.identifier  = DeDuplicationTable2;
        ctx->userDataDdtHeader.type        = UserData;
        ctx->userDataDdtHeader.compression = None;
        // levels, tableLevel, previousLevelOffset, negative, overflow, blockAlignmentShift,
        // dataShift, tableShift, sizeType, entries, blocks, start are already set during creation
        ctx->userDataDdtHeader.crc64       = crc64;
        ctx->userDataDdtHeader.cmpCrc64    = crc64;
        ctx->userDataDdtHeader.length      = primary_table_size;
        ctx->userDataDdtHeader.cmpLength   = primary_table_size;

        TRACE("Calculated CRC64 for primary DDT: 0x%16lX", crc64);
    }

    // First write the DDT header
    fseek(ctx->imageStream, ctx->primaryDdtOffset, SEEK_SET);

    size_t headerWritten = fwrite(&ctx->userDataDdtHeader, sizeof(DdtHeader2), 1, ctx->imageStream);
    if(headerWritten != 1)
    {
        TRACE("Failed to write primary DDT header to file");
        return AARUF_ERROR_CANNOT_WRITE_HEADER;
    }

    // Then write the table data (position is already after the header)
    size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                    ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                    : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

    // Write the primary table data
    size_t written_bytes = 0;
    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        written_bytes = fwrite(ctx->userDataDdtMini, primary_table_size, 1, ctx->imageStream);
    else
        written_bytes = fwrite(ctx->userDataDdtBig, primary_table_size, 1, ctx->imageStream);

    if(written_bytes == 1)
    {
        TRACE("Successfully wrote primary DDT header and table to file (%" PRIu64 " entries, %zu bytes)",
              ctx->userDataDdtHeader.entries, primary_table_size);

        // Add primary DDT to index
        TRACE("Adding primary DDT to index");
        IndexEntry primary_ddt_entry;
        primary_ddt_entry.blockType = DeDuplicationTable2;
        primary_ddt_entry.dataType  = UserData;
        primary_ddt_entry.offset    = ctx->primaryDdtOffset;

        utarray_push_back(ctx->indexEntries, &primary_ddt_entry);
        TRACE("Added primary DDT index entry at offset %" PRIu64, ctx->primaryDdtOffset);
    }
    else
        TRACE("Failed to write primary DDT table to file");

    return AARUF_STATUS_OK;
}

/**
 * @brief Serialize a single-level DDT (tableShift == 0) directly after its header.
 *
 * For single-level DDT configurations the entire table of sector references is contiguous.
 * This routine computes a CRC64 for the table, populates all header metadata, writes the header
 * at ctx->primaryDdtOffset followed immediately by the table, and registers the block in the
 * index.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode with tableShift == 0.
 * @return AARUF_STATUS_OK on success; AARUF_ERROR_CANNOT_WRITE_HEADER if serialization fails.
 *         Returns AARUF_STATUS_OK without action when the context represents a multi-level DDT
 *         or the table buffers are NULL.
 * @retval AARUF_STATUS_OK Success or nothing to do (not single-level / buffers missing).
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER Failed writing header or table data.
 * @internal
 */
static int32_t write_single_level_ddt(aaruformatContext *ctx)
{
    // Write the single level DDT table block aligned just after the header
    if(ctx->userDataDdtHeader.tableShift != 0 || ctx->userDataDdtMini == NULL && ctx->userDataDdtBig == NULL)
        return AARUF_STATUS_OK;

    TRACE("Writing single-level DDT table to file");

    // Calculate CRC64 of the primary DDT table data
    crc64_ctx *crc64_context = aaruf_crc64_init();
    if(crc64_context != NULL)
    {
        size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                        ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                        : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtMini, primary_table_size);
        else
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtBig, primary_table_size);

        uint64_t crc64;
        aaruf_crc64_final(crc64_context, &crc64);

        // Properly populate all header fields
        ctx->userDataDdtHeader.identifier          = DeDuplicationTable2;
        ctx->userDataDdtHeader.type                = UserData;
        ctx->userDataDdtHeader.compression         = None;
        ctx->userDataDdtHeader.levels              = 1;  // Single level
        ctx->userDataDdtHeader.tableLevel          = 0;  // Top level
        ctx->userDataDdtHeader.previousLevelOffset = 0;  // No previous level for single-level DDT
        // negative and overflow are already set during creation
        // blockAlignmentShift, dataShift, tableShift, sizeType, entries, blocks, start are already set
        ctx->userDataDdtHeader.crc64               = crc64;
        ctx->userDataDdtHeader.cmpCrc64            = crc64;
        ctx->userDataDdtHeader.length              = primary_table_size;
        ctx->userDataDdtHeader.cmpLength           = primary_table_size;

        TRACE("Calculated CRC64 for single-level DDT: 0x%16lX", crc64);
    }

    // Write the DDT header first
    fseek(ctx->imageStream, ctx->primaryDdtOffset, SEEK_SET);

    size_t header_written = fwrite(&ctx->userDataDdtHeader, sizeof(DdtHeader2), 1, ctx->imageStream);
    if(header_written != 1)
    {
        TRACE("Failed to write single-level DDT header to file");
        return AARUF_ERROR_CANNOT_WRITE_HEADER;
    }

    // Then write the table data (position is already after the header)
    size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                    ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                    : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

    // Write the primary table data
    size_t written_bytes = 0;
    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        written_bytes = fwrite(ctx->userDataDdtMini, primary_table_size, 1, ctx->imageStream);
    else
        written_bytes = fwrite(ctx->userDataDdtBig, primary_table_size, 1, ctx->imageStream);

    if(written_bytes == 1)
    {
        TRACE("Successfully wrote single-level DDT header and table to file (%" PRIu64 " entries, %zu bytes)",
              ctx->userDataDdtHeader.entries, primary_table_size);

        // Add single-level DDT to index
        TRACE("Adding single-level DDT to index");
        IndexEntry single_ddt_entry;
        single_ddt_entry.blockType = DeDuplicationTable2;
        single_ddt_entry.dataType  = UserData;
        single_ddt_entry.offset    = ctx->primaryDdtOffset;

        utarray_push_back(ctx->indexEntries, &single_ddt_entry);
        TRACE("Added single-level DDT index entry at offset %" PRIu64, ctx->primaryDdtOffset);
    }
    else
        TRACE("Failed to write single-level DDT table data to file");

    return AARUF_STATUS_OK;
}

/**
 * @brief Finalize any active checksum calculations and append a checksum block.
 *
 * This routine completes pending hash contexts (MD5, SHA-1, SHA-256, SpamSum, BLAKE3), marks the
 * presence flags in ctx->checksums, and if at least one checksum exists writes a ChecksumBlock at
 * the end of the image (block-aligned). Individual ChecksumEntry records are serialized for each
 * available algorithm and the block is indexed. Feature flags (e.g. AARU_FEATURE_RW_BLAKE3) are
 * updated if required.
 *
 * Memory ownership: for SpamSum a buffer is allocated here if a digest was being computed and is
 * subsequently written without being freed inside this function (it will be freed during close).
 * The BLAKE3 context is freed after finalization.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 * @internal
 */
static void write_checksum_block(aaruformatContext *ctx)
{
    uint64_t alignment_mask;
    uint64_t aligned_position;

    // Finalize pending checksums
    if(ctx->calculating_md5)
    {
        ctx->checksums.hasMd5 = true;
        aaruf_md5_final(&ctx->md5_context, ctx->checksums.md5);
    }
    if(ctx->calculating_sha1)
    {
        ctx->checksums.hasSha1 = true;
        aaruf_sha1_final(&ctx->sha1_context, ctx->checksums.sha1);
    }
    if(ctx->calculating_sha256)
    {
        ctx->checksums.hasSha256 = true;
        aaruf_sha256_final(&ctx->sha256_context, ctx->checksums.sha256);
    }
    if(ctx->calculating_spamsum)
    {
        ctx->checksums.hasSpamSum = true;
        ctx->checksums.spamsum    = calloc(1, FUZZY_MAX_RESULT);
        aaruf_spamsum_final(ctx->spamsum_context, ctx->checksums.spamsum);
        aaruf_spamsum_free(ctx->spamsum_context);
    }
    if(ctx->calculating_blake3)
    {
        ctx->checksums.hasBlake3 = true;
        blake3_hasher_finalize(ctx->blake3_context, ctx->checksums.blake3, BLAKE3_OUT_LEN);
        free(ctx->blake3_context);
    }

    // Write the checksums block
    bool has_checksums = ctx->checksums.hasMd5 || ctx->checksums.hasSha1 || ctx->checksums.hasSha256 ||
                         ctx->checksums.hasSpamSum || ctx->checksums.hasBlake3;

    if(!has_checksums) return;

    ChecksumHeader checksum_header = {0};
    checksum_header.identifier     = ChecksumBlock;

    fseek(ctx->imageStream, 0, SEEK_END);
    long checksum_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    alignment_mask         = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(checksum_position & alignment_mask)
    {
        aligned_position = checksum_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        checksum_position = aligned_position;
    }

    // Skip checksum_header
    fseek(ctx->imageStream, sizeof(checksum_header), SEEK_CUR);

    if(ctx->checksums.hasMd5)
    {
        TRACE("Writing MD5 checksum entry");
        ChecksumEntry md5_entry = {0};
        md5_entry.length        = MD5_DIGEST_LENGTH;
        md5_entry.type          = Md5;
        fwrite(&md5_entry, sizeof(ChecksumEntry), 1, ctx->imageStream);
        fwrite(&ctx->checksums.md5, MD5_DIGEST_LENGTH, 1, ctx->imageStream);
        checksum_header.length += sizeof(ChecksumEntry) + MD5_DIGEST_LENGTH;
        checksum_header.entries++;
    }

    if(ctx->checksums.hasSha1)
    {
        TRACE("Writing SHA1 checksum entry");
        ChecksumEntry sha1_entry = {0};
        sha1_entry.length        = SHA1_DIGEST_LENGTH;
        sha1_entry.type          = Sha1;
        fwrite(&sha1_entry, sizeof(ChecksumEntry), 1, ctx->imageStream);
        fwrite(&ctx->checksums.sha1, SHA1_DIGEST_LENGTH, 1, ctx->imageStream);
        checksum_header.length += sizeof(ChecksumEntry) + SHA1_DIGEST_LENGTH;
        checksum_header.entries++;
    }

    if(ctx->checksums.hasSha256)
    {
        TRACE("Writing SHA256 checksum entry");
        ChecksumEntry sha256_entry = {0};
        sha256_entry.length        = SHA256_DIGEST_LENGTH;
        sha256_entry.type          = Sha256;
        fwrite(&sha256_entry, sizeof(ChecksumEntry), 1, ctx->imageStream);
        fwrite(&ctx->checksums.sha256, SHA256_DIGEST_LENGTH, 1, ctx->imageStream);
        checksum_header.length += sizeof(ChecksumEntry) + SHA256_DIGEST_LENGTH;
        checksum_header.entries++;
    }

    if(ctx->checksums.hasSpamSum)
    {
        TRACE("Writing SpamSum checksum entry");
        ChecksumEntry spamsum_entry = {0};
        spamsum_entry.length        = strlen((const char *)ctx->checksums.spamsum);
        spamsum_entry.type          = SpamSum;
        fwrite(&spamsum_entry, sizeof(ChecksumEntry), 1, ctx->imageStream);
        fwrite(ctx->checksums.spamsum, spamsum_entry.length, 1, ctx->imageStream);
        checksum_header.length += sizeof(ChecksumEntry) + spamsum_entry.length;
        checksum_header.entries++;
    }

    if(ctx->checksums.hasBlake3)
    {
        TRACE("Writing BLAKE3 checksum entry");
        ChecksumEntry blake3_entry = {0};
        blake3_entry.length        = BLAKE3_OUT_LEN;
        blake3_entry.type          = Blake3;
        fwrite(&blake3_entry, sizeof(ChecksumEntry), 1, ctx->imageStream);
        fwrite(&ctx->checksums.blake3, BLAKE3_OUT_LEN, 1, ctx->imageStream);
        checksum_header.length += sizeof(ChecksumEntry) + BLAKE3_OUT_LEN;
        checksum_header.entries++;
        ctx->header.featureCompatible |= AARU_FEATURE_RW_BLAKE3;
    }

    fseek(ctx->imageStream, checksum_position, SEEK_SET);
    TRACE("Writing checksum header");
    fwrite(&checksum_header, sizeof(ChecksumHeader), 1, ctx->imageStream);

    // Add checksum block to index
    TRACE("Adding checksum block to index");
    IndexEntry checksum_index_entry;
    checksum_index_entry.blockType = ChecksumBlock;
    checksum_index_entry.dataType  = 0;
    checksum_index_entry.offset    = checksum_position;

    utarray_push_back(ctx->indexEntries, &checksum_index_entry);
    TRACE("Added checksum block index entry at offset %" PRIu64, checksum_position);
}

/**
 * @brief Serialize the tracks metadata block and add it to the index.
 *
 * Writes a TracksHeader followed by the array of TrackEntry structures if any track entries are
 * present (tracksHeader.entries > 0 and trackEntries not NULL). The block is aligned to the DDT
 * block boundary and an IndexEntry is appended.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 * @internal
 */
static void write_tracks_block(aaruformatContext *ctx)
{
    // Write tracks block
    if(ctx->tracksHeader.entries <= 0 || ctx->trackEntries == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long     tracks_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    uint64_t alignment_mask  = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(tracks_position & alignment_mask)
    {
        uint64_t aligned_position = tracks_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        tracks_position = aligned_position;
    }

    TRACE("Writing tracks block at position %ld", tracks_position);
    // Write header
    if(fwrite(&ctx->tracksHeader, sizeof(TracksHeader), 1, ctx->imageStream) == 1)
    {
        // Write entries
        size_t written_entries =
            fwrite(ctx->trackEntries, sizeof(TrackEntry), ctx->tracksHeader.entries, ctx->imageStream);

        if(written_entries == ctx->tracksHeader.entries)
        {
            TRACE("Successfully wrote tracks block with %u entries", ctx->tracksHeader.entries);
            // Add tracks block to index
            TRACE("Adding tracks block to index");

            IndexEntry tracks_index_entry;
            tracks_index_entry.blockType = TracksBlock;
            tracks_index_entry.dataType  = 0;
            tracks_index_entry.offset    = tracks_position;
            utarray_push_back(ctx->indexEntries, &tracks_index_entry);
            TRACE("Added tracks block index entry at offset %" PRIu64, tracks_position);
        }
    }
}

/**
 * @brief Serialize a MODE 2 (XA) subheaders data block.
 *
 * When Compact Disc Mode 2 form sectors are present, optional 8-byte subheaders (one per logical
 * sector including negative / overflow ranges) are stored in an in-memory buffer. This function
 * writes that buffer as a DataBlock of type CompactDiscMode2Subheader with CRC64 (no compression)
 * and adds an IndexEntry.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode; ctx->mode2_subheaders must
 *            point to a buffer sized for the described sector span.
 * @internal
 */
static void write_mode2_subheaders_block(aaruformatContext *ctx)
{
    // Write MODE 2 subheader data block
    if(ctx->mode2_subheaders == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long     mode2_subheaders_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    uint64_t alignment_mask            = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(mode2_subheaders_position & alignment_mask)
    {
        uint64_t aligned_position = mode2_subheaders_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        mode2_subheaders_position = aligned_position;
    }

    TRACE("Writing MODE 2 subheaders block at position %ld", mode2_subheaders_position);
    BlockHeader subheaders_block = {0};
    subheaders_block.identifier  = DataBlock;
    subheaders_block.type        = CompactDiscMode2Subheader;
    subheaders_block.compression = None;
    subheaders_block.length =
        (uint32_t)(ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow) * 8;
    subheaders_block.cmpLength = subheaders_block.length;

    // Calculate CRC64
    subheaders_block.crc64    = aaruf_crc64_data(ctx->mode2_subheaders, subheaders_block.length);
    subheaders_block.cmpCrc64 = subheaders_block.crc64;

    // Write header
    if(fwrite(&subheaders_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        // Write data
        size_t written_bytes = fwrite(ctx->mode2_subheaders, subheaders_block.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote MODE 2 subheaders block (%" PRIu64 " bytes)", subheaders_block.length);
            // Add MODE 2 subheaders block to index
            TRACE("Adding MODE 2 subheaders block to index");
            IndexEntry mode2_subheaders_index_entry;
            mode2_subheaders_index_entry.blockType = DataBlock;
            mode2_subheaders_index_entry.dataType  = CompactDiscMode2Subheader;
            mode2_subheaders_index_entry.offset    = mode2_subheaders_position;
            utarray_push_back(ctx->indexEntries, &mode2_subheaders_index_entry);
            TRACE("Added MODE 2 subheaders block index entry at offset %" PRIu64, mode2_subheaders_position);
        }
    }
}

/**
 * @brief Serialize the optional CD sector prefix block.
 *
 * The sector prefix corresponds to the leading bytes of a raw CD sector (synchronization pattern,
 * address / header in MSF format and the mode byte) that precede the user data and any ECC/ECCP
 * fields. It is unrelated to subchannel (P–W) data, which is handled separately. If prefix data
 * was collected (ctx->sector_prefix != NULL), this writes a DataBlock of type CdSectorPrefix
 * containing exactly the bytes accumulated up to sector_prefix_offset. The block is CRC64
 * protected, uncompressed, aligned to the DDT block boundary and indexed.
 *
 * Typical raw Mode 1 / Mode 2 sector layout (2352 bytes total):
 *   12-byte sync pattern (00 FF FF FF FF FF FF FF FF FF FF 00)
 *   3-byte address (Minute, Second, Frame in BCD)
 *   1-byte mode (e.g., 0x01, 0x02)
 *   ... user data ...
 *   ... ECC / EDC ...
 * The stored prefix encompasses the first 16 bytes (sync + address + mode) or whatever subset
 * was gathered by the writer.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 * @internal
 */
static void write_sector_prefix(aaruformatContext *ctx)
{
    if(ctx->sector_prefix == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long     prefix_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    uint64_t alignment_mask  = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(prefix_position & alignment_mask)
    {
        uint64_t aligned_position = prefix_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        prefix_position = aligned_position;
    }

    TRACE("Writing sector prefix block at position %ld", prefix_position);
    BlockHeader prefix_block = {0};
    prefix_block.identifier  = DataBlock;
    prefix_block.type        = CdSectorPrefix;
    prefix_block.compression = None;
    prefix_block.length      = (uint32_t)ctx->sector_prefix_offset;
    prefix_block.cmpLength   = prefix_block.length;

    // Calculate CRC64
    prefix_block.crc64    = aaruf_crc64_data(ctx->sector_prefix, prefix_block.length);
    prefix_block.cmpCrc64 = prefix_block.crc64;

    // Write header
    if(fwrite(&prefix_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        // Write data
        size_t written_bytes = fwrite(ctx->sector_prefix, prefix_block.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote CD sector prefix block (%" PRIu64 " bytes)", prefix_block.length);
            // Add prefix block to index
            TRACE("Adding CD sector prefix block to index");
            IndexEntry prefix_index_entry;
            prefix_index_entry.blockType = DataBlock;
            prefix_index_entry.dataType  = CdSectorPrefix;
            prefix_index_entry.offset    = prefix_position;
            utarray_push_back(ctx->indexEntries, &prefix_index_entry);
            TRACE("Added CD sector prefix block index entry at offset %" PRIu64, prefix_position);
        }
    }
}

/**
 * @brief Serialize the optional CD sector suffix block (EDC/ECC region capture).
 *
 * The sector suffix contains trailing integrity and redundancy bytes of a raw CD sector that
 * follow the user data area. Depending on the mode this includes:
 *   - Mode 1: 4-byte EDC, 8-byte reserved, 276 bytes (P/Q layers) ECC = 288 bytes total.
 *   - Mode 2 Form 1: 4-byte EDC, 8 reserved, 276 ECC = 288 bytes.
 *   - Mode 2 Form 2: 4-byte EDC only (no ECC) but when an error is detected the implementation
 *     may still store a 288-byte suffix container for uniformity when capturing errored data.
 *
 * During writing, when an error or uncorrectable condition is detected for a sector's suffix,
 * the 288-byte trailing portion (starting at offset 2064 for Mode 1 / Form 1 or 2348/2349 for
 * other layouts) is copied into an in-memory expandable buffer pointed to by ctx->sector_suffix.
 * The per-sector DDT entry encodes either an inlined status (OK / No CRC / etc.) or an index to
 * the stored suffix (ctx->sector_suffix_offset / 288). This function serializes the accumulated
 * suffix buffer as a DataBlock of type CdSectorSuffix if any suffix bytes were stored.
 *
 * Layout considerations:
 *   - The block length is exactly the number of bytes copied (ctx->sector_suffix_offset).
 *   - No compression is applied; CRC64 is calculated on the raw suffix stream for integrity.
 *   - The write position is aligned to the DDT block alignment (2^blockAlignmentShift).
 *
 * Indexing: An IndexEntry is appended so later readers can locate the suffix collection. Absence
 * of this block implies no per-sector suffix captures were required (all suffixes considered OK).
 *
 * Thread / reentrancy: This routine is called only once during finalization (aaruf_close) in a
 * single-threaded context; no synchronization is performed.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 * @internal
 */
static void write_sector_suffix(aaruformatContext *ctx)
{
    if(ctx->sector_suffix == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           suffix_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask  = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(suffix_position & alignment_mask)
    {
        const uint64_t aligned_position = suffix_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        suffix_position = aligned_position;
    }

    TRACE("Writing sector suffix block at position %ld", suffix_position);
    BlockHeader suffix_block = {0};
    suffix_block.identifier  = DataBlock;
    suffix_block.type        = CdSectorSuffix;
    suffix_block.compression = None;
    suffix_block.length      = (uint32_t)ctx->sector_suffix_offset;
    suffix_block.cmpLength   = suffix_block.length;

    // Calculate CRC64
    suffix_block.crc64    = aaruf_crc64_data(ctx->sector_suffix, suffix_block.length);
    suffix_block.cmpCrc64 = suffix_block.crc64;

    // Write header
    if(fwrite(&suffix_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        // Write data
        const size_t written_bytes = fwrite(ctx->sector_suffix, suffix_block.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote CD sector suffix block (%" PRIu64 " bytes)", suffix_block.length);
            // Add suffix block to index
            TRACE("Adding CD sector suffix block to index");
            IndexEntry suffix_index_entry;
            suffix_index_entry.blockType = DataBlock;
            suffix_index_entry.dataType  = CdSectorSuffix;
            suffix_index_entry.offset    = suffix_position;
            utarray_push_back(ctx->indexEntries, &suffix_index_entry);
            TRACE("Added CD sector suffix block index entry at offset %" PRIu64, suffix_position);
        }
    }
}

/**
 * @brief Serialize the per-sector CD prefix status / index DeDuplication Table (DDT v2, prefix variant).
 *
 * This DDT records for each logical sector (including negative and overflow ranges) an optional
 * index into the stored 16‑byte prefix capture buffer plus a 4-bit status code. It is written only
 * if at least one prefix status or captured prefix was recorded (i.e., the in-memory DDT array exists).
 *
 * Encoding (current implementation uses the "mini" 16-bit form):
 *   Bits 15..12 : SectorStatus enum value (see enums.h, values already positioned for v2 mini layout).
 *   Bits 11..0  : 12-bit index (0..4095) of the 16-byte prefix chunk inside the CdSectorPrefix data block,
 *                 or 0 when no external prefix bytes were stored (status applies to a generated/implicit prefix).
 *
 * Notes:
 *   - Unlike DDT v1, there are no CD_XFIX_MASK / CD_DFIX_MASK macros used here. The bit layout is compact
 *     and directly encoded when writing (status values are pre-shifted where needed in write.c).
 *   - Only the 16-bit mini variant is currently produced (sectorPrefixDdtMini). A 32-bit form is reserved
 *     for future expansion (sectorPrefixDdt) but is not emitted unless populated.
 *   - The table length equals (negative + Sectors + overflow) * entrySize.
 *   - dataShift is set to 4 (2^4 = 16) expressing the granularity of referenced prefix units.
 *   - No compression is applied; crc64/cmpCrc64 protect the raw table bytes.
 *   - Idempotent: if an index entry of type DeDuplicationTable2 + CdSectorPrefixCorrected already exists
 *     the function returns immediately.
 *
 * Alignment: The table is block-aligned using the same blockAlignmentShift as user data DDTs.
 * Indexing: An IndexEntry is appended on success so readers can locate and parse the table.
 *
 * @param ctx Pointer to a valid aaruformatContext in write mode (must not be NULL).
 * @internal
 */
static void write_sector_prefix_ddt(aaruformatContext *ctx)
{
    if(ctx->sectorPrefixDdtMini == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           prefix_ddt_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask      = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(prefix_ddt_position & alignment_mask)
    {
        const uint64_t aligned_position = prefix_ddt_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        prefix_ddt_position = aligned_position;
    }

    TRACE("Writing sector prefix DDT v2 at position %ld", prefix_ddt_position);
    DdtHeader2 ddt_header2          = {0};
    ddt_header2.identifier          = DeDuplicationTable2;
    ddt_header2.type                = CdSectorPrefix;
    ddt_header2.compression         = None;
    ddt_header2.levels              = 1;
    ddt_header2.tableLevel          = 0;
    ddt_header2.negative            = ctx->userDataDdtHeader.negative;
    ddt_header2.overflow            = ctx->userDataDdtHeader.overflow;
    ddt_header2.blockAlignmentShift = ctx->userDataDdtHeader.blockAlignmentShift;
    ddt_header2.dataShift           = ctx->userDataDdtHeader.dataShift;
    ddt_header2.tableShift          = 0;  // Single-level DDT
    ddt_header2.sizeType            = SmallDdtSizeType;
    ddt_header2.entries   = ctx->imageInfo.Sectors + ctx->userDataDdtHeader.negative + ctx->userDataDdtHeader.overflow;
    ddt_header2.blocks    = ctx->userDataDdtHeader.blocks;
    ddt_header2.start     = 0;
    ddt_header2.length    = ddt_header2.entries * sizeof(uint16_t);
    ddt_header2.cmpLength = ddt_header2.length;
    // Calculate CRC64
    ddt_header2.crc64     = aaruf_crc64_data((uint8_t *)ctx->sectorPrefixDdtMini, (uint32_t)ddt_header2.length);
    ddt_header2.cmpCrc64  = ddt_header2.crc64;

    // Write header
    if(fwrite(&ddt_header2, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
    {
        // Write data
        const size_t written_bytes = fwrite(ctx->sectorPrefixDdtMini, ddt_header2.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote sector prefix DDT v2 (%" PRIu64 " bytes)", ddt_header2.length);
            // Add prefix block to index
            TRACE("Adding sector prefix DDT v2 to index");
            IndexEntry prefix_ddt_index_entry;
            prefix_ddt_index_entry.blockType = DeDuplicationTable2;
            prefix_ddt_index_entry.dataType  = CdSectorPrefix;
            prefix_ddt_index_entry.offset    = prefix_ddt_position;
            utarray_push_back(ctx->indexEntries, &prefix_ddt_index_entry);
            TRACE("Added sector prefix DDT v2 index entry at offset %" PRIu64, prefix_ddt_position);
        }
    }
}

/**
 * @brief Serialize the per-sector CD suffix status / index DeDuplication Table (DDT v2, suffix variant).
 *
 * This routine emits the DDT v2 table that maps each logical sector (including negative pregap
 * and overflow ranges) to (a) a 4-bit SectorStatus code and (b) a 12-bit index pointing into the
 * captured suffix data block (CdSectorSuffix). The suffix bytes (typically the 288-byte EDC/ECC
 * region for Mode 1 or Mode 2 Form 1, or shorter EDC-only for Form 2) are stored separately by
 * write_sector_suffix(). When a sector's suffix was captured because it differed from the expected
 * generated values (e.g., uncorrectable, intentionally preserved corruption, or variant layout),
 * the in-memory mini entry records the index of its 16 * 18 (288) byte chunk. If no suffix bytes
 * were explicitly stored for a sector the index field is zero and only the status applies.
 *
 * Encoding (mini 16-bit variant only, DDT v2 semantics):
 *   Bits 15..12 : SectorStatus enumeration (already aligned for direct storage; no legacy masks used).
 *   Bits 11..0  : 12-bit index (0..4095) referencing a suffix unit of size 288 bytes (2^dataShift granularity),
 *                 or 0 when the sector uses an implicit / regenerated suffix (no external data captured).
 *
 * Characteristics & constraints:
 *   - Only DDT v2 is supported here; no fallback or mixed-mode emission with v1 occurs.
 *   - Only the compact "mini" (16-bit) table form is currently produced (sectorSuffixDdtMini filled during write).
 *   - Table length = (negative + total Sectors + overflow) * sizeof(uint16_t).
 *   - dataShift mirrors userDataDdtHeader.dataShift (expressing granularity for index referencing).
 *   - Single-level table (levels = 1, tableLevel = 0, tableShift = 0).
 *   - CRC64 protects the raw uncompressed table (crc64 == cmpCrc64 because compression = None).
 *   - Alignment: The table is aligned to 2^(blockAlignmentShift) before writing to guarantee block boundary access.
 *   - Idempotence: If sectorSuffixDdtMini is NULL the function is a no-op (indicating no suffix anomalies captured).
 *
 * Index integration:
 *   On success an IndexEntry (blockType = DeDuplicationTable2, dataType = CdSectorSuffix, offset = file position)
 *   is appended to ctx->indexEntries enabling later readers to locate and parse the suffix DDT.
 *
 * Error handling & assumptions:
 *   - The function does not explicitly propagate write failures upward; partial write errors simply
 *     omit the index entry (TRACE logs provide diagnostics). Higher level close logic determines
 *     overall success.
 *   - Executed in a single-threaded finalization path; no locking is performed or required.
 *
 * Preconditions:
 *   - ctx must be a valid non-NULL pointer opened for writing.
 *   - ctx->sectorSuffixDdtMini must point to a fully populated contiguous array of uint16_t entries.
 *
 * @param ctx Active aaruformatContext being finalized.
 * @internal
 */
static void write_sector_suffix_ddt(aaruformatContext *ctx)
{
    if(ctx->sectorSuffixDdtMini == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           suffix_ddt_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask      = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(suffix_ddt_position & alignment_mask)
    {
        const uint64_t aligned_position = suffix_ddt_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        suffix_ddt_position = aligned_position;
    }

    TRACE("Writing sector suffix DDT v2 at position %ld", suffix_ddt_position);
    DdtHeader2 ddt_header2          = {0};
    ddt_header2.identifier          = DeDuplicationTable2;
    ddt_header2.type                = CdSectorSuffix;
    ddt_header2.compression         = None;
    ddt_header2.levels              = 1;
    ddt_header2.tableLevel          = 0;
    ddt_header2.negative            = ctx->userDataDdtHeader.negative;
    ddt_header2.overflow            = ctx->userDataDdtHeader.overflow;
    ddt_header2.blockAlignmentShift = ctx->userDataDdtHeader.blockAlignmentShift;
    ddt_header2.dataShift           = ctx->userDataDdtHeader.dataShift;
    ddt_header2.tableShift          = 0;  // Single-level DDT
    ddt_header2.sizeType            = SmallDdtSizeType;
    ddt_header2.entries   = ctx->imageInfo.Sectors + ctx->userDataDdtHeader.negative + ctx->userDataDdtHeader.overflow;
    ddt_header2.blocks    = ctx->userDataDdtHeader.blocks;
    ddt_header2.start     = 0;
    ddt_header2.length    = ddt_header2.entries * sizeof(uint16_t);
    ddt_header2.cmpLength = ddt_header2.length;
    // Calculate CRC64
    ddt_header2.crc64     = aaruf_crc64_data((uint8_t *)ctx->sectorSuffixDdtMini, (uint32_t)ddt_header2.length);
    ddt_header2.cmpCrc64  = ddt_header2.crc64;

    // Write header
    if(fwrite(&ddt_header2, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
    {
        // Write data
        const size_t written_bytes = fwrite(ctx->sectorSuffixDdtMini, ddt_header2.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote sector suffix DDT v2 (%" PRIu64 " bytes)", ddt_header2.length);
            // Add suffix block to index
            TRACE("Adding sector suffix DDT v2 to index");
            IndexEntry suffix_ddt_index_entry;
            suffix_ddt_index_entry.blockType = DeDuplicationTable2;
            suffix_ddt_index_entry.dataType  = CdSectorSuffix;
            suffix_ddt_index_entry.offset    = suffix_ddt_position;
            utarray_push_back(ctx->indexEntries, &suffix_ddt_index_entry);
            TRACE("Added sector suffix DDT v2 index entry at offset %" PRIu64, suffix_ddt_position);
        }
    }
}

/**
 * @brief Serialize the per-sector subchannel or tag data block.
 *
 * This routine writes out the accumulated subchannel or tag metadata that accompanies each logical
 * sector (including negative pregap and overflow ranges). The exact interpretation and size depend
 * on the media type:
 *
 * **Optical Disc (CD) subchannel:**
 *   - Type: CdSectorSubchannel
 *   - Contains the deinterleaved P through W subchannel data (96 bytes per sector).
 *   - Covers: (negative + Sectors + overflow) sectors.
 *   - The P channel marks pause boundaries; Q encodes track/index/time information (MCN, ISRC).
 *   - R–W channels are typically used for CD+G graphics or CD-TEXT.
 *
 * **Apple block media tags:**
 *   - **AppleProfile / AppleFileWare:** 20 bytes per sector (AppleProfileTag).
 *   - **AppleSonyDS / AppleSonySS:** 12 bytes per sector (AppleSonyTag).
 *   - **PriamDataTower:** 24 bytes per sector (PriamDataTowerTag).
 *   - Tags encode filesystem metadata, allocation state, or device-specific control information.
 *   - Only positive sectors (0 through Sectors-1) and overflow are included; no negative range.
 *
 * The block size is computed as (applicable_sector_count) × (bytes_per_sector_for_media_type).
 * No compression is applied; the raw buffer is written verbatim after a DataBlock header with
 * CRC64 integrity protection. The write position is aligned to the DDT block boundary
 * (2^blockAlignmentShift) before serialization begins.
 *
 * **Media type validation:**
 * The function only proceeds if XmlMediaType is OpticalDisc or BlockMedia and (for block media)
 * the specific MediaType matches one of the supported Apple or Priam variants. Any other media
 * type causes an immediate silent return (logged at TRACE level).
 *
 * **Alignment & indexing:**
 * The block is aligned using the same alignment shift as the user data DDT. An IndexEntry
 * (blockType = DataBlock, dataType = subchannel_block.type, offset = aligned file position) is
 * appended to ctx->indexEntries on successful write, enabling readers to locate the subchannel
 * or tag data.
 *
 * **Thread / reentrancy:**
 * This function is invoked once during finalization (aaruf_close) in a single-threaded context.
 * No synchronization is performed.
 *
 * **Error handling:**
 * Write errors are logged but not explicitly propagated as return codes. If the write succeeds
 * an index entry is added; if it fails no entry is added and diagnostics appear in TRACE logs.
 * Higher level close logic determines overall success or failure.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->sector_subchannel must point to a fully populated buffer sized appropriately
 *            for the media type and sector count.
 *
 * @internal
 */
static void write_sector_subchannel(const aaruformatContext *ctx)
{
    if(ctx->sector_subchannel == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }

    TRACE("Writing sector subchannel block at position %ld", block_position);
    BlockHeader subchannel_block = {0};
    subchannel_block.identifier  = DataBlock;
    subchannel_block.compression = None;

    if(ctx->imageInfo.XmlMediaType == OpticalDisc)
    {
        subchannel_block.type = CdSectorSubchannel;
        subchannel_block.length =
            (uint32_t)(ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow) * 96;
    }
    else if(ctx->imageInfo.XmlMediaType == BlockMedia)
        switch(ctx->imageInfo.MediaType)
        {
            case AppleProfile:
            case AppleFileWare:
                subchannel_block.type   = AppleProfileTag;
                subchannel_block.length = (uint32_t)(ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow) * 20;
                break;
            case AppleSonyDS:
            case AppleSonySS:
                subchannel_block.type   = AppleSonyTag;
                subchannel_block.length = (uint32_t)(ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow) * 12;
                break;
            case PriamDataTower:
                subchannel_block.type   = PriamDataTowerTag;
                subchannel_block.length = (uint32_t)(ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow) * 24;
                break;
            default:
                TRACE("Incorrect media type, not writing sector subchannel block");
                return;  // Incorrect media type
        }
    else
    {
        TRACE("Incorrect media type, not writing sector subchannel block");
        return;  // Incorrect media type
    }

    subchannel_block.cmpLength = subchannel_block.length;

    // Calculate CRC64
    subchannel_block.crc64    = aaruf_crc64_data(ctx->sector_subchannel, subchannel_block.length);
    subchannel_block.cmpCrc64 = subchannel_block.crc64;

    // Write header
    if(fwrite(&subchannel_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        // Write data
        const size_t written_bytes = fwrite(ctx->sector_subchannel, subchannel_block.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote sector subchannel block (%" PRIu64 " bytes)", subchannel_block.length);
            // Add subchannel block to index
            TRACE("Adding sector subchannel block to index");
            IndexEntry subchannel_index_entry;
            subchannel_index_entry.blockType = DataBlock;
            subchannel_index_entry.dataType  = subchannel_block.type;
            subchannel_index_entry.offset    = block_position;
            utarray_push_back(ctx->indexEntries, &subchannel_index_entry);
            TRACE("Added sector subchannel block index entry at offset %" PRIu64, block_position);
        }
    }
}

/**
 * @brief Serialize the accumulated index entries at the end of the image and back-patch the header.
 *
 * All previously written structural blocks push their IndexEntry into ctx->indexEntries. This
 * function collects them, writes an IndexHeader3 followed by each IndexEntry, computes CRC64 over
 * the entries, and then updates the main AaruHeaderV2 (at offset 0) with the index offset. The
 * index itself is aligned to the DDT block boundary. No previous index chaining is currently
 * implemented (index_header.previous = 0).
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 * @return AARUF_STATUS_OK on success; AARUF_ERROR_CANNOT_WRITE_HEADER if the index header, any
 *         entry, or the header back-patch fails.
 * @retval AARUF_STATUS_OK Index written and header updated.
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER Failed writing index header, entries, or updating main header.
 * @internal
 */
static int32_t write_index_block(aaruformatContext *ctx)
{
    // Write the complete index at the end of the file
    TRACE("Writing index at the end of the file");
    fseek(ctx->imageStream, 0, SEEK_END);
    long index_position = ftell(ctx->imageStream);

    // Align index position to block boundary if needed
    uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    if(index_position & alignment_mask)
    {
        uint64_t aligned_position = index_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        index_position = aligned_position;
        TRACE("Aligned index position to %" PRIu64, aligned_position);
    }

    // Prepare index header
    IndexHeader3 index_header;
    index_header.identifier = IndexBlock3;
    index_header.entries    = utarray_len(ctx->indexEntries);
    index_header.previous   = 0;  // No previous index for now

    TRACE("Writing index with %" PRIu64 " entries at position %ld", index_header.entries, index_position);

    // Calculate CRC64 of index entries
    crc64_ctx *index_crc64_context = aaruf_crc64_init();
    if(index_crc64_context != NULL && index_header.entries > 0)
    {
        size_t index_data_size = index_header.entries * sizeof(IndexEntry);
        aaruf_crc64_update(index_crc64_context, (uint8_t *)utarray_front(ctx->indexEntries), index_data_size);
        aaruf_crc64_final(index_crc64_context, &index_header.crc64);
        TRACE("Calculated index CRC64: 0x%16lX", index_header.crc64);
    }
    else
        index_header.crc64 = 0;

    // Write index header
    if(fwrite(&index_header, sizeof(IndexHeader3), 1, ctx->imageStream) == 1)
    {
        TRACE("Successfully wrote index header");

        // Write index entries
        if(index_header.entries > 0)
        {
            size_t      entries_written = 0;
            IndexEntry *entry           = NULL;

            for(entry = (IndexEntry *)utarray_front(ctx->indexEntries); entry != NULL;
                entry = (IndexEntry *)utarray_next(ctx->indexEntries, entry))
                if(fwrite(entry, sizeof(IndexEntry), 1, ctx->imageStream) == 1)
                {
                    entries_written++;
                    TRACE("Wrote index entry: blockType=0x%08X dataType=%u offset=%" PRIu64, entry->blockType,
                          entry->dataType, entry->offset);
                }
                else
                {
                    TRACE("Failed to write index entry %zu", entries_written);
                    break;
                }

            if(entries_written == index_header.entries)
            {
                TRACE("Successfully wrote all %zu index entries", entries_written);

                // Update header with index offset and rewrite it
                ctx->header.indexOffset = index_position;
                TRACE("Updating header with index offset: %" PRIu64, ctx->header.indexOffset);

                // Seek back to beginning and rewrite header
                fseek(ctx->imageStream, 0, SEEK_SET);
                if(fwrite(&ctx->header, sizeof(AaruHeaderV2), 1, ctx->imageStream) == 1)
                    TRACE("Successfully updated header with index offset");
                else
                {
                    TRACE("Failed to update header with index offset");
                    return AARUF_ERROR_CANNOT_WRITE_HEADER;
                }
            }
            else
            {
                TRACE("Failed to write all index entries (wrote %zu of %" PRIu64 ")", entries_written,
                      index_header.entries);
                return AARUF_ERROR_CANNOT_WRITE_HEADER;
            }
        }
    }
    else
    {
        TRACE("Failed to write index header");
        return AARUF_ERROR_CANNOT_WRITE_HEADER;
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Close an Aaru image context, flushing pending data structures and releasing resources.
 *
 * Public API entry point used to finalize an image being written or simply dispose of a context
 * opened for reading. For write-mode contexts (ctx->isWriting true) the function performs the
 * following ordered steps:
 *   1. Rewrite the (possibly updated) main header at offset 0.
 *   2. Close any open data block via aaruf_close_current_block().
 *   3. Flush a cached secondary DDT (multi-level) if pending.
 *   4. Flush either the primary DDT (multi-level) or the single-level DDT table.
 *   5. Finalize and append checksum block(s) for all enabled algorithms.
 *   6. Write auxiliary metadata blocks: tracks, MODE 2 subheaders, sector prefix.
 *   7. Serialize the global index and patch header.indexOffset.
 *   8. Clear deduplication hash map if used.
 *
 * Afterwards (or for read-mode contexts) all dynamically allocated buffers, arrays, hash tables
 * and mapping structures are freed/unmapped. Media tags are removed from their hash table.
 *
 * Error Handling:
 *   - Returns -1 with errno = EINVAL if the provided pointer is NULL or not a valid context.
 *   - Returns -1 with errno set to AARUF_ERROR_CANNOT_WRITE_HEADER if a header write fails.
 *   - If any intermediate serialization helper returns an error status, that error value is
 *     propagated (converted to -1 with errno set accordingly by the caller if desired). In the
 *     current implementation aaruf_close() directly returns the negative error code for helper
 *     failures to preserve detail.
 *
 * @param context Opaque pointer returned by earlier open/create calls (must be an aaruformatContext).
 * @return 0 on success; -1 or negative libaaruformat error code on failure.
 * @retval 0 All pending data flushed (if writing) and resources released successfully.
 * @retval -1 Invalid context pointer or initial header rewrite failure (errno = EINVAL or
 * AARUF_ERROR_CANNOT_WRITE_HEADER).
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER A later write helper (e.g., index, DDT) failed and returned this code
 * directly.
 * @retval <other negative libaaruformat code> Propagated from a write helper if future helpers add more error codes.
 * @note On success the context memory itself is freed; the caller must not reuse the pointer.
 */
int aaruf_close(void *context)
{
    TRACE("Entering aaruf_close(%p)", context);

    mediaTagEntry *media_tag     = NULL;
    mediaTagEntry *tmp_media_tag = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");
        errno = EINVAL;
        return -1;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        errno = EINVAL;
        return -1;
    }

    if(ctx->isWriting)
    {
        TRACE("File is writing");

        TRACE("Seeking to start of image");
        // Write the header at the beginning of the file
        fseek(ctx->imageStream, 0, SEEK_SET);

        TRACE("Writing header at position 0");
        if(fwrite(&ctx->header, sizeof(AaruHeaderV2), 1, ctx->imageStream) != 1)
        {
            fclose(ctx->imageStream);
            ctx->imageStream = NULL;
            errno            = AARUF_ERROR_CANNOT_WRITE_HEADER;
            return -1;
        }

        // Close current block first
        TRACE("Closing current block if any");
        if(ctx->writingBuffer != NULL)
        {
            int error = aaruf_close_current_block(ctx);

            if(error != AARUF_STATUS_OK) return error;
        }

        // Write cached secondary DDT table if any
        int32_t res = write_cached_secondary_ddt(ctx);
        if(res != AARUF_STATUS_OK) return res;

        // Write primary DDT table (multi-level) if applicable
        res = write_primary_ddt(ctx);
        if(res != AARUF_STATUS_OK) return res;

        // Write single-level DDT table if applicable
        res = write_single_level_ddt(ctx);
        if(res != AARUF_STATUS_OK) return res;

        // Finalize checksums and write checksum block
        write_checksum_block(ctx);

        // Write tracks block
        write_tracks_block(ctx);

        // Write MODE 2 subheader data block
        write_mode2_subheaders_block(ctx);

        // Write CD sector prefix data block
        write_sector_prefix(ctx);

        // Write sector prefix DDT (statuses + optional indexes)
        write_sector_prefix_ddt(ctx);

        // Write CD sector suffix data block (EDC/ECC captures)
        write_sector_suffix(ctx);

        // Write sector prefix DDT (EDC/ECC captures)
        write_sector_suffix_ddt(ctx);

        // Write sector subchannel data block
        write_sector_subchannel(ctx);

        // Write the complete index at the end of the file
        res = write_index_block(ctx);
        if(res != AARUF_STATUS_OK) return res;

        if(ctx->deduplicate && ctx->sectorHashMap != NULL)
        {
            TRACE("Clearing sector hash map");
            // Clear sector hash map
            free_map(ctx->sectorHashMap);
            ctx->sectorHashMap = NULL;
        }
    }

    TRACE("Freeing memory pointers");
    // This may do nothing if imageStream is NULL, but as the behaviour is undefined, better sure than sorry
    if(ctx->imageStream != NULL)
    {
        fclose(ctx->imageStream);
        ctx->imageStream = NULL;
    }

    // Free index entries array
    if(ctx->indexEntries != NULL)
    {
        utarray_free(ctx->indexEntries);
        ctx->indexEntries = NULL;
    }

    free(ctx->sector_prefix);
    ctx->sector_prefix = NULL;
    free(ctx->sectorPrefixCorrected);
    ctx->sectorPrefixCorrected = NULL;
    free(ctx->sector_suffix);
    ctx->sector_suffix = NULL;
    free(ctx->sectorSuffixCorrected);
    ctx->sectorSuffixCorrected = NULL;
    free(ctx->sector_subchannel);
    ctx->sector_subchannel = NULL;
    free(ctx->mode2_subheaders);
    ctx->mode2_subheaders = NULL;

    TRACE("Freeing media tags");
    if(ctx->mediaTags != NULL) HASH_ITER(hh, ctx->mediaTags, media_tag, tmp_media_tag)
        {
            HASH_DEL(ctx->mediaTags, media_tag);
            free(media_tag->data);
            free(media_tag);
        }

#ifdef __linux__  // TODO: Implement
    TRACE("Unmapping user data DDT if it is not in memory");
    if(!ctx->inMemoryDdt)
    {
        munmap(ctx->userDataDdt, ctx->mappedMemoryDdtSize);
        ctx->userDataDdt = NULL;
    }
#endif

    free(ctx->sectorPrefixDdtMini);
    ctx->sectorPrefixDdtMini = NULL;
    free(ctx->sectorPrefixDdt);
    ctx->sectorPrefixDdt = NULL;
    free(ctx->sectorSuffixDdtMini);
    ctx->sectorSuffixDdtMini = NULL;
    free(ctx->sectorSuffixDdt);
    ctx->sectorSuffixDdt = NULL;

    free(ctx->metadataBlock);
    ctx->metadataBlock = NULL;
    free(ctx->trackEntries);
    ctx->trackEntries = NULL;
    free(ctx->cicmBlock);
    ctx->cicmBlock = NULL;

    if(ctx->dumpHardwareEntriesWithData != NULL)
    {
        for(int i = 0; i < ctx->dumpHardwareHeader.entries; i++)
        {
            free(ctx->dumpHardwareEntriesWithData[i].extents);
            ctx->dumpHardwareEntriesWithData[i].extents = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].manufacturer);
            ctx->dumpHardwareEntriesWithData[i].manufacturer = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].model);
            ctx->dumpHardwareEntriesWithData[i].model = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].revision);
            ctx->dumpHardwareEntriesWithData[i].revision = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].firmware);
            ctx->dumpHardwareEntriesWithData[i].firmware = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].serial);
            ctx->dumpHardwareEntriesWithData[i].serial = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].softwareName);
            ctx->dumpHardwareEntriesWithData[i].softwareName = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].softwareVersion);
            ctx->dumpHardwareEntriesWithData[i].softwareVersion = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem);
            ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem = NULL;
        }
        ctx->dumpHardwareEntriesWithData = NULL;
    }

    free(ctx->readableSectorTags);
    ctx->readableSectorTags = NULL;

    free(ctx->eccCdContext);
    ctx->eccCdContext = NULL;

    free(ctx->checksums.spamsum);
    ctx->checksums.spamsum = NULL;

    // TODO: Free caches

    free(context);

    TRACE("Exiting aaruf_close() = 0");
    return 0;
}
