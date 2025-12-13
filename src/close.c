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
 * CRC64 is computed for the serialized table contents and stored in crc64; cmpCrc64 stores
 * the checksum of compressed data or equals crc64 if compression is not applied or not effective.
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
static int32_t write_cached_secondary_ddt(aaruformat_context *ctx)
{
    // Write cached secondary table to file end and update primary table entry with its position
    // Check if we have a cached table that needs to be written (either it has an offset or exists in memory)
    bool has_cached_secondary_ddt =
        ctx->user_data_ddt_header.tableShift > 0 && (ctx->cached_ddt_offset != 0 || ctx->cached_secondary_ddt2 != NULL);

    if(!has_cached_secondary_ddt) return AARUF_STATUS_OK;

    TRACE("Writing cached secondary DDT table to file");

    fseek(ctx->imageStream, 0, SEEK_END);
    long end_of_file = ftell(ctx->imageStream);

    // Align the position according to block alignment shift
    uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(end_of_file & alignment_mask)
    {
        // Calculate the next aligned position
        uint64_t aligned_position = end_of_file + alignment_mask & ~alignment_mask;

        // Seek to the aligned position and pad with zeros if necessary
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        end_of_file = aligned_position;

        TRACE("Aligned DDT write position from %ld to %" PRIu64 " (alignment shift: %d)",
              ftell(ctx->imageStream) - (aligned_position - end_of_file), aligned_position,
              ctx->user_data_ddt_header.blockAlignmentShift);
    }

    // Prepare DDT header for the cached table
    DdtHeader2 ddt_header          = {0};
    ddt_header.identifier          = DeDuplicationTableSecondary;
    ddt_header.type                = UserData;
    ddt_header.compression         = ctx->compression_enabled ? Lzma : None;
    ddt_header.levels              = ctx->user_data_ddt_header.levels;
    ddt_header.tableLevel          = ctx->user_data_ddt_header.tableLevel + 1;
    ddt_header.previousLevelOffset = ctx->primary_ddt_offset;
    ddt_header.negative            = ctx->user_data_ddt_header.negative;
    ddt_header.overflow            = ctx->user_data_ddt_header.overflow;
    ddt_header.blockAlignmentShift = ctx->user_data_ddt_header.blockAlignmentShift;
    ddt_header.dataShift           = ctx->user_data_ddt_header.dataShift;
    ddt_header.tableShift          = 0;  // Secondary tables are single level

    uint64_t items_per_ddt_entry = 1 << ctx->user_data_ddt_header.tableShift;
    ddt_header.blocks            = items_per_ddt_entry;
    ddt_header.entries           = items_per_ddt_entry;
    ddt_header.start             = ctx->cached_ddt_position * items_per_ddt_entry;

    // Calculate data size
    ddt_header.length = items_per_ddt_entry * sizeof(uint64_t);

    // Calculate CRC64 of the data
    crc64_ctx *crc64_context = aaruf_crc64_init();
    if(crc64_context != NULL)
    {
        aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cached_secondary_ddt2, (uint32_t)ddt_header.length);

        uint64_t crc64;
        aaruf_crc64_final(crc64_context, &crc64);
        ddt_header.crc64 = crc64;
    }

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(ddt_header.compression == None)
    {
        buffer              = (uint8_t *)ctx->cached_secondary_ddt2;
        ddt_header.cmpCrc64 = ddt_header.crc64;
    }
    else
    {
        buffer = malloc((size_t)ddt_header.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for secondary DDT v2 compression");
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }

        size_t dst_size   = (size_t)ddt_header.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size,

                                 (uint8_t *)ctx->cached_secondary_ddt2, ddt_header.length, lzma_properties, &props_size,
                                 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        ddt_header.cmpLength = (uint32_t)dst_size;

        if(ddt_header.cmpLength >= ddt_header.length)
        {
            ddt_header.compression = None;
            free(buffer);
            buffer = (uint8_t *)ctx->cached_secondary_ddt2;
        }
    }

    if(ddt_header.compression == None)
    {
        ddt_header.cmpLength = ddt_header.length;
        ddt_header.cmpCrc64  = ddt_header.crc64;
    }
    else
        ddt_header.cmpCrc64 = aaruf_crc64_data(buffer, ddt_header.cmpLength);

    if(ddt_header.compression == Lzma) ddt_header.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
    {
        if(ddt_header.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        if(fwrite(buffer, ddt_header.cmpLength, 1, ctx->imageStream) == 1)
        {
            // Update primary table entry to point to new location
            const uint64_t new_secondary_table_block_offset =
                end_of_file >> ctx->user_data_ddt_header.blockAlignmentShift;

            ctx->user_data_ddt2[ctx->cached_ddt_position] = (uint64_t)new_secondary_table_block_offset;

            // Update index: remove old entry for cached DDT and add new one
            TRACE("Updating index for cached secondary DDT");

            // Remove old index entry for the cached DDT
            if(ctx->cached_ddt_offset != 0)
            {
                TRACE("Removing old index entry for DDT at offset %" PRIu64, ctx->cached_ddt_offset);
                const IndexEntry *entry = NULL;

                // Find and remove the old index entry
                for(unsigned int k = 0; k < utarray_len(ctx->index_entries); k++)
                {
                    entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                    if(entry && entry->offset == ctx->cached_ddt_offset &&
                       entry->blockType == DeDuplicationTableSecondary)
                    {
                        TRACE("Found old DDT index entry at position %u, removing", k);
                        utarray_erase(ctx->index_entries, k, 1);
                        break;
                    }
                }
            }

            // Add new index entry for the newly written secondary DDT
            IndexEntry new_ddt_entry;
            new_ddt_entry.blockType = DeDuplicationTableSecondary;
            new_ddt_entry.dataType  = UserData;
            new_ddt_entry.offset    = end_of_file;

            utarray_push_back(ctx->index_entries, &new_ddt_entry);
            ctx->dirty_index_block = true;
            TRACE("Added new DDT index entry at offset %" PRIu64, end_of_file);

            // Write the updated primary table back to its original position in the file
            long saved_pos = ftell(ctx->imageStream);
            fseek(ctx->imageStream, ctx->primary_ddt_offset + sizeof(DdtHeader2), SEEK_SET);

            size_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

            size_t primary_written_bytes = 0;
            primary_written_bytes        = fwrite(ctx->user_data_ddt2, primary_table_size, 1, ctx->imageStream);

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
    free(ctx->cached_secondary_ddt2);
    ctx->cached_secondary_ddt2 = NULL;
    ctx->cached_ddt_offset     = 0;

    // Set position
    fseek(ctx->imageStream, 0, SEEK_END);

    if(ddt_header.compression == Lzma) free(buffer);

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
static int32_t write_primary_ddt(aaruformat_context *ctx)
{
    // Write the cached primary DDT table back to its position in the file
    if(ctx->user_data_ddt_header.tableShift <= 0 || ctx->user_data_ddt2 == NULL) return AARUF_STATUS_OK;

    TRACE("Writing cached primary DDT table back to file");

    // Calculate CRC64 of the primary DDT table data first
    crc64_ctx *crc64_context = aaruf_crc64_init();
    if(crc64_context != NULL)
    {
        size_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

        aaruf_crc64_update(crc64_context, (uint8_t *)ctx->user_data_ddt2, primary_table_size);

        uint64_t crc64;
        aaruf_crc64_final(crc64_context, &crc64);

        // Properly populate all header fields for multi-level DDT primary table
        ctx->user_data_ddt_header.identifier  = DeDuplicationTable2;
        ctx->user_data_ddt_header.type        = UserData;
        ctx->user_data_ddt_header.compression = None;
        // levels, tableLevel, previousLevelOffset, negative, overflow, blockAlignmentShift,
        // dataShift, tableShift, sizeType, entries, blocks, start are already set during creation
        ctx->user_data_ddt_header.crc64       = crc64;
        ctx->user_data_ddt_header.cmpCrc64    = crc64;
        ctx->user_data_ddt_header.length      = primary_table_size;
        ctx->user_data_ddt_header.cmpLength   = primary_table_size;

        TRACE("Calculated CRC64 for primary DDT: 0x%16lX", crc64);
    }

    // First write the DDT header
    fseek(ctx->imageStream, ctx->primary_ddt_offset, SEEK_SET);

    size_t headerWritten = fwrite(&ctx->user_data_ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream);
    if(headerWritten != 1)
    {
        TRACE("Failed to write primary DDT header to file");
        return AARUF_ERROR_CANNOT_WRITE_HEADER;
    }

    // Then write the table data (position is already after the header)
    size_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

    // Write the primary table data
    size_t written_bytes = 0;
    written_bytes        = fwrite(ctx->user_data_ddt2, primary_table_size, 1, ctx->imageStream);

    if(written_bytes == 1)
    {
        TRACE("Successfully wrote primary DDT header and table to file (%" PRIu64 " entries, %zu bytes)",
              ctx->user_data_ddt_header.entries, primary_table_size);

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing primary DDT index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == DeDuplicationTable2 && entry->dataType == UserData)
            {
                TRACE("Found existing primary DDT index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add primary DDT to index
        TRACE("Adding primary DDT to index");
        IndexEntry primary_ddt_entry;
        primary_ddt_entry.blockType = DeDuplicationTable2;
        primary_ddt_entry.dataType  = UserData;
        primary_ddt_entry.offset    = ctx->primary_ddt_offset;

        utarray_push_back(ctx->index_entries, &primary_ddt_entry);
        ctx->dirty_index_block = true;
        TRACE("Added primary DDT index entry at offset %" PRIu64, ctx->primary_ddt_offset);
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
static int32_t write_single_level_ddt(aaruformat_context *ctx)
{
    // Write the single level DDT table block aligned just after the header
    if(ctx->user_data_ddt_header.tableShift != 0 || ctx->user_data_ddt2 == NULL) return AARUF_STATUS_OK;

    TRACE("Writing single-level DDT table to file");

    // Calculate CRC64 of the primary DDT table data
    const size_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

    // Properly populate all header fields
    ctx->user_data_ddt_header.identifier          = DeDuplicationTable2;
    ctx->user_data_ddt_header.type                = UserData;
    ctx->user_data_ddt_header.compression         = ctx->compression_enabled ? Lzma : None;
    ctx->user_data_ddt_header.levels              = 1;  // Single level
    ctx->user_data_ddt_header.tableLevel          = 0;  // Top level
    ctx->user_data_ddt_header.previousLevelOffset = 0;  // No previous level for single-level DDT
    // negative and overflow are already set during creation
    // blockAlignmentShift, dataShift, tableShift, sizeType, entries, blocks, start are already set
    ctx->user_data_ddt_header.length              = primary_table_size;
    ctx->user_data_ddt_header.cmpLength           = primary_table_size;

    ctx->user_data_ddt_header.crc64 = aaruf_crc64_data((uint8_t *)ctx->user_data_ddt2, primary_table_size);

    TRACE("Calculated CRC64 for single-level DDT: 0x%16lX", ctx->user_data_ddt_header.crc64);

    uint8_t *cmp_buffer                              = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(ctx->user_data_ddt_header.compression == None)
    {

        cmp_buffer                         = (uint8_t *)ctx->user_data_ddt2;
        ctx->user_data_ddt_header.cmpCrc64 = ctx->user_data_ddt_header.crc64;
    }
    else
    {
        cmp_buffer = malloc((size_t)ctx->user_data_ddt_header.length * 2);  // Allocate double size for compression
        if(cmp_buffer == NULL)
        {
            TRACE("Failed to allocate memory for secondary DDT v2 compression");
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }

        size_t dst_size   = (size_t)ctx->user_data_ddt_header.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(cmp_buffer, &dst_size, (uint8_t *)ctx->user_data_ddt2,
                                 ctx->user_data_ddt_header.length, lzma_properties, &props_size, 9, ctx->lzma_dict_size,
                                 4, 0, 2, 273, 8);

        ctx->user_data_ddt_header.cmpLength = (uint32_t)dst_size;

        if(ctx->user_data_ddt_header.cmpLength >= ctx->user_data_ddt_header.length)
        {
            ctx->user_data_ddt_header.compression = None;
            free(cmp_buffer);

            cmp_buffer = (uint8_t *)ctx->user_data_ddt2;
        }
    }

    if(ctx->user_data_ddt_header.compression == None)
    {
        ctx->user_data_ddt_header.cmpLength = ctx->user_data_ddt_header.length;
        ctx->user_data_ddt_header.cmpCrc64  = ctx->user_data_ddt_header.crc64;
    }
    else
        ctx->user_data_ddt_header.cmpCrc64 =
            aaruf_crc64_data(cmp_buffer, (uint32_t)ctx->user_data_ddt_header.cmpLength);

    if(ctx->user_data_ddt_header.compression == Lzma) ctx->user_data_ddt_header.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write the DDT header first
    fseek(ctx->imageStream, 0, SEEK_END);
    long           ddt_position   = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(ddt_position & alignment_mask)
    {
        const uint64_t aligned_position = ddt_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        ddt_position = aligned_position;
    }

    const size_t header_written = fwrite(&ctx->user_data_ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream);
    if(header_written != 1)
    {
        TRACE("Failed to write single-level DDT header to file");
        return AARUF_ERROR_CANNOT_WRITE_HEADER;
    }

    // Write the primary table data
    size_t written_bytes = 0;
    if(ctx->user_data_ddt_header.compression == Lzma)
        fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

    written_bytes = fwrite(cmp_buffer, ctx->user_data_ddt_header.cmpLength, 1, ctx->imageStream);

    if(written_bytes == 1)
    {
        TRACE("Successfully wrote single-level DDT header and table to file (%" PRIu64
              " entries, %zu bytes, %zu compressed bytes)",
              ctx->user_data_ddt_header.entries, ctx->user_data_ddt_header.length, ctx->user_data_ddt_header.cmpLength);

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing single-level DDT index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == DeDuplicationTable2 && entry->dataType == UserData)
            {
                TRACE("Found existing single-level DDT index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add single-level DDT to index
        TRACE("Adding single-level DDT to index");
        IndexEntry single_ddt_entry;
        single_ddt_entry.blockType = DeDuplicationTable2;
        single_ddt_entry.dataType  = UserData;
        single_ddt_entry.offset    = ddt_position;

        utarray_push_back(ctx->index_entries, &single_ddt_entry);
        ctx->dirty_index_block = true;
        TRACE("Added single-level DDT index entry at offset %" PRIu64, ddt_position);
    }
    else
        TRACE("Failed to write single-level DDT table data to file");

    return AARUF_STATUS_OK;
}

/**
 * @brief Converts tape DDT hash table to array format and writes it as a single-level DDT.
 *
 * This function is specifically designed for tape media images where sectors have been tracked
 * using a sparse hash table (UTHASH) during write operations. It converts the hash-based tape
 * DDT into a traditional array-based DDT structure suitable for serialization to disk. The
 * function performs a complete transformation from the sparse hash representation to a dense
 * array representation, then delegates the actual write operation to write_single_level_ddt().
 *
 * The conversion process involves:
 * 1. Validating the context is for tape media
 * 2. Scanning the hash table to determine the maximum sector address (key)
 * 3. Allocating a contiguous array large enough to hold all entries up to max_key
 * 4. Populating the array by copying hash table entries to their corresponding indices
 * 5. Initializing a DDT v2 header with appropriate metadata
 * 6. Calling write_single_level_ddt() to serialize the DDT to disk
 *
 * **Hash Table to Array Conversion:**
 * The tape DDT hash table uses sector addresses as keys and DDT entries as values. This function
 * creates a zero-initialized array of size (max_key + 1) and copies each hash entry to
 * array[entry->key] = entry->value. Sectors not present in the hash table remain as zero entries
 * in the array, which indicates SectorStatusNotDumped in the DDT format.
 *
 * **Memory Allocation:**
 * The function always uses BigDdtSizeType (32-bit entries) for tape DDTs, allocating
 * (max_key + 1) * sizeof(uint32_t) bytes. This provides sufficient capacity for the 28-bit
 * data + 4-bit status encoding used in tape DDT entries.
 *
 * **DDT Header Configuration:**
 * The userDataDdtHeader is configured for a single-level DDT v2 structure:
 * - identifier: DeDuplicationTable2
 * - type: UserData
 * - compression: Determined by ctx->compression_enabled (Lzma or None)
 * - levels: 1 (single-level structure)
 * - tableLevel: 0 (top-level table)
 * - tableShift: 0 (no multi-level indirection)
 * - sizeType: BigDdtSizeType (32-bit entries)
 * - entries/blocks: max_key + 1
 * - negative/overflow: 0 (not used for tape)
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL and must be in write mode.
 *            The context must have is_tape set to true and tapeDdt hash table populated.
 *            The ctx->userDataDdtBig array will be allocated and populated by this function.
 *            The ctx->userDataDdtHeader will be initialized with DDT metadata.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully converted and wrote the tape DDT. This occurs when:
 *         - The context is valid and is_tape is true
 *         - Memory allocation for the DDT array succeeds
 *         - The hash table entries are successfully copied to the array
 *         - write_single_level_ddt() completes successfully
 *         - The DDT is written to disk and indexed
 *
 * @retval AARUF_STATUS_INVALID_CONTEXT (-2) The context is not for tape media. This occurs when:
 *         - ctx->is_tape is false
 *         - This function was called for a disk/optical image instead of tape
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - calloc() fails to allocate the userDataDdtBig array
 *         - Insufficient system memory for (max_key + 1) * 4 bytes
 *
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER (-21) Writing the DDT failed. This can occur when:
 *         - write_single_level_ddt() fails to write the DDT header
 *         - File I/O errors prevent writing the DDT data
 *         - Disk full or other storage errors
 *         - This error is propagated from write_single_level_ddt()
 *
 * @note This function is only called during image finalization (aaruf_close) for tape images.
 *       It should not be called for disk or optical media images.
 *
 * @note Hash Table Iteration:
 *       - Uses HASH_ITER macro from UTHASH to safely traverse all entries
 *       - Finds maximum key in first pass to determine array size
 *       - Copies entries in second pass to populate the array
 *       - Empty (zero) array slots represent sectors not written to tape
 *
 * @note Memory Ownership:
 *       - Allocates ctx->userDataDdtBig which becomes owned by the context
 *       - The allocated array is freed during context cleanup (not in this function)
 *       - The original hash table (ctx->tapeDdt) is freed separately during cleanup
 *
 * @note Single-Level DDT Choice:
 *       - Tape DDTs always use single-level structure (tableShift = 0)
 *       - Multi-level DDTs are not used because tape access patterns are typically sparse
 *       - The hash table already provides efficient sparse storage during write
 *       - Conversion to dense array only happens once at close time
 *
 * @note Compression:
 *       - The actual compression is handled by write_single_level_ddt()
 *       - Compression type is determined by ctx->compression_enabled flag
 *       - If enabled, LZMA compression is applied to the DDT array
 *       - Compression may be disabled if it doesn't reduce size
 *
 * @warning The function assumes tapeDdt hash table is properly populated. An empty hash table
 *          will result in a DDT with a single zero entry (max_key = 0, entries = 1).
 *
 * @warning This function modifies ctx->userDataDdtHeader and ctx->userDataDdtBig. These must
 *          not be modified by other code during the close operation.
 *
 * @warning The allocated array size is (max_key + 1), which could be very large if tape sectors
 *          have high addresses with sparse distribution. Memory usage should be considered.
 *
 * @see set_ddt_tape() for how entries are added to the hash table during write operations
 * @see write_single_level_ddt() for the actual DDT serialization logic
 * @see TapeDdtHashEntry for the hash table entry structure
 * @internal
 */
static int32_t write_tape_ddt(aaruformat_context *ctx)
{
    if(!ctx->is_tape) return AARUF_STATUS_INVALID_CONTEXT;

    // Traverse the tape DDT uthash and find the biggest key
    uint64_t          max_key = 0;
    TapeDdtHashEntry *entry, *tmp;
    HASH_ITER(hh, ctx->tape_ddt, entry, tmp)
    if(entry->key > max_key) max_key = entry->key;

    // Initialize context user data DDT header
    ctx->user_data_ddt_header.identifier          = DeDuplicationTable2;
    ctx->user_data_ddt_header.type                = UserData;
    ctx->user_data_ddt_header.compression         = ctx->compression_enabled ? Lzma : None;
    ctx->user_data_ddt_header.levels              = 1;  // Single level
    ctx->user_data_ddt_header.tableLevel          = 0;  // Top level
    ctx->user_data_ddt_header.previousLevelOffset = 0;  // No previous level for single-level DDT
    ctx->user_data_ddt_header.negative            = 0;
    ctx->user_data_ddt_header.overflow            = 0;
    ctx->user_data_ddt_header.tableShift          = 0;  // Single level
    ctx->user_data_ddt_header.entries             = max_key + 1;
    ctx->user_data_ddt_header.blocks              = max_key + 1;
    ctx->user_data_ddt_header.start               = 0;
    ctx->user_data_ddt_header.length              = ctx->user_data_ddt_header.entries * sizeof(uint64_t);
    ctx->user_data_ddt_header.cmpLength           = ctx->user_data_ddt_header.length;

    // Initialize memory for user data DDT
    ctx->user_data_ddt2 = calloc(ctx->user_data_ddt_header.entries, sizeof(uint64_t));
    if(ctx->user_data_ddt2 == NULL)
    {
        TRACE("Failed to allocate memory for tape DDT table");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Populate user data DDT from tape DDT uthash
    HASH_ITER(hh, ctx->tape_ddt, entry, tmp)
    if(entry->key < ctx->user_data_ddt_header.blocks) ctx->user_data_ddt2[entry->key] = entry->value;

    // Do not repeat code
    return write_single_level_ddt(ctx);
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
static void write_checksum_block(aaruformat_context *ctx)
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
    alignment_mask         = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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

    // Remove any previously existing index entries of the same type before adding
    TRACE("Removing any previously existing checksum block index entries");
    for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
    {
        const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
        if(entry && entry->blockType == ChecksumBlock && entry->dataType == 0)
        {
            TRACE("Found existing checksum block index entry at position %d, removing", k);
            utarray_erase(ctx->index_entries, k, 1);
        }
    }

    // Add checksum block to index
    TRACE("Adding checksum block to index");
    IndexEntry checksum_index_entry;
    checksum_index_entry.blockType = ChecksumBlock;
    checksum_index_entry.dataType  = 0;
    checksum_index_entry.offset    = checksum_position;

    utarray_push_back(ctx->index_entries, &checksum_index_entry);
    ctx->dirty_index_block = true;
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
static void write_tracks_block(aaruformat_context *ctx)
{
    // Write tracks block
    if(ctx->tracks_header.entries <= 0 || ctx->track_entries == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long     tracks_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    uint64_t alignment_mask  = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(tracks_position & alignment_mask)
    {
        uint64_t aligned_position = tracks_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        tracks_position = aligned_position;
    }

    TRACE("Writing tracks block at position %ld", tracks_position);
    // Write header
    if(fwrite(&ctx->tracks_header, sizeof(TracksHeader), 1, ctx->imageStream) == 1)
    {
        // Write entries
        size_t written_entries =
            fwrite(ctx->track_entries, sizeof(TrackEntry), ctx->tracks_header.entries, ctx->imageStream);

        if(written_entries == ctx->tracks_header.entries)
        {
            TRACE("Successfully wrote tracks block with %u entries", ctx->tracks_header.entries);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing tracks block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == TracksBlock && entry->dataType == 0)
                {
                    TRACE("Found existing tracks block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add tracks block to index
            TRACE("Adding tracks block to index");

            IndexEntry tracks_index_entry;
            tracks_index_entry.blockType = TracksBlock;
            tracks_index_entry.dataType  = 0;
            tracks_index_entry.offset    = tracks_position;
            utarray_push_back(ctx->index_entries, &tracks_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added tracks block index entry at offset %" PRIu64, tracks_position);
        }
    }
}

/**
 * @brief Serialize a MODE 2 (XA) subheaders data block.
 *
 * When Compact Disc Mode 2 form sectors are present, optional 8-byte subheaders (one per logical
 * sector including negative / overflow ranges) are stored in an in-memory buffer. This function
 * writes that buffer as a DataBlock of type CompactDiscMode2Subheader with CRC64 (compression enabled if configured)
 * and adds an IndexEntry.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode; ctx->mode2_subheaders must
 *            point to a buffer sized for the described sector span.
 * @internal
 */
static void write_mode2_subheaders_block(aaruformat_context *ctx)
{
    // Write MODE 2 subheader data block
    if(ctx->mode2_subheaders == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long     mode2_subheaders_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    uint64_t alignment_mask            = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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
    subheaders_block.compression = ctx->compression_enabled ? Lzma : None;
    subheaders_block.length =
        (uint32_t)(ctx->user_data_ddt_header.negative + ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow) *
        8;

    // Calculate CRC64
    subheaders_block.crc64 = aaruf_crc64_data(ctx->mode2_subheaders, subheaders_block.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(subheaders_block.compression == None)
    {
        buffer                    = ctx->mode2_subheaders;
        subheaders_block.cmpCrc64 = subheaders_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)subheaders_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for MODE 2 subheaders compression");
            return;
        }

        size_t dst_size   = (size_t)subheaders_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->mode2_subheaders, subheaders_block.length, lzma_properties,
                                 &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        subheaders_block.cmpLength = (uint32_t)dst_size;

        if(subheaders_block.cmpLength >= subheaders_block.length)
        {
            subheaders_block.compression = None;
            free(buffer);
            buffer = ctx->mode2_subheaders;
        }
    }

    if(subheaders_block.compression == None)
    {
        subheaders_block.cmpLength = subheaders_block.length;
        subheaders_block.cmpCrc64  = subheaders_block.crc64;
    }
    else
        subheaders_block.cmpCrc64 = aaruf_crc64_data(buffer, subheaders_block.cmpLength);

    const size_t length_to_write = subheaders_block.cmpLength;
    if(subheaders_block.compression == Lzma) subheaders_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&subheaders_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(subheaders_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote MODE 2 subheaders block (%" PRIu64 " bytes)", subheaders_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing MODE 2 subheaders block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == CompactDiscMode2Subheader)
                {
                    TRACE("Found existing MODE 2 subheaders block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add MODE 2 subheaders block to index
            TRACE("Adding MODE 2 subheaders block to index");
            IndexEntry mode2_subheaders_index_entry;
            mode2_subheaders_index_entry.blockType = DataBlock;
            mode2_subheaders_index_entry.dataType  = CompactDiscMode2Subheader;
            mode2_subheaders_index_entry.offset    = mode2_subheaders_position;
            utarray_push_back(ctx->index_entries, &mode2_subheaders_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added MODE 2 subheaders block index entry at offset %" PRIu64, mode2_subheaders_position);
        }
    }

    if(subheaders_block.compression == Lzma) free(buffer);
}

/**
 * @brief Serialize the optional CD sector prefix block.
 *
 * The sector prefix corresponds to the leading bytes of a raw CD sector (synchronization pattern,
 * address / header in MSF format and the mode byte) that precede the user data and any ECC/ECCP
 * fields. It is unrelated to subchannel (P–W) data, which is handled separately. If prefix data
 * was collected (ctx->sector_prefix != NULL), this writes a DataBlock of type CdSectorPrefix
 * containing exactly the bytes accumulated up to sector_prefix_offset. The block is CRC64
 * protected, compressed if enabled, aligned to the DDT block boundary and indexed.
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
static void write_sector_prefix(aaruformat_context *ctx)
{
    if(ctx->sector_prefix == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long     prefix_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    uint64_t alignment_mask  = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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
    prefix_block.compression = ctx->compression_enabled ? Lzma : None;
    prefix_block.length      = (uint32_t)ctx->sector_prefix_offset;

    // Calculate CRC64
    prefix_block.crc64 = aaruf_crc64_data(ctx->sector_prefix, prefix_block.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(prefix_block.compression == None)
    {
        buffer                = ctx->sector_prefix;
        prefix_block.cmpCrc64 = prefix_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)prefix_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for CD sector prefix compression");
            return;
        }

        size_t dst_size   = (size_t)prefix_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_prefix, prefix_block.length, lzma_properties,
                                 &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        prefix_block.cmpLength = (uint32_t)dst_size;

        if(prefix_block.cmpLength >= prefix_block.length)
        {
            prefix_block.compression = None;
            free(buffer);
            buffer = ctx->sector_prefix;
        }
    }

    if(prefix_block.compression == None)
    {
        prefix_block.cmpLength = prefix_block.length;
        prefix_block.cmpCrc64  = prefix_block.crc64;
    }
    else
        prefix_block.cmpCrc64 = aaruf_crc64_data(buffer, prefix_block.cmpLength);

    const size_t length_to_write = prefix_block.cmpLength;
    if(prefix_block.compression == Lzma) prefix_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&prefix_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(prefix_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote CD sector prefix block (%" PRIu64 " bytes)", prefix_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing CD sector prefix block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == CdSectorPrefix)
                {
                    TRACE("Found existing CD sector prefix block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add prefix block to index
            TRACE("Adding CD sector prefix block to index");
            IndexEntry prefix_index_entry;
            prefix_index_entry.blockType = DataBlock;
            prefix_index_entry.dataType  = CdSectorPrefix;
            prefix_index_entry.offset    = prefix_position;
            utarray_push_back(ctx->index_entries, &prefix_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added CD sector prefix block index entry at offset %" PRIu64, prefix_position);
        }
    }

    if(prefix_block.compression == Lzma) free(buffer);
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
 *   - Compression is applied if enabled; CRC64 is calculated on the raw suffix stream for integrity.
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
static void write_sector_suffix(aaruformat_context *ctx)
{
    if(ctx->sector_suffix == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           suffix_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask  = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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
    suffix_block.compression = ctx->compression_enabled ? Lzma : None;
    suffix_block.length      = (uint32_t)ctx->sector_suffix_offset;

    // Calculate CRC64
    suffix_block.crc64 = aaruf_crc64_data(ctx->sector_suffix, suffix_block.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(suffix_block.compression == None)
    {
        buffer                = ctx->sector_suffix;
        suffix_block.cmpCrc64 = suffix_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)suffix_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for CD sector suffix compression");
            return;
        }

        size_t dst_size   = (size_t)suffix_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_suffix, suffix_block.length, lzma_properties,
                                 &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        suffix_block.cmpLength = (uint32_t)dst_size;

        if(suffix_block.cmpLength >= suffix_block.length)
        {
            suffix_block.compression = None;
            free(buffer);
            buffer = ctx->sector_suffix;
        }
    }

    if(suffix_block.compression == None)
    {
        suffix_block.cmpLength = suffix_block.length;
        suffix_block.cmpCrc64  = suffix_block.crc64;
    }
    else
        suffix_block.cmpCrc64 = aaruf_crc64_data(buffer, suffix_block.cmpLength);

    const size_t length_to_write = suffix_block.cmpLength;
    if(suffix_block.compression == Lzma) suffix_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&suffix_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(suffix_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote CD sector suffix block (%" PRIu64 " bytes)", suffix_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing CD sector suffix block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == CdSectorSuffix)
                {
                    TRACE("Found existing CD sector suffix block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add suffix block to index
            TRACE("Adding CD sector suffix block to index");
            IndexEntry suffix_index_entry;
            suffix_index_entry.blockType = DataBlock;
            suffix_index_entry.dataType  = CdSectorSuffix;
            suffix_index_entry.offset    = suffix_position;
            utarray_push_back(ctx->index_entries, &suffix_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added CD sector suffix block index entry at offset %" PRIu64, suffix_position);
        }
    }

    if(suffix_block.compression == Lzma) free(buffer);
}

/**
 * @brief Serialize the per-sector CD prefix status / index DeDuplication Table (DDT v2, prefix variant).
 *
 * This DDT records for each logical sector (including negative and overflow ranges) an optional
 * index into the stored 16‑byte prefix capture buffer plus a 4-bit status code. It is written only
 * if at least one prefix status or captured prefix was recorded (i.e., the in-memory DDT array exists).
 *
 * Encoding:
 *   Bits 63..61 : SectorStatus enum value (see enums.h, values already positioned for v2 layout).
 *   Bits 60..0  : Index of the 16-byte prefix chunk inside the CdSectorPrefix data block,
 *                 or 0 when no external prefix bytes were stored (status applies to a generated/implicit prefix).
 *
 * Notes:
 *   - Unlike DDT v1, there are no CD_XFIX_MASK / CD_DFIX_MASK macros used here. The bit layout is compact
 *     and directly encoded when writing (status values are pre-shifted where needed in write.c).
 *   - The table length equals (negative + Sectors + overflow) * entrySize.
 *   - dataShift is set to 4 (2^4 = 16) expressing the granularity of referenced prefix units.
 *   - Compression is applied if enabled; crc64/cmpCrc64 protect the raw table bytes.
 *   - Idempotent: if an index entry of type DeDuplicationTable2 + CdSectorPrefixCorrected already exists
 *     the function returns immediately.
 *
 * Alignment: The table is block-aligned using the same blockAlignmentShift as user data DDTs.
 * Indexing: An IndexEntry is appended on success so readers can locate and parse the table.
 *
 * @param ctx Pointer to a valid aaruformatContext in write mode (must not be NULL).
 * @internal
 */
static void write_sector_prefix_ddt(aaruformat_context *ctx)
{
    if(ctx->sector_prefix_ddt2 == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           prefix_ddt_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask      = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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
    ddt_header2.compression         = ctx->compression_enabled ? Lzma : None;
    ddt_header2.levels              = 1;
    ddt_header2.tableLevel          = 0;
    ddt_header2.negative            = ctx->user_data_ddt_header.negative;
    ddt_header2.overflow            = ctx->user_data_ddt_header.overflow;
    ddt_header2.blockAlignmentShift = ctx->user_data_ddt_header.blockAlignmentShift;
    ddt_header2.dataShift           = ctx->user_data_ddt_header.dataShift;
    ddt_header2.tableShift          = 0;  // Single-level DDT
    ddt_header2.entries =
        ctx->image_info.Sectors + ctx->user_data_ddt_header.negative + ctx->user_data_ddt_header.overflow;
    ddt_header2.blocks = ctx->user_data_ddt_header.blocks;
    ddt_header2.start  = 0;
    ddt_header2.length = ddt_header2.entries * sizeof(uint64_t);
    // Calculate CRC64
    ddt_header2.crc64  = aaruf_crc64_data((uint8_t *)ctx->sector_prefix_ddt2, (uint32_t)ddt_header2.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(ddt_header2.compression == None)
    {
        buffer               = (uint8_t *)ctx->sector_prefix_ddt2;
        ddt_header2.cmpCrc64 = ddt_header2.crc64;
    }
    else
    {
        buffer = malloc((size_t)ddt_header2.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for sector prefix DDT v2 compression");
            return;
        }

        size_t dst_size   = (size_t)ddt_header2.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, (uint8_t *)ctx->sector_prefix_ddt2, ddt_header2.length,
                                 lzma_properties, &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        ddt_header2.cmpLength = (uint32_t)dst_size;

        if(ddt_header2.cmpLength >= ddt_header2.length)
        {
            ddt_header2.compression = None;
            free(buffer);
            buffer = (uint8_t *)ctx->sector_prefix_ddt2;
        }
    }

    if(ddt_header2.compression == None)
    {
        ddt_header2.cmpLength = ddt_header2.length;
        ddt_header2.cmpCrc64  = ddt_header2.crc64;
    }
    else
        ddt_header2.cmpCrc64 = aaruf_crc64_data(buffer, (uint32_t)ddt_header2.cmpLength);

    const size_t length_to_write = ddt_header2.cmpLength;
    if(ddt_header2.compression == Lzma) ddt_header2.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&ddt_header2, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
    {
        if(ddt_header2.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote sector prefix DDT v2 (%" PRIu64 " bytes)", ddt_header2.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing sector prefix DDT v2 index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DeDuplicationTable2 && entry->dataType == CdSectorPrefix)
                {
                    TRACE("Found existing sector prefix DDT v2 index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add prefix block to index
            TRACE("Adding sector prefix DDT v2 to index");
            IndexEntry prefix_ddt_index_entry;
            prefix_ddt_index_entry.blockType = DeDuplicationTable2;
            prefix_ddt_index_entry.dataType  = CdSectorPrefix;
            prefix_ddt_index_entry.offset    = prefix_ddt_position;
            utarray_push_back(ctx->index_entries, &prefix_ddt_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added sector prefix DDT v2 index entry at offset %" PRIu64, prefix_ddt_position);
        }
    }

    if(ddt_header2.compression == Lzma) free(buffer);
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
 * Encoding (DDT v2 semantics):
 *   Bits 63..61 : SectorStatus enumeration (already aligned for direct storage; no legacy masks used).
 *   Bits 60..0  : Index referencing a suffix unit of size 288 bytes (2^dataShift granularity),
 *                 or 0 when the sector uses an implicit / regenerated suffix (no external data captured).
 *
 * Characteristics & constraints:
 *   - Only DDT v2 is supported here; no fallback or mixed-mode emission with v1 occurs.
 *   - Table length = (negative + total Sectors + overflow) * sizeof(uint16_t).
 *   - dataShift mirrors userDataDdtHeader.dataShift (expressing granularity for index referencing).
 *   - Single-level table (levels = 1, tableLevel = 0, tableShift = 0).
 *   - Compression is applied if enabled; CRC64 protects the table bytes.
 *   - Alignment: The table is aligned to 2^(blockAlignmentShift) before writing to guarantee block boundary access.
 *   - Idempotence: If sectorSuffixDdt2 is NULL the function is a no-op (indicating no suffix anomalies captured).
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
 *   - ctx->sectorSuffixDdt2 must point to a fully populated contiguous array of uint16_t entries.
 *
 * @param ctx Active aaruformatContext being finalized.
 * @internal
 */
static void write_sector_suffix_ddt(aaruformat_context *ctx)
{
    if(ctx->sector_suffix_ddt2 == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           suffix_ddt_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask      = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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
    ddt_header2.compression         = ctx->compression_enabled ? Lzma : None;
    ddt_header2.levels              = 1;
    ddt_header2.tableLevel          = 0;
    ddt_header2.negative            = ctx->user_data_ddt_header.negative;
    ddt_header2.overflow            = ctx->user_data_ddt_header.overflow;
    ddt_header2.blockAlignmentShift = ctx->user_data_ddt_header.blockAlignmentShift;
    ddt_header2.dataShift           = ctx->user_data_ddt_header.dataShift;
    ddt_header2.tableShift          = 0;  // Single-level DDT
    ddt_header2.entries =
        ctx->image_info.Sectors + ctx->user_data_ddt_header.negative + ctx->user_data_ddt_header.overflow;
    ddt_header2.blocks = ctx->user_data_ddt_header.blocks;
    ddt_header2.start  = 0;
    ddt_header2.length = ddt_header2.entries * sizeof(uint64_t);
    // Calculate CRC64
    ddt_header2.crc64  = aaruf_crc64_data((uint8_t *)ctx->sector_suffix_ddt2, (uint32_t)ddt_header2.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(ddt_header2.compression == None)
    {
        buffer               = (uint8_t *)ctx->sector_suffix_ddt2;
        ddt_header2.cmpCrc64 = ddt_header2.crc64;
    }
    else
    {
        buffer = malloc((size_t)ddt_header2.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for sector suffix DDT v2 compression");
            return;
        }

        size_t dst_size   = (size_t)ddt_header2.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, (uint8_t *)ctx->sector_suffix_ddt2, ddt_header2.length,
                                 lzma_properties, &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        ddt_header2.cmpLength = (uint32_t)dst_size;

        if(ddt_header2.cmpLength >= ddt_header2.length)
        {
            ddt_header2.compression = None;
            free(buffer);
            buffer = (uint8_t *)ctx->sector_suffix_ddt2;
        }
    }

    if(ddt_header2.compression == None)
    {
        ddt_header2.cmpLength = ddt_header2.length;
        ddt_header2.cmpCrc64  = ddt_header2.crc64;
    }
    else
        ddt_header2.cmpCrc64 = aaruf_crc64_data(buffer, (uint32_t)ddt_header2.cmpLength);

    const size_t length_to_write = ddt_header2.cmpLength;
    if(ddt_header2.compression == Lzma) ddt_header2.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&ddt_header2, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
    {
        if(ddt_header2.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote sector suffix DDT v2 (%" PRIu64 " bytes)", ddt_header2.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing sector suffix DDT v2 block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DeDuplicationTable2 && entry->dataType == CdSectorSuffix)
                {
                    TRACE("Found existing sector suffix DDT v2 index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add suffix block to index
            TRACE("Adding sector suffix DDT v2 to index");
            IndexEntry suffix_ddt_index_entry;
            suffix_ddt_index_entry.blockType = DeDuplicationTable2;
            suffix_ddt_index_entry.dataType  = CdSectorSuffix;
            suffix_ddt_index_entry.offset    = suffix_ddt_position;
            utarray_push_back(ctx->index_entries, &suffix_ddt_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added sector suffix DDT v2 index entry at offset %" PRIu64, suffix_ddt_position);
        }
    }

    if(ddt_header2.compression == Lzma) free(buffer);
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
 * Compression is conditionally applied based on media type and compression settings:
 *   - **Optical Disc:** When compression is enabled, applies Claunia Subchannel Transform (CST)
 *     followed by LZMA compression (LzmaClauniaSubchannelTransform). If the compressed size is
 *     not smaller than the original, falls back to uncompressed storage.
 *   - **Block Media:** Always attempts LZMA compression for tag data. If the compressed size is
 *     not smaller than the original, falls back to uncompressed storage.
 * The data is written after a DataBlock header with CRC64 integrity protection. The write position
 * is aligned to the DDT block boundary (2^blockAlignmentShift) before serialization begins.
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
static void write_sector_subchannel(aaruformat_context *ctx)
{
    if(ctx->sector_subchannel == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    // Align index position to block boundary if needed
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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

    uint8_t *buffer                                  = ctx->sector_subchannel;
    bool     owns_buffer                             = false;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(ctx->image_info.MetadataMediaType == OpticalDisc)
    {
        subchannel_block.type   = CdSectorSubchannel;
        subchannel_block.length = (uint32_t)(ctx->user_data_ddt_header.negative + ctx->image_info.Sectors +
                                             ctx->user_data_ddt_header.overflow) *
                                  96;
        subchannel_block.cmpLength = subchannel_block.length;

        if(ctx->compression_enabled)
        {
            uint8_t *cst_buffer = malloc(subchannel_block.length);

            if(cst_buffer == NULL)
            {
                TRACE("Failed to allocate memory for Claunia Subchannel Transform output");
                return;
            }

            uint8_t *dst_buffer = malloc(subchannel_block.length);

            if(dst_buffer == NULL)
            {
                TRACE("Failed to allocate memory for LZMA output");
                free(cst_buffer);
                return;
            }

            aaruf_cst_transform(ctx->sector_subchannel, cst_buffer, subchannel_block.length);
            size_t dst_size   = subchannel_block.length;
            size_t props_size = LZMA_PROPERTIES_LENGTH;

            aaruf_lzma_encode_buffer(dst_buffer, &dst_size, cst_buffer, subchannel_block.length, lzma_properties,
                                     &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

            free(cst_buffer);

            if(dst_size < subchannel_block.length)
            {
                subchannel_block.compression = LzmaClauniaSubchannelTransform;
                subchannel_block.cmpLength   = (uint32_t)dst_size;
                buffer                       = dst_buffer;
                owns_buffer                  = true;
            }
            else
            {
                subchannel_block.compression = None;
                free(dst_buffer);
                subchannel_block.cmpLength = subchannel_block.length;
            }
        }
    }
    else if(ctx->image_info.MetadataMediaType == BlockMedia)
    {
        switch(ctx->image_info.MediaType)
        {
            case AppleProfile:
            case AppleFileWare:
                subchannel_block.type   = AppleProfileTag;
                subchannel_block.length = (uint32_t)(ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow) * 20;
                break;
            case AppleSonyDS:
            case AppleSonySS:
                subchannel_block.type   = AppleSonyTag;
                subchannel_block.length = (uint32_t)(ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow) * 12;
                break;
            case PriamDataTower:
                subchannel_block.type   = PriamDataTowerTag;
                subchannel_block.length = (uint32_t)(ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow) * 24;
                break;
            default:
                TRACE("Incorrect media type, not writing sector subchannel block");
                return;  // Incorrect media type
        }
        subchannel_block.cmpLength   = subchannel_block.length;
        subchannel_block.compression = Lzma;

        uint8_t *dst_buffer = malloc(subchannel_block.length);

        if(dst_buffer == NULL)
        {
            TRACE("Failed to allocate memory for LZMA output");
            return;
        }

        size_t dst_size   = subchannel_block.length;
        size_t props_size = LZMA_PROPERTIES_LENGTH;

        aaruf_lzma_encode_buffer(dst_buffer, &dst_size, ctx->sector_subchannel, subchannel_block.length,
                                 lzma_properties, &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        if(dst_size < subchannel_block.length)
        {
            subchannel_block.cmpLength = (uint32_t)dst_size;
            buffer                     = dst_buffer;
            owns_buffer                = true;
        }
        else
        {
            subchannel_block.compression = None;
            free(dst_buffer);
            subchannel_block.cmpLength = subchannel_block.length;
        }
    }
    else
    {
        TRACE("Incorrect media type, not writing sector subchannel block");
        return;  // Incorrect media type
    }

    // Calculate CRC64 for raw subchannel data and compressed payload when present
    subchannel_block.crc64 = aaruf_crc64_data(ctx->sector_subchannel, subchannel_block.length);
    if(subchannel_block.compression == None)
        subchannel_block.cmpCrc64 = subchannel_block.crc64;
    else
        subchannel_block.cmpCrc64 = aaruf_crc64_data(buffer, subchannel_block.cmpLength);

    const size_t length_to_write = subchannel_block.cmpLength;
    if(subchannel_block.compression != None) subchannel_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&subchannel_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(subchannel_block.compression != None) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote sector subchannel block (%" PRIu64 " bytes)", subchannel_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing subchannel block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == subchannel_block.type)
                {
                    TRACE("Found existing subchannel block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add subchannel block to index
            TRACE("Adding sector subchannel block to index");
            IndexEntry subchannel_index_entry;
            subchannel_index_entry.blockType = DataBlock;
            subchannel_index_entry.dataType  = subchannel_block.type;
            subchannel_index_entry.offset    = block_position;
            utarray_push_back(ctx->index_entries, &subchannel_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added sector subchannel block index entry at offset %" PRIu64, block_position);
        }
    }

    if(owns_buffer) free(buffer);
}

/**
 * @brief Serialize DVD long sector auxiliary data blocks to the image file.
 *
 * This function writes four separate data blocks containing DVD-specific auxiliary information
 * extracted from "long" DVD sectors. DVD long sectors contain additional fields beyond the 2048
 * bytes of user data, including sector identification, error detection, and copy protection
 * information. When writing DVD images with long sector support, these auxiliary fields are
 * stored separately from the main user data to optimize storage and enable selective access.
 *
 * The function is only invoked if all four auxiliary buffers have been populated during image
 * creation (sector_id, sector_ied, sector_cpr_mai, sector_edc). If any buffer is NULL, the
 * function returns immediately without writing anything, allowing DVD images without long sector
 * data to be created normally.
 *
 * **Four auxiliary data blocks written:**
 *
 * 1. **DVD Sector ID Block (DvdSectorId)**: 4 bytes per sector
 *    - Contains the sector ID field from DVD long sectors
 *    - Used for sector identification and addressing validation
 *
 * 2. **DVD Sector IED Block (DvdSectorIed)**: 2 bytes per sector
 *    - Contains the IED (ID Error Detection) field from DVD long sectors
 *    - Used for detecting errors in the sector ID field
 *
 * 3. **DVD Sector CPR/MAI Block (DvdSectorCprMai)**: 6 bytes per sector
 *    - Contains the CPR_MAI (Copyright Management Information) field
 *    - Used for copy protection and media authentication information
 *
 * 4. **DVD Sector EDC Block (DvdSectorEdc)**: 4 bytes per sector
 *    - Contains the EDC (Error Detection Code) field from DVD long sectors
 *    - Used for detecting errors in the sector data
 *
 * **Block structure for each auxiliary block:**
 * Each block consists of:
 *   1. BlockHeader containing identifier (DataBlock), type (DvdSectorId/IED/CprMai/Edc),
 *      compression (None), lengths, and CRC64 checksums
 *   2. Raw auxiliary data: concatenated fields from all sectors (including negative, normal,
 *      and overflow sectors)
 *
 * The total number of sectors includes negative sectors (for lead-in), normal image sectors,
 * and overflow sectors (for lead-out), calculated as:
 * total_sectors = negative + Sectors + overflow
 *
 * **Write sequence for each block:**
 * 1. Seek to end of file
 * 2. Align file position to block boundary (using blockAlignmentShift)
 * 3. Construct BlockHeader with appropriate type and calculated length
 * 4. Calculate CRC64 over the auxiliary data buffer
 * 5. Write BlockHeader (sizeof(BlockHeader) bytes)
 * 6. Write auxiliary data buffer (length bytes)
 * 7. On success, add IndexEntry to ctx->indexEntries
 *
 * **Alignment and file positioning:**
 * Before writing each block, the file position is moved to EOF and then aligned forward to the
 * next boundary satisfying (position & alignment_mask) == 0, where alignment_mask is derived
 * from ctx->userDataDdtHeader.blockAlignmentShift. This ensures all blocks begin on properly
 * aligned offsets for efficient I/O and compliance with the Aaru format specification.
 *
 * **Index registration:**
 * After successfully writing each block's header and data, an IndexEntry is appended to
 * ctx->indexEntries with:
 *   - blockType = DataBlock
 *   - dataType = DvdSectorId, DvdSectorIed, DvdSectorCprMai, or DvdSectorEdc
 *   - offset = the aligned file position where the BlockHeader was written
 *
 * **Error handling:**
 * Write errors (fwrite returning < 1) are silently ignored for individual blocks; no index entry
 * is added if a write fails, but iteration continues to attempt writing remaining blocks.
 * Diagnostic TRACE logs report success or failure for each block. The function does not propagate
 * error codes; higher-level close logic must validate overall integrity if needed.
 *
 * **No-op conditions:**
 * If any of the four auxiliary buffers is NULL, the function returns immediately without writing
 * anything. This is an all-or-nothing operation - either all four blocks are written or none.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->sector_id contains the ID fields from all DVD long sectors (may be NULL).
 *            ctx->sector_ied contains the IED fields from all DVD long sectors (may be NULL).
 *            ctx->sector_cpr_mai contains the CPR/MAI fields from all DVD long sectors (may be NULL).
 *            ctx->sector_edc contains the EDC fields from all DVD long sectors (may be NULL).
 *            ctx->imageStream must be open and writable. ctx->indexEntries must be initialized
 *            (utarray) to accept new index entries.
 *
 * @note DVD Long Sector Format:
 *       - Standard DVD sectors contain 2048 bytes of user data
 *       - Long DVD sectors include additional fields for error detection and copy protection
 *       - Total long sector size varies by DVD format (typically 2064-2076 bytes)
 *       - These auxiliary fields are critical for forensic imaging and copy-protected media
 *
 * @note Sector Coverage:
 *       - Negative sectors: Lead-in area before sector 0 (if present)
 *       - Normal sectors: Main data area (sectors 0 to Sectors-1)
 *       - Overflow sectors: Lead-out area after the main data (if present)
 *       - All three areas are included in the auxiliary data blocks
 *
 * @note Field Sizes:
 *       - ID field: 4 bytes per sector (sector identification)
 *       - IED field: 2 bytes per sector (ID error detection)
 *       - CPR/MAI field: 6 bytes per sector (copyright management)
 *       - EDC field: 4 bytes per sector (error detection code)
 *       - Total auxiliary data: 16 bytes per sector across all four blocks
 *
 * @note Memory Management:
 *       - The function allocates temporary buffers for compression when enabled
 *       - Auxiliary data buffers are managed by the caller
 *       - Compression buffers are freed after each block is written
 *       - Source data memory is freed later during context cleanup (aaruf_close)
 *
 * @note Use Cases:
 *       - Forensic imaging of DVD media requiring complete sector data
 *       - Preservation of copy-protected DVD content
 *       - Analysis of DVD error detection and correction information
 *       - Validation of DVD sector structure and integrity
 *       - Research into DVD copy protection mechanisms
 *
 * @note Order in Close Sequence:
 *       - DVD long sector blocks are typically written after user data but before metadata
 *       - The exact position in the file depends on what other blocks precede them
 *       - Index entries ensure blocks can be located during subsequent opens
 *       - All four blocks are written consecutively if present
 *
 * @warning The auxiliary data buffers must contain data for ALL sectors (negative + normal +
 *          overflow). Partial buffers or mismatched sizes will cause incorrect data to be
 *          written or buffer overruns.
 *
 * @warning Compression is applied if enabled. The blocks may be stored compressed or uncompressed
 *          depending on the compression_enabled setting and compression effectiveness.
 *
 * @warning If any of the four auxiliary buffers is NULL, the entire function is skipped.
 *          This is an all-or-nothing operation - either all four blocks are written or none.
 *
 * @see aaruf_write_sector_long() for writing individual DVD long sectors that populate these buffers.
 * @see BlockHeader for the block header structure definition.
 *
 * @internal
 */
void write_dvd_long_sector_blocks(aaruformat_context *ctx)
{
    if(ctx->sector_id == NULL || ctx->sector_ied == NULL || ctx->sector_cpr_mai == NULL || ctx->sector_edc == NULL)
        return;

    uint64_t total_sectors =
        ctx->user_data_ddt_header.negative + ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow;

    // Write DVD sector ID block
    fseek(ctx->imageStream, 0, SEEK_END);
    long           id_position    = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(id_position & alignment_mask)
    {
        const uint64_t aligned_position = id_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        id_position = aligned_position;
    }
    TRACE("Writing DVD sector ID block at position %ld", id_position);
    BlockHeader id_block = {0};
    id_block.identifier  = DataBlock;
    id_block.type        = DvdSectorId;
    id_block.compression = ctx->compression_enabled ? Lzma : None;
    id_block.length      = (uint32_t)total_sectors * 4;

    // Calculate CRC64
    id_block.crc64 = aaruf_crc64_data(ctx->sector_id, id_block.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(id_block.compression == None)
    {
        buffer            = ctx->sector_id;
        id_block.cmpCrc64 = id_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)id_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for DVD sector ID compression");
            return;
        }

        size_t dst_size   = (size_t)id_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_id, id_block.length, lzma_properties, &props_size, 9,
                                 ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        id_block.cmpLength = (uint32_t)dst_size;

        if(id_block.cmpLength >= id_block.length)
        {
            id_block.compression = None;
            free(buffer);
            buffer = ctx->sector_id;
        }
    }

    if(id_block.compression == None)
    {
        id_block.cmpLength = id_block.length;
        id_block.cmpCrc64  = id_block.crc64;
    }
    else
        id_block.cmpCrc64 = aaruf_crc64_data(buffer, id_block.cmpLength);

    size_t length_to_write = id_block.cmpLength;
    if(id_block.compression == Lzma) id_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&id_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(id_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote DVD sector ID block (%" PRIu64 " bytes)", id_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing DVD sector ID block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == DvdSectorId)
                {
                    TRACE("Found existing DVD sector ID block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add ID block to index
            TRACE("Adding DVD sector ID block to index");
            IndexEntry id_index_entry;
            id_index_entry.blockType = DataBlock;
            id_index_entry.dataType  = DvdSectorId;
            id_index_entry.offset    = id_position;
            utarray_push_back(ctx->index_entries, &id_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added DVD sector ID block index entry at offset %" PRIu64, id_position);
        }
    }

    if(id_block.compression == Lzma) free(buffer);

    // Write DVD sector IED block
    fseek(ctx->imageStream, 0, SEEK_END);
    long ied_position = ftell(ctx->imageStream);
    if(ied_position & alignment_mask)
    {
        const uint64_t aligned_position = ied_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        ied_position = aligned_position;
    }
    TRACE("Writing DVD sector IED block at position %ld", ied_position);
    BlockHeader ied_block = {0};
    ied_block.identifier  = DataBlock;
    ied_block.type        = DvdSectorIed;
    ied_block.compression = ctx->compression_enabled ? Lzma : None;
    ied_block.length      = (uint32_t)total_sectors * 2;
    // Calculate CRC64
    ied_block.crc64       = aaruf_crc64_data(ctx->sector_ied, ied_block.length);

    buffer = NULL;

    if(ied_block.compression == None)
    {
        buffer             = ctx->sector_ied;
        ied_block.cmpCrc64 = ied_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)ied_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for DVD sector IED compression");
            return;
        }

        size_t dst_size   = (size_t)ied_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_ied, ied_block.length, lzma_properties, &props_size, 9,
                                 ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        ied_block.cmpLength = (uint32_t)dst_size;

        if(ied_block.cmpLength >= ied_block.length)
        {
            ied_block.compression = None;
            free(buffer);
            buffer = ctx->sector_ied;
        }
    }

    if(ied_block.compression == None)
    {
        ied_block.cmpLength = ied_block.length;
        ied_block.cmpCrc64  = ied_block.crc64;
    }
    else
        ied_block.cmpCrc64 = aaruf_crc64_data(buffer, ied_block.cmpLength);

    length_to_write = ied_block.cmpLength;
    if(ied_block.compression == Lzma) ied_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&ied_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(ied_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote DVD sector IED block (%" PRIu64 " bytes)", ied_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing DVD sector IED block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == DvdSectorIed)
                {
                    TRACE("Found existing DVD sector IED block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add IED block to index
            TRACE("Adding DVD sector IED block to index");
            IndexEntry ied_index_entry;
            ied_index_entry.blockType = DataBlock;
            ied_index_entry.dataType  = DvdSectorIed;
            ied_index_entry.offset    = ied_position;
            utarray_push_back(ctx->index_entries, &ied_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added DVD sector IED block index entry at offset %" PRIu64, ied_position);
        }
    }

    if(ied_block.compression == Lzma) free(buffer);

    // Write DVD sector CPR/MAI block
    fseek(ctx->imageStream, 0, SEEK_END);
    long cpr_mai_position = ftell(ctx->imageStream);
    if(cpr_mai_position & alignment_mask)
    {
        const uint64_t aligned_position = cpr_mai_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        cpr_mai_position = aligned_position;
    }
    TRACE("Writing DVD sector CPR/MAI block at position %ld", cpr_mai_position);
    BlockHeader cpr_mai_block = {0};
    cpr_mai_block.identifier  = DataBlock;
    cpr_mai_block.type        = DvdSectorCprMai;
    cpr_mai_block.compression = ctx->compression_enabled ? Lzma : None;
    cpr_mai_block.length      = (uint32_t)total_sectors * 6;
    // Calculate CRC64
    cpr_mai_block.crc64       = aaruf_crc64_data(ctx->sector_cpr_mai, cpr_mai_block.length);

    buffer = NULL;

    if(cpr_mai_block.compression == None)
    {
        buffer                 = ctx->sector_cpr_mai;
        cpr_mai_block.cmpCrc64 = cpr_mai_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)cpr_mai_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for DVD sector CPR/MAI compression");
            return;
        }

        size_t dst_size   = (size_t)cpr_mai_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_cpr_mai, cpr_mai_block.length, lzma_properties,
                                 &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        cpr_mai_block.cmpLength = (uint32_t)dst_size;

        if(cpr_mai_block.cmpLength >= cpr_mai_block.length)
        {
            cpr_mai_block.compression = None;
            free(buffer);
            buffer = ctx->sector_cpr_mai;
        }
    }

    if(cpr_mai_block.compression == None)
    {
        cpr_mai_block.cmpLength = cpr_mai_block.length;
        cpr_mai_block.cmpCrc64  = cpr_mai_block.crc64;
    }
    else
        cpr_mai_block.cmpCrc64 = aaruf_crc64_data(buffer, cpr_mai_block.cmpLength);

    length_to_write = cpr_mai_block.cmpLength;
    if(cpr_mai_block.compression == Lzma) cpr_mai_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&cpr_mai_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(cpr_mai_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote DVD sector CPR/MAI block (%" PRIu64 " bytes)", cpr_mai_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing DVD sector CPR/MAI block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == DvdSectorCprMai)
                {
                    TRACE("Found existing DVD sector CPR/MAI block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add CPR/MAI block to index
            TRACE("Adding DVD sector CPR/MAI block to index");
            IndexEntry cpr_mai_index_entry;
            cpr_mai_index_entry.blockType = DataBlock;
            cpr_mai_index_entry.dataType  = DvdSectorCprMai;
            cpr_mai_index_entry.offset    = cpr_mai_position;
            utarray_push_back(ctx->index_entries, &cpr_mai_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added DVD sector CPR/MAI block index entry at offset %" PRIu64, cpr_mai_position);
        }
    }

    if(cpr_mai_block.compression == Lzma) free(buffer);

    // Write DVD sector EDC block
    fseek(ctx->imageStream, 0, SEEK_END);
    long edc_position = ftell(ctx->imageStream);
    if(edc_position & alignment_mask)
    {
        const uint64_t aligned_position = edc_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        edc_position = aligned_position;
    }
    TRACE("Writing DVD sector EDC block at position %ld", edc_position);
    BlockHeader edc_block = {0};
    edc_block.identifier  = DataBlock;
    edc_block.type        = DvdSectorEdc;
    edc_block.compression = ctx->compression_enabled ? Lzma : None;
    edc_block.length      = (uint32_t)total_sectors * 4;
    // Calculate CRC64
    edc_block.crc64       = aaruf_crc64_data(ctx->sector_edc, edc_block.length);

    buffer = NULL;

    if(edc_block.compression == None)
    {
        buffer             = ctx->sector_edc;
        edc_block.cmpCrc64 = edc_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)edc_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for DVD sector EDC compression");
            return;
        }

        size_t dst_size   = (size_t)edc_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_edc, edc_block.length, lzma_properties, &props_size, 9,
                                 ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        edc_block.cmpLength = (uint32_t)dst_size;

        if(edc_block.cmpLength >= edc_block.length)
        {
            edc_block.compression = None;
            free(buffer);
            buffer = ctx->sector_edc;
        }
    }

    if(edc_block.compression == None)
    {
        edc_block.cmpLength = edc_block.length;
        edc_block.cmpCrc64  = edc_block.crc64;
    }
    else
        edc_block.cmpCrc64 = aaruf_crc64_data(buffer, edc_block.cmpLength);

    length_to_write = edc_block.cmpLength;
    if(edc_block.compression == Lzma) edc_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&edc_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(edc_block.compression == Lzma) fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote DVD sector EDC block (%" PRIu64 " bytes)", edc_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing DVD sector EDC block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == DvdSectorEdc)
                {
                    TRACE("Found existing DVD sector EDC block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add EDC block to index
            TRACE("Adding DVD sector EDC block to index");
            IndexEntry edc_index_entry;
            edc_index_entry.blockType = DataBlock;
            edc_index_entry.dataType  = DvdSectorEdc;
            edc_index_entry.offset    = edc_position;
            utarray_push_back(ctx->index_entries, &edc_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added DVD sector EDC block index entry at offset %" PRIu64, edc_position);
        }
    }

    if(edc_block.compression == Lzma) free(buffer);
}

/**
 * @brief Serialize the DVD decrypted title key data block to the image file.
 *
 * This function writes a data block containing decrypted DVD title keys for all sectors
 * in the image. DVD title keys are used in the Content Scrambling System (CSS) encryption
 * scheme to decrypt sector data on encrypted DVDs. When imaging encrypted DVD media, if
 * the decryption keys are available, they can be stored alongside the encrypted data to
 * enable future decryption without requiring the original disc or authentication process.
 *
 * The function is only invoked if the decrypted title key buffer has been populated during
 * image creation (ctx->sector_decrypted_title_key != NULL). If the buffer is NULL, the
 * function returns immediately without writing anything, allowing DVD images without
 * decrypted title keys to be created normally. This is typical for non-encrypted DVDs
 * or when keys were not available during imaging.
 *
 * **Data block structure:**
 *
 * - **Block Type**: DataBlock with type DvdSectorTitleKeyDecrypted
 * - **Size**: 5 bytes per sector (total_sectors × 5 bytes)
 *   - total_sectors = negative sectors + user sectors + overflow sectors
 * - **Compression**: Applied if enabled (LZMA compression)
 * - **CRC64**: Computed over the decrypted title key buffer
 * - **Alignment**: Block-aligned according to ctx->userDataDdtHeader.blockAlignmentShift
 *
 * **Block write sequence:**
 *
 * 1. Check if ctx->sector_decrypted_title_key is NULL; return early if so
 * 2. Seek to end of file to determine write position
 * 3. Align file position forward to next block boundary (if needed)
 * 4. Construct BlockHeader with:
 *    - identifier = DataBlock
 *    - type = DvdSectorTitleKeyDecrypted
 *    - compression = ctx->compression_enabled ? Lzma : None
 *    - length = (negative + sectors + overflow) × 5
 *    - cmpLength = compressed size or original size if compression not effective
 *    - crc64 = CRC64 of original key buffer
 *    - cmpCrc64 = CRC64 of compressed data or same as crc64 if uncompressed
 * 5. Write BlockHeader (sizeof(BlockHeader) bytes)
 * 6. Write LZMA properties if compressed (LZMA_PROPERTIES_LENGTH bytes)
 * 7. Write decrypted title key data buffer (compressed or uncompressed)
 * 8. Create and append IndexEntry to ctx->indexEntries:
 *    - blockType = DataBlock
 *    - dataType = DvdSectorTitleKeyDecrypted
 *    - offset = aligned block position
 *
 * **Alignment and file positioning:**
 *
 * Before writing the block, the file position is moved to EOF and then aligned forward
 * to the next boundary satisfying (position & alignment_mask) == 0, where alignment_mask
 * is derived from the blockAlignmentShift. This ensures that all structural blocks begin
 * on properly aligned offsets for efficient I/O and compliance with the Aaru format
 * specification.
 *
 * **Index registration:**
 *
 * After successful write, an IndexEntry is created and added to ctx->indexEntries using
 * utarray_push_back(). This allows the block to be located efficiently during image
 * reading via the index lookup mechanism. The index stores the exact file offset where
 * the BlockHeader was written.
 *
 * **Title Key Format:**
 *
 * Each sector's title key is exactly 5 bytes. The keys are stored sequentially in sector
 * order (negative sectors first, then user sectors, then overflow sectors). The corrected
 * sector addressing scheme is used to index into the buffer:
 *   - Negative sector N: index = (N - negative)
 *   - User sector U: index = (negative + U)
 *   - Overflow sector O: index = (negative + Sectors + O)
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode.
 *
 * @note This is a static helper function called during image finalization (aaruf_close).
 *       It is not part of the public API.
 *
 * @note The function performs no error handling beyond checking for NULL buffer.
 *       Write failures are silently ignored, consistent with other optional metadata
 *       serialization routines in the finalization sequence.
 *
 * @note Decrypted title keys are sensitive cryptographic material. Applications should
 *       consider access control and security implications when storing and distributing
 *       images containing decrypted keys.
 *
 * @note The function uses TRACE() macros for diagnostic logging of write progress,
 *       file positions, and index entry creation.
 *
 * @warning This function assumes ctx->imageStream is open and writable. Calling it
 *          with a read-only or closed stream will result in undefined behavior.
 *
 * @warning The function does not validate that ctx->sector_decrypted_title_key contains
 *          exactly (negative + sectors + overflow) × 5 bytes. Buffer overruns may occur
 *          if the buffer was improperly allocated.
 *
 * @warning Do not call this function directly. It is invoked automatically by aaruf_close()
 *          as part of the image finalization sequence.
 *
 * @internal
 */
static void write_dvd_title_key_decrypted_block(aaruformat_context *ctx)
{
    if(ctx->sector_decrypted_title_key == NULL) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }
    TRACE("Writing DVD decrypted title key block at position %ld", block_position);
    BlockHeader decrypted_title_key_block = {0};
    decrypted_title_key_block.identifier  = DataBlock;
    decrypted_title_key_block.type        = DvdSectorTitleKeyDecrypted;
    decrypted_title_key_block.compression = ctx->compression_enabled ? Lzma : None;
    decrypted_title_key_block.length =
        (uint32_t)(ctx->user_data_ddt_header.negative + ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow) *
        5;
    // Calculate CRC64
    decrypted_title_key_block.crc64 =
        aaruf_crc64_data(ctx->sector_decrypted_title_key, decrypted_title_key_block.length);

    uint8_t *buffer                                  = NULL;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(decrypted_title_key_block.compression == None)
    {
        buffer                             = ctx->sector_decrypted_title_key;
        decrypted_title_key_block.cmpCrc64 = decrypted_title_key_block.crc64;
    }
    else
    {
        buffer = malloc((size_t)decrypted_title_key_block.length * 2);  // Allocate double size for compression
        if(buffer == NULL)
        {
            TRACE("Failed to allocate memory for DVD decrypted title key compression");
            return;
        }

        size_t dst_size   = (size_t)decrypted_title_key_block.length * 2 * 2;
        size_t props_size = LZMA_PROPERTIES_LENGTH;
        aaruf_lzma_encode_buffer(buffer, &dst_size, ctx->sector_decrypted_title_key, decrypted_title_key_block.length,
                                 lzma_properties, &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

        decrypted_title_key_block.cmpLength = (uint32_t)dst_size;

        if(decrypted_title_key_block.cmpLength >= decrypted_title_key_block.length)
        {
            decrypted_title_key_block.compression = None;
            free(buffer);
            buffer = ctx->sector_decrypted_title_key;
        }
    }

    if(decrypted_title_key_block.compression == None)
    {
        decrypted_title_key_block.cmpLength = decrypted_title_key_block.length;
        decrypted_title_key_block.cmpCrc64  = decrypted_title_key_block.crc64;
    }
    else
        decrypted_title_key_block.cmpCrc64 = aaruf_crc64_data(buffer, decrypted_title_key_block.cmpLength);

    const size_t length_to_write = decrypted_title_key_block.cmpLength;
    if(decrypted_title_key_block.compression == Lzma) decrypted_title_key_block.cmpLength += LZMA_PROPERTIES_LENGTH;

    // Write header
    if(fwrite(&decrypted_title_key_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
    {
        if(decrypted_title_key_block.compression == Lzma)
            fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        // Write data
        const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote DVD decrypted title key block (%" PRIu64 " bytes)",
                  decrypted_title_key_block.cmpLength);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing DVD decrypted title key block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == DataBlock && entry->dataType == DvdSectorTitleKeyDecrypted)
                {
                    TRACE("Found existing DVD decrypted title key block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add decrypted title key block to index
            TRACE("Adding DVD decrypted title key block to index");
            IndexEntry decrypted_title_key_index_entry;
            decrypted_title_key_index_entry.blockType = DataBlock;
            decrypted_title_key_index_entry.dataType  = DvdSectorTitleKeyDecrypted;
            decrypted_title_key_index_entry.offset    = block_position;
            utarray_push_back(ctx->index_entries, &decrypted_title_key_index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added DVD decrypted title key block index entry at offset %" PRIu64, block_position);
        }
    }

    if(decrypted_title_key_block.compression == Lzma) free(buffer);
}

/**
 * @brief Serialize all accumulated media tags to the image file.
 *
 * Media tags represent arbitrary metadata or descriptor blobs associated with the entire medium
 * (as opposed to per-sector or per-track metadata). Examples include proprietary drive firmware
 * information, TOC descriptors, ATIP data, PMA/Lead-in content, or manufacturer-specific binary
 * structures that do not fit the standard track or sector model. Each tag is identified by a
 * numeric type field interpreted by upper layers or external tooling.
 *
 * This function traverses the ctx->mediaTags hash table (keyed by tag type) using HASH_ITER and
 * writes each tag as an independent DataBlock. Each block is:
 *   - Aligned to the DDT block boundary (controlled by ctx->userDataDdtHeader.blockAlignmentShift)
 *   - Prefixed with a BlockHeader containing the identifier DataBlock and a data type derived
 *     from the tag's type field via ::aaruf_get_datatype_for_media_tag_type()
 *   - Compressed if enabled (compression = ctx->compression_enabled ? Lzma : None)
 *   - CRC64-protected: the checksum is computed over the raw tag data and stored in crc64;
 *     cmpCrc64 stores the checksum of compressed data or equals crc64 if uncompressed
 *   - Followed immediately by the tag's data payload (compressed or uncompressed)
 *
 * After successfully writing a tag's header and data, an IndexEntry is appended to
 * ctx->indexEntries with:
 *   - blockType = DataBlock
 *   - dataType = the converted tag type (from aaruf_get_datatype_for_media_tag_type)
 *   - offset = the aligned file position where the BlockHeader was written
 *
 * **Alignment and file positioning:**
 * Before writing each tag, the file position is moved to EOF and then aligned forward to the next
 * boundary satisfying (position & alignment_mask) == 0, where alignment_mask is derived from the
 * blockAlignmentShift. This ensures that all structural blocks (including media tags) begin on
 * properly aligned offsets for efficient I/O and compliance with the Aaru format specification.
 *
 * **Order of operations for each tag:**
 * 1. Seek to end of file
 * 2. Align file position to block boundary
 * 3. Construct BlockHeader with identifier, type, compression setting, length, CRC64
 * 4. Compress tag data if enabled and compression is effective
 * 5. Write BlockHeader (sizeof(BlockHeader) bytes)
 * 6. Write LZMA properties if compressed (LZMA_PROPERTIES_LENGTH bytes)
 * 7. Write tag data (compressed or uncompressed)
 * 8. On success, push IndexEntry to ctx->indexEntries
 *
 * **Error handling:**
 * Write errors (fwrite returning < 1) are silently ignored for individual tags; no index entry is
 * added if a write fails, but iteration continues. Diagnostic TRACE logs report success or
 * failure for each tag. The function does not propagate error codes; higher-level close logic
 * must validate overall integrity if needed.
 *
 * **Hash table iteration:**
 * The function uses HASH_ITER(hh, ctx->mediaTags, media_tag, tmp_media_tag) from uthash to
 * safely iterate all entries. The tmp_media_tag parameter provides deletion-safe traversal,
 * though this function does not delete entries (cleanup is handled during context teardown).
 *
 * **No-op conditions:**
 * If ctx->mediaTags is NULL (no tags were added during image creation), the function returns
 * immediately without writing anything or modifying the index.
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->mediaTags contains the hash table of media tags to serialize (may be NULL
 *            if no tags exist). ctx->imageStream must be open and writable. ctx->indexEntries
 *            must be initialized (utarray) to accept new index entries.
 *
 * @note Media tags are format-agnostic at this layer. The tag type-to-datatype mapping is
 *       delegated to ::aaruf_get_datatype_for_media_tag_type(), which consults internal
 *       tables or enumerations defined elsewhere in the library.
 *
 * @see ::aaruf_write_media_tag() for adding tags to the context during image creation.
 * @see ::aaruf_get_datatype_for_media_tag_type() for type conversion logic.
 * @see mediaTagEntry for the hash table entry structure.
 *
 * @internal
 */
static void write_media_tags(aaruformat_context *ctx)
{
    if(ctx->mediaTags == NULL) return;

    mediaTagEntry *media_tag     = NULL;
    mediaTagEntry *tmp_media_tag = NULL;

    HASH_ITER(hh, ctx->mediaTags, media_tag, tmp_media_tag)
    {
        fseek(ctx->imageStream, 0, SEEK_END);
        long           tag_position   = ftell(ctx->imageStream);
        const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
        if(tag_position & alignment_mask)
        {
            const uint64_t aligned_position = tag_position + alignment_mask & ~alignment_mask;
            fseek(ctx->imageStream, aligned_position, SEEK_SET);
            tag_position = aligned_position;
        }

        TRACE("Writing media tag block type %d at position %ld", aaruf_get_datatype_for_media_tag_type(media_tag->type),
              tag_position);
        BlockHeader tag_block = {0};
        tag_block.identifier  = DataBlock;
        tag_block.type        = (uint16_t)aaruf_get_datatype_for_media_tag_type(media_tag->type);
        tag_block.compression = ctx->compression_enabled ? Lzma : None;
        tag_block.length      = media_tag->length;

        // Calculate CRC64
        tag_block.crc64 = aaruf_crc64_data(media_tag->data, tag_block.length);

        uint8_t *buffer                                  = NULL;
        uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

        if(tag_block.compression == None)
        {
            buffer             = media_tag->data;
            tag_block.cmpCrc64 = tag_block.crc64;
        }
        else
        {
            buffer = malloc((size_t)tag_block.length * 2);  // Allocate double size for compression
            if(buffer == NULL)
            {
                TRACE("Failed to allocate memory for media tag compression");
                return;
            }

            size_t dst_size   = (size_t)tag_block.length * 2 * 2;
            size_t props_size = LZMA_PROPERTIES_LENGTH;
            aaruf_lzma_encode_buffer(buffer, &dst_size, media_tag->data, tag_block.length, lzma_properties, &props_size,
                                     9, ctx->lzma_dict_size, 4, 0, 2, 273, 8);

            tag_block.cmpLength = (uint32_t)dst_size;

            if(tag_block.cmpLength >= tag_block.length)
            {
                tag_block.compression = None;
                free(buffer);
                buffer = media_tag->data;
            }
        }

        if(tag_block.compression == None)
        {
            tag_block.cmpLength = tag_block.length;
            tag_block.cmpCrc64  = tag_block.crc64;
        }
        else
            tag_block.cmpCrc64 = aaruf_crc64_data(buffer, tag_block.cmpLength);

        const size_t length_to_write = tag_block.cmpLength;
        if(tag_block.compression == Lzma) tag_block.cmpLength += LZMA_PROPERTIES_LENGTH;

        // Write header
        if(fwrite(&tag_block, sizeof(BlockHeader), 1, ctx->imageStream) == 1)
        {
            if(tag_block.compression == Lzma)
                fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);  // Write data

            const size_t written_bytes = fwrite(buffer, length_to_write, 1, ctx->imageStream);
            if(written_bytes == 1)
            {
                TRACE("Successfully wrote media tag block type %d (%" PRIu64 " bytes)", tag_block.type,
                      tag_block.cmpLength);

                // Remove any previously existing index entries of the same type before adding
                TRACE("Removing any previously existing media tag type %d block index entries", tag_block.type);
                for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
                {
                    const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                    if(entry && entry->blockType == DataBlock && entry->dataType == tag_block.type)
                    {
                        TRACE("Found existing media tag type %d block index entry at position %d, removing",
                              tag_block.type, k);
                        utarray_erase(ctx->index_entries, k, 1);
                    }
                }

                // Add media tag block to index
                TRACE("Adding media tag type %d block to index", tag_block.type);
                IndexEntry tag_index_entry;
                tag_index_entry.blockType = DataBlock;
                tag_index_entry.dataType  = tag_block.type;
                tag_index_entry.offset    = tag_position;
                utarray_push_back(ctx->index_entries, &tag_index_entry);
                ctx->dirty_index_block = true;
                TRACE("Added media tag block type %d index entry at offset %" PRIu64, tag_block.type, tag_position);
            }
        }

        if(tag_block.compression == Lzma) free(buffer);
    }
}

/**
 * @brief Serialize the tape file metadata block to the image file.
 *
 * This function writes a TapeFileBlock containing the complete tape file structure metadata
 * to the Aaru image file. The tape file block documents all logical files present on the tape
 * medium, recording each file's partition number, file number, and block range (first and last
 * block addresses). This metadata enables random access to specific files within the tape image
 * and preserves the original tape's logical organization for archival purposes.
 *
 * The tape file block is optional; if no tape file metadata has been populated (ctx->tapeFiles
 * hash table is NULL or empty), the function returns immediately without writing anything. This
 * no-op behavior allows the close operation to proceed gracefully whether or not tape file
 * structure metadata was included during image creation.
 *
 * **Block Structure:**
 * The serialized block consists of:
 * ```
 * +-------------------------+
 * | TapeFileHeader (24 B)   | <- identifier, entries, length, crc64
 * +-------------------------+
 * | TapeFileEntry 0 (21 B)  | <- File, Partition, FirstBlock, LastBlock
 * | TapeFileEntry 1 (21 B)  |
 * | ...                     |
 * | TapeFileEntry (n-1)     |
 * +-------------------------+
 * ```
 *
 * **Processing Flow:**
 * 1. **Entry Enumeration:** Iterate through ctx->tapeFiles hash table to count entries
 * 2. **Buffer Allocation:** Allocate temporary buffer for all TapeFileEntry structures
 * 3. **Data Copying:** Copy each file entry from hash table to buffer sequentially
 * 4. **Header Construction:** Build TapeFileHeader with entry count and CRC64 checksum
 * 5. **Alignment:** Seek to EOF and align to block boundary (blockAlignmentShift)
 * 6. **Write Operations:** Write header followed by entry array to image stream
 * 7. **Indexing:** Add IndexEntry pointing to this block for fast location during reads
 * 8. **Cleanup:** Free temporary buffer
 *
 * **Hash Table Iteration:**
 * The function uses UTHASH's HASH_ITER macro to safely traverse ctx->tapeFiles:
 * - First pass: Count total entries in the hash table
 * - Second pass: Copy each TapeFileEntry to the output buffer
 * - The iteration order depends on hash table internals, not insertion order
 * - For deterministic output, entries could be sorted before writing (not currently done)
 *
 * **CRC64 Integrity Protection:**
 * A CRC64-ECMA checksum is computed over the complete array of TapeFileEntry structures
 * using aaruf_crc64_data(). This checksum is stored in the TapeFileHeader and verified
 * during image opening by process_tape_files_block() to detect corruption in the file
 * table. The checksum covers only the entry data, not the header itself.
 *
 * **Alignment Strategy:**
 * Before writing, the file position is:
 * 1. Moved to EOF using fseek(SEEK_END)
 * 2. Aligned forward to next boundary: (position + alignment_mask) & ~alignment_mask
 * 3. Where alignment_mask = (1 << blockAlignmentShift) - 1
 * This ensures the tape file block starts on a properly aligned offset for efficient
 * I/O and compliance with the Aaru format specification.
 *
 * **Write Sequence:**
 * The function performs a two-stage write operation:
 * 1. Write TapeFileHeader (sizeof(TapeFileHeader) = 24 bytes)
 * 2. Write TapeFileEntry array (tape_file_block.length bytes)
 *
 * Both writes must succeed for the index entry to be added. If either write fails,
 * the block is incomplete but the function continues (no error propagation).
 *
 * **Indexing:**
 * On successful write, an IndexEntry is created and pushed to ctx->indexEntries:
 * - blockType = TapeFileBlock (identifies this as tape file metadata)
 * - dataType = 0 (tape file blocks have no subtype)
 * - offset = file position where TapeFileHeader was written
 *
 * This index entry enables process_tape_files_block() to quickly locate the tape
 * file metadata during subsequent image opens without scanning the entire file.
 *
 * **Entry Order:**
 * The current implementation writes entries in hash table iteration order, which is
 * non-deterministic and depends on the hash function and insertion sequence. For
 * better compatibility and reproducibility, entries should ideally be sorted by:
 * 1. Partition number (ascending)
 * 2. File number within partition (ascending)
 * However, the current implementation does not enforce this ordering.
 *
 * **Error Handling:**
 * The function handles errors gracefully without propagating them:
 * - NULL hash table: Return immediately (no tape files to write)
 * - Memory allocation failure: Log via TRACE and return (block not written)
 * - Write failures: Silent (index entry not added, block incomplete)
 *
 * This opportunistic approach ensures that tape file metadata write failures do not
 * prevent the image from being created, though the resulting image will lack file
 * structure metadata.
 *
 * **Memory Management:**
 * - Allocates temporary buffer sized to hold all TapeFileEntry structures
 * - Buffer is zero-initialized with memset for consistent padding bytes
 * - Buffer is always freed before the function returns, even on write failure
 * - Source data in ctx->tapeFiles is not modified and is freed later during cleanup
 *
 * **Thread Safety:**
 * This function is NOT thread-safe. It modifies shared ctx state (imageStream file
 * position, indexEntries array) and must only be called during single-threaded
 * finalization (within aaruf_close).
 *
 * **Use Cases:**
 * - Preserving tape file structure for archival and forensic purposes
 * - Enabling random access to specific files within tape images
 * - Documenting multi-file tape organization for analysis tools
 * - Supporting tape formats with complex file/partition layouts
 * - Facilitating tape image validation and structure verification
 *
 * **Relationship to Other Functions:**
 * - File entries are added via aaruf_set_tape_file() during image creation
 * - Entries are stored in ctx->tapeFiles hash table until image close
 * - This function serializes the hash table to disk during aaruf_close()
 * - process_tape_files_block() reads and reconstructs the hash table during aaruf_open()
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            The tapeFiles hash table should be populated if tape file metadata exists.
 *            The imageStream must be open and writable.
 *            The indexEntries array must be initialized for adding the index entry.
 *
 * @note The tape file block is written near the end of the image file, after sector data
 *       and before the final index block. The exact position depends on what other metadata
 *       blocks are present in the image.
 *
 * @note If ctx->tapeFiles is NULL or empty, the function returns immediately without
 *       writing anything. This is not an error condition - it simply means the image
 *       contains no tape file structure metadata.
 *
 * @note Memory allocation failure during buffer creation results in no tape file block
 *       being written, but does not prevent the image from being successfully created.
 *       The image will simply lack tape file metadata.
 *
 * @note The TapeFileHeader.entries field is intentionally set to tape_file_count, not
 *       stored separately. The count is derived dynamically from the hash table size.
 *
 * @note Entry ordering is not guaranteed to match insertion order or logical order.
 *       Reading applications should sort entries by partition/file number if ordered
 *       access is required.
 *
 * @warning Write failures (fwrite returns != 1) are silently ignored. The function does
 *          not return an error code or set errno. Partial writes may leave the block
 *          incomplete, which will be detected during subsequent reads via CRC mismatch.
 *
 * @warning The temporary buffer allocation may fail on systems with limited memory or when
 *          the tape has an extremely large number of files. Allocation failures result in
 *          silent no-op; the image is created without tape file metadata.
 *
 * @warning Bounds checking during iteration protects against buffer overruns. If index
 *          exceeds tape_file_count (which should never occur), the loop breaks early as
 *          a sanity check.
 *
 * @see TapeFileHeader for the block header structure definition
 * @see TapeFileEntry for individual file entry structure definition
 * @see tapeFileHashEntry for the hash table entry structure
 * @see aaruf_set_tape_file() for adding tape files during image creation
 * @see process_tape_files_block() for the loading process during image opening
 * @see aaruf_get_tape_file() for retrieving tape file information from opened images
 *
 * @internal
 */
static void write_tape_file_block(aaruformat_context *ctx)
{
    if(ctx->tape_files == NULL) return;

    // Iterate the uthash and count how many entries do we have
    const tapeFileHashEntry *tape_file       = NULL;
    const tapeFileHashEntry *tmp_tape_file   = NULL;
    size_t                   tape_file_count = 0;
    HASH_ITER(hh, ctx->tape_files, tape_file, tmp_tape_file) tape_file_count++;

    // Create a memory buffer to copy all the file entries
    const size_t   buffer_size = tape_file_count * sizeof(TapeFileEntry);
    TapeFileEntry *buffer      = malloc(buffer_size);
    if(buffer == NULL)
    {
        TRACE("Failed to allocate memory for tape file entries");
        return;
    }
    memset(buffer, 0, buffer_size);
    size_t index = 0;
    HASH_ITER(hh, ctx->tape_files, tape_file, tmp_tape_file)
    {
        if(index >= tape_file_count) break;
        memcpy(&buffer[index], &tape_file->fileEntry, sizeof(TapeFileEntry));
        index++;
    }

    // Create the tape file block in memory
    TapeFileHeader tape_file_block = {0};
    tape_file_block.identifier     = TapeFileBlock;
    tape_file_block.length         = (uint32_t)buffer_size;
    tape_file_block.crc64          = aaruf_crc64_data((uint8_t *)buffer, (uint32_t)tape_file_block.length);

    // Write tape file block to file, block aligned
    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }
    TRACE("Writing tape file block at position %ld", block_position);
    if(fwrite(&tape_file_block, sizeof(TapeFileHeader), 1, ctx->imageStream) == 1)
    {
        const size_t written_bytes = fwrite(buffer, tape_file_block.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote tape file block (%" PRIu64 " bytes)", tape_file_block.length);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing tape file block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == TapeFileBlock && entry->dataType == 0)
                {
                    TRACE("Found existing tape file block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add tape file block to index
            TRACE("Adding tape file block to index");
            IndexEntry index_entry;
            index_entry.blockType = TapeFileBlock;
            index_entry.dataType  = 0;
            index_entry.offset    = block_position;
            utarray_push_back(ctx->index_entries, &index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added tape file block index entry at offset %" PRIu64, block_position);
        }
    }

    free(buffer);
}

/**
 * @brief Serialize the tape partition metadata block to the image file.
 *
 * This function writes a TapePartitionBlock containing the complete tape partition structure
 * metadata to the Aaru image file. The tape partition block documents all physical partitions
 * present on the tape medium, recording each partition's partition number and block range (first
 * and last block addresses). This metadata enables proper interpretation of tape file locations,
 * validation of partition boundaries, and preservation of the original tape's physical organization
 * for archival purposes.
 *
 * The tape partition block is optional; if no tape partition metadata has been populated
 * (ctx->tapePartitions hash table is NULL or empty), the function returns immediately without
 * writing anything. This no-op behavior allows the close operation to proceed gracefully whether
 * or not tape partition structure metadata was included during image creation.
 *
 * **Block Structure:**
 * The serialized block consists of:
 * ```
 * +-----------------------------+
 * | TapePartitionHeader (24 B)  | <- identifier, entries, length, crc64
 * +-----------------------------+
 * | TapePartitionEntry 0 (17 B) | <- Number, FirstBlock, LastBlock
 * | TapePartitionEntry 1 (17 B) |
 * | ...                         |
 * | TapePartitionEntry (n-1)    |
 * +-----------------------------+
 * ```
 *
 * **Processing Flow:**
 * 1. **Entry Enumeration:** Iterate through ctx->tapePartitions hash table to count entries
 * 2. **Buffer Allocation:** Allocate temporary buffer for all TapePartitionEntry structures
 * 3. **Data Copying:** Copy each partition entry from hash table to buffer sequentially
 * 4. **Header Construction:** Build TapePartitionHeader with entry count and CRC64 checksum
 * 5. **Alignment:** Seek to EOF and align to block boundary (blockAlignmentShift)
 * 6. **Write Operations:** Write header followed by entry array to image stream
 * 7. **Indexing:** Add IndexEntry pointing to this block for fast location during reads
 * 8. **Cleanup:** Free temporary buffer
 *
 * **Hash Table Iteration:**
 * The function uses UTHASH's HASH_ITER macro to safely traverse ctx->tapePartitions:
 * - First pass: Count total entries in the hash table
 * - Second pass: Copy each TapePartitionEntry to the output buffer
 * - The iteration order depends on hash table internals, not insertion order
 * - For deterministic output, entries could be sorted by partition number (not currently done)
 *
 * **CRC64 Integrity Protection:**
 * A CRC64-ECMA checksum is computed over the complete array of TapePartitionEntry structures
 * using aaruf_crc64_data(). This checksum is stored in the TapePartitionHeader and verified
 * during image opening by process_tape_partitions_block() to detect corruption in the partition
 * table. The checksum covers only the entry data, not the header itself.
 *
 * **Alignment Strategy:**
 * Before writing, the file position is:
 * 1. Moved to EOF using fseek(SEEK_END)
 * 2. Aligned forward to next boundary: (position + alignment_mask) & ~alignment_mask
 * 3. Where alignment_mask = (1 << blockAlignmentShift) - 1
 * This ensures the tape partition block starts on a properly aligned offset for efficient
 * I/O and compliance with the Aaru format specification.
 *
 * **Write Sequence:**
 * The function performs a two-stage write operation:
 * 1. Write TapePartitionHeader (sizeof(TapePartitionHeader) = 24 bytes)
 * 2. Write TapePartitionEntry array (tape_partition_block.length bytes)
 *
 * Both writes must succeed for the index entry to be added. If either write fails,
 * the block is incomplete but the function continues (no error propagation).
 *
 * **Indexing:**
 * On successful write, an IndexEntry is created and pushed to ctx->indexEntries:
 * - blockType = TapePartitionBlock (identifies this as tape partition metadata)
 * - dataType = 0 (tape partition blocks have no subtype)
 * - offset = file position where TapePartitionHeader was written
 *
 * This index entry enables process_tape_partitions_block() to quickly locate the tape
 * partition metadata during subsequent image opens without scanning the entire file.
 *
 * **Entry Order:**
 * The current implementation writes entries in hash table iteration order, which is
 * non-deterministic and depends on the hash function and insertion sequence. For
 * better compatibility and reproducibility, entries should ideally be sorted by
 * partition number (ascending). However, the current implementation does not enforce
 * this ordering.
 *
 * **Partition Block Ranges:**
 * Each partition entry defines an independent block address space:
 * - FirstBlock: Starting block address (often 0, but format-dependent)
 * - LastBlock: Ending block address (inclusive)
 * - Block count: (LastBlock - FirstBlock + 1)
 *
 * Different partitions may have overlapping logical block numbers (e.g., partition 0
 * and partition 1 can both have blocks 0-1000). The partition metadata is essential
 * for correctly interpreting tape file locations, as files reference partition numbers
 * in their definitions.
 *
 * **Error Handling:**
 * The function handles errors gracefully without propagating them:
 * - NULL hash table: Return immediately (no tape partitions to write)
 * - Memory allocation failure: Log via TRACE and return (block not written)
 * - Write failures: Silent (index entry not added, block incomplete)
 *
 * This opportunistic approach ensures that tape partition metadata write failures do not
 * prevent the image from being created, though the resulting image will lack partition
 * structure metadata.
 *
 * **Memory Management:**
 * - Allocates temporary buffer sized to hold all TapePartitionEntry structures
 * - Buffer is zero-initialized with memset for consistent padding bytes
 * - Buffer is always freed before the function returns, even on write failure
 * - Source data in ctx->tapePartitions is not modified and is freed later during cleanup
 *
 * **Thread Safety:**
 * This function is NOT thread-safe. It modifies shared ctx state (imageStream file
 * position, indexEntries array) and must only be called during single-threaded
 * finalization (within aaruf_close).
 *
 * **Use Cases:**
 * - Preserving tape partition structure for archival and forensic purposes
 * - Documenting multi-partition tape layouts (LTO, DLT, AIT formats)
 * - Enabling validation of file block ranges against partition boundaries
 * - Supporting tape formats with complex partition organizations
 * - Facilitating tape image validation and structure verification
 *
 * **Relationship to Other Functions:**
 * - Partition entries are added via aaruf_set_tape_partition() during image creation
 * - Entries are stored in ctx->tapePartitions hash table until image close
 * - This function serializes the hash table to disk during aaruf_close()
 * - process_tape_partitions_block() reads and reconstructs the hash table during aaruf_open()
 * - Tape files (written by write_tape_file_block) reference these partitions
 *
 * **Format Considerations:**
 * - Single-partition tapes: May omit TapePartitionBlock entirely (partition 0 implied)
 * - Multi-partition tapes: Should include complete partition layout for proper file access
 * - Block addresses are local to each partition in most tape formats
 * - Partition metadata is primarily informational and used for validation
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            The tapePartitions hash table should be populated if tape partition metadata exists.
 *            The imageStream must be open and writable.
 *            The indexEntries array must be initialized for adding the index entry.
 *
 * @note The tape partition block is written near the end of the image file, after sector data
 *       and before the final index block. The exact position depends on what other metadata
 *       blocks are present in the image.
 *
 * @note If ctx->tapePartitions is NULL or empty, the function returns immediately without
 *       writing anything. This is not an error condition - it simply means the image
 *       contains no tape partition structure metadata (typical for single-partition tapes).
 *
 * @note Memory allocation failure during buffer creation results in no tape partition block
 *       being written, but does not prevent the image from being successfully created.
 *       The image will simply lack tape partition metadata.
 *
 * @note The partition metadata should be consistent with file metadata. Files should only
 *       reference partitions that have been defined, and their block ranges should fall
 *       within the partition boundaries, though no automatic validation is performed.
 *
 * @warning Write failures are not propagated as errors. If the write operation fails,
 *          the index entry is not added, but aaruf_close() continues with other finalization
 *          tasks. The resulting image may be incomplete or missing partition metadata.
 *
 * @see write_tape_file_block() for writing tape file metadata (which references partitions)
 * @see process_tape_partitions_block() for reading partition metadata during image open
 * @see aaruf_set_tape_partition() for adding partition entries during image creation
 * @see TapePartitionHeader for the block header structure
 * @see TapePartitionEntry for individual partition entry structure
 *
 * @internal
 */
static void write_tape_partition_block(aaruformat_context *ctx)
{
    if(ctx->tape_partitions == NULL) return;

    // Iterate the uthash and count how many entries do we have
    const TapePartitionHashEntry *tape_partition       = NULL;
    const TapePartitionHashEntry *tmp_tape_partition   = NULL;
    size_t                        tape_partition_count = 0;
    HASH_ITER(hh, ctx->tape_partitions, tape_partition, tmp_tape_partition) tape_partition_count++;

    // Create a memory buffer to copy all the partition entries
    const size_t        buffer_size = tape_partition_count * sizeof(TapePartitionEntry);
    TapePartitionEntry *buffer      = malloc(buffer_size);
    if(buffer == NULL)
    {
        TRACE("Failed to allocate memory for tape partition entries");
        return;
    }
    memset(buffer, 0, buffer_size);
    size_t index = 0;
    HASH_ITER(hh, ctx->tape_partitions, tape_partition, tmp_tape_partition)
    {
        if(index >= tape_partition_count) break;
        memcpy(&buffer[index], &tape_partition->partitionEntry, sizeof(TapePartitionEntry));
        index++;
    }

    // Create the tape partition block in memory
    TapePartitionHeader tape_partition_block = {0};
    tape_partition_block.identifier          = TapePartitionBlock;
    tape_partition_block.length              = (uint32_t)buffer_size;
    tape_partition_block.crc64 = aaruf_crc64_data((uint8_t *)buffer, (uint32_t)tape_partition_block.length);

    // Write tape partition block to partition, block aligned
    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }
    TRACE("Writing tape partition block at position %ld", block_position);
    if(fwrite(&tape_partition_block, sizeof(TapePartitionHeader), 1, ctx->imageStream) == 1)
    {
        const size_t written_bytes = fwrite(buffer, tape_partition_block.length, 1, ctx->imageStream);
        if(written_bytes == 1)
        {
            TRACE("Successfully wrote tape partition block (%" PRIu64 " bytes)", tape_partition_block.length);

            // Remove any previously existing index entries of the same type before adding
            TRACE("Removing any previously existing tape partition block index entries");
            for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
            {
                const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
                if(entry && entry->blockType == TapePartitionBlock && entry->dataType == 0)
                {
                    TRACE("Found existing tape partition block index entry at position %d, removing", k);
                    utarray_erase(ctx->index_entries, k, 1);
                }
            }

            // Add tape partition block to index
            TRACE("Adding tape partition block to index");
            IndexEntry index_entry;
            index_entry.blockType = TapePartitionBlock;
            index_entry.dataType  = 0;
            index_entry.offset    = block_position;
            utarray_push_back(ctx->index_entries, &index_entry);
            ctx->dirty_index_block = true;
            TRACE("Added tape partition block index entry at offset %" PRIu64, block_position);
        }
    }

    free(buffer);
}

/**
 * @brief Serialize the geometry metadata block to the image file.
 *
 * This function writes a GeometryBlockHeader containing legacy CHS (Cylinder-Head-Sector) style
 * logical geometry metadata to the Aaru image file. The geometry block records the physical/logical
 * layout of media that can be addressed using classical CHS parameters (cylinders, heads, sectors
 * per track) common to legacy hard disk drives and some optical media formats.
 *
 * The geometry information is optional; if no geometry metadata was previously set (detected by
 * checking ctx->geometryBlock.identifier != GeometryBlock), the function returns immediately as a
 * no-op. When present, the block is written at the end of the image file, aligned to the DDT block
 * boundary specified by blockAlignmentShift, and an IndexEntry is appended to ctx->indexEntries so
 * readers can locate it during image parsing.
 *
 * Block layout:
 *   - GeometryBlockHeader (16 bytes):
 *       - identifier (4 bytes): BlockType::GeometryBlock magic constant
 *       - cylinders (4 bytes): Number of cylinders
 *       - heads (4 bytes): Number of heads (tracks per cylinder)
 *       - sectorsPerTrack (4 bytes): Number of sectors per track
 *   - No additional payload follows the header in current format versions.
 *
 * Total logical sectors implied by the geometry: cylinders × heads × sectorsPerTrack.
 * Sector size is not encoded in this block and must be derived from other metadata (e.g., from
 * the media type or explicitly stored elsewhere in the image).
 *
 * Alignment strategy:
 *   - The write position is obtained via fseek(SEEK_END) + ftell().
 *   - If the position is not aligned to (1 << blockAlignmentShift), it is advanced to the next
 *     aligned boundary by computing: (position + alignment_mask) & ~alignment_mask.
 *   - This ensures the geometry block starts on a block-aligned offset for efficient access.
 *
 * Error handling:
 *   - If fwrite() fails to write the GeometryBlockHeader, the function silently returns without
 *     updating the index. This is consistent with other optional metadata writers in this module
 *     that use opportunistic writes (failures logged via TRACE but not propagated as errors).
 *   - The caller (aaruf_close) will continue finalizing other blocks even if geometry write fails.
 *
 * Indexing:
 *   - On successful write, an IndexEntry with blockType = GeometryBlock, dataType = 0, and
 *     offset = block_position is pushed to ctx->indexEntries.
 *   - The index will be serialized later by write_index_block() and allows readers to quickly
 *     locate the geometry metadata without scanning the entire file.
 *
 * Use cases:
 *   - Preserving original CHS geometry for disk images from legacy systems (e.g., IDE/PATA drives,
 *     floppy disks, early SCSI devices) where BIOS or firmware relied on CHS addressing.
 *   - Documenting physical layout of optical media that may have track/sector organization.
 *   - Supporting forensic/archival workflows that need complete metadata fidelity.
 *
 * Thread safety: This function is not thread-safe; it modifies shared ctx state (imageStream file
 * position, indexEntries array) and must only be called during single-threaded finalization
 * (within aaruf_close).
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            The geometryBlock field must be pre-populated if geometry metadata is desired.
 *            The imageStream must be open and writable.
 * @internal
 * @see GeometryBlockHeader
 * @see aaruf_set_geometry() for setting geometry values before closing.
 */
static void write_geometry_block(aaruformat_context *ctx)
{
    if(ctx->geometry_block.identifier != GeometryBlock) return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }

    TRACE("Writing geometry block at position %ld", block_position);

    // Write header
    if(fwrite(&ctx->geometry_block, sizeof(GeometryBlockHeader), 1, ctx->imageStream) == 1)
    {
        TRACE("Successfully wrote geometry block");

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing geometry block index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == GeometryBlock && entry->dataType == 0)
            {
                TRACE("Found existing geometry block index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add geometry block to index
        TRACE("Adding geometry block to index");
        IndexEntry index_entry;
        index_entry.blockType = GeometryBlock;
        index_entry.dataType  = 0;
        index_entry.offset    = block_position;
        utarray_push_back(ctx->index_entries, &index_entry);
        ctx->dirty_index_block = true;
        TRACE("Added geometry block index entry at offset %" PRIu64, block_position);
    }
}

/**
 * @brief Serialize the metadata block containing image and media descriptive information.
 *
 * This function writes a MetadataBlock containing human-readable and machine-readable metadata
 * strings that describe the image creation context, the physical media being preserved, and the
 * drive used for acquisition. The metadata block stores variable-length UTF-16LE strings for fields
 * such as creator identification, user comments, media identification (title, manufacturer, model,
 * serial number, barcode, part number), and drive identification (manufacturer, model, serial
 * number, firmware revision). Each string is stored sequentially in a single contiguous buffer,
 * with the MetadataBlockHeader recording both the offset (relative to the start of the buffer)
 * and length of each field.
 *
 * The metadata block is optional; if no metadata fields have been populated (all string pointers
 * are NULL and sequence numbers are zero), the function returns immediately without writing
 * anything. This no-op behavior is detected by checking that the identifier has not been
 * explicitly set to MetadataBlock and all relevant imageInfo string fields are NULL.
 *
 * **Block structure:**
 * The serialized block consists of:
 *   1. MetadataBlockHeader (fixed size, containing identifier, sequence numbers, field offsets
 *      and lengths for all metadata strings)
 *   2. Variable-length payload: concatenated UTF-16LE string data for all non-NULL fields
 *
 * The total blockSize is computed as sizeof(MetadataBlockHeader) plus the sum of all populated
 * string lengths (creatorLength, commentsLength, mediaTitleLength, etc.). Note that lengths are
 * in bytes, not character counts.
 *
 * **Field packing order:**
 * Non-NULL strings from ctx->imageInfo are copied into the buffer in the following order:
 *   1. Creator
 *   2. Comments
 *   3. MediaTitle
 *   4. MediaManufacturer
 *   5. MediaModel
 *   6. MediaSerialNumber
 *   7. MediaBarcode
 *   8. MediaPartNumber
 *   9. DriveManufacturer
 *   10. DriveModel
 *   11. DriveSerialNumber
 *   12. DriveFirmwareRevision
 *
 * As each field is copied, its offset (relative to the buffer start, which begins after the
 * header) is recorded in the corresponding offset field of ctx->metadataBlockHeader, and the
 * position pointer is advanced by the field's length.
 *
 * **Alignment and file positioning:**
 * Before writing the block, the file position is moved to EOF and then aligned forward to the
 * next boundary satisfying (position & alignment_mask) == 0, where alignment_mask is derived
 * from ctx->userDataDdtHeader.blockAlignmentShift. This ensures the metadata block begins on
 * a properly aligned offset for efficient I/O and compliance with the Aaru format specification.
 *
 * **Index registration:**
 * After successfully writing the metadata block, an IndexEntry is appended to ctx->indexEntries
 * with:
 *   - blockType = MetadataBlock
 *   - dataType = 0 (metadata blocks have no subtype)
 *   - offset = the aligned file position where the block was written
 *
 * **Memory management:**
 * The function allocates a temporary buffer (via calloc) sized to hold the entire block payload.
 * If allocation fails, the function returns immediately without writing anything. The buffer is
 * freed before the function returns, regardless of write success or failure.
 *
 * **Error handling:**
 * Write errors (fwrite returning < 1) are silently ignored; no index entry is added if the write
 * fails, but the temporary buffer is still freed. Diagnostic TRACE logs report success or failure.
 * The function does not propagate error codes; higher-level close logic must validate overall
 * integrity if needed.
 *
 * **No-op conditions:**
 * - ctx->metadataBlockHeader.identifier is not MetadataBlock AND
 * - ctx->metadataBlockHeader.mediaSequence == 0 AND
 * - ctx->metadataBlockHeader.lastMediaSequence == 0 AND
 * - All ctx->imageInfo string fields (Creator, Comments, MediaTitle, etc.) are NULL
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->metadataBlockHeader contains the header template with pre-populated field
 *            lengths and sequence numbers (if applicable). ctx->imageInfo contains pointers
 *            to the actual UTF-16LE string data (may be NULL for unpopulated fields).
 *            ctx->imageStream must be open and writable. ctx->indexEntries must be initialized
 *            (utarray) to accept new index entries.
 *
 * @note UTF-16LE Encoding:
 *       - All metadata strings use UTF-16LE encoding to support international characters
 *       - Field lengths are in bytes, not character counts (UTF-16LE uses 2 or 4 bytes per character)
 *       - The library treats string data as opaque and does not validate UTF-16LE encoding
 *       - Ensure even byte lengths to maintain UTF-16LE character alignment
 *
 * @note Unlike data blocks (which include CRC64 checksums), the metadata block does not currently
 *       include integrity checking beyond the implicit file-level checksums. The header itself
 *       stores offsets/lengths but not CRCs for individual string fields.
 *
 * @note Media sequence numbers (mediaSequence, lastMediaSequence) support multi-volume image
 *       sets (e.g., spanning multiple optical discs). Single-volume images typically set both
 *       to 0 or leave them uninitialized.
 *
 * @see MetadataBlockHeader for the on-disk structure definition.
 * @see ::aaruf_set_creator() for populating the creator field.
 * @see ::aaruf_set_comments() for populating the comments field.
 * @see ::aaruf_set_media_title() for populating the media title field.
 *
 * @internal
 */
static void write_metadata_block(aaruformat_context *ctx)
{
    if(ctx->metadata_block_header.identifier != MetadataBlock && ctx->metadata_block_header.mediaSequence == 0 &&
       ctx->metadata_block_header.lastMediaSequence == 0 && ctx->creator == NULL && ctx->comments == NULL &&
       ctx->media_title == NULL && ctx->media_manufacturer == NULL && ctx->media_model == NULL &&
       ctx->media_serial_number == NULL && ctx->media_barcode == NULL && ctx->media_part_number == NULL &&
       ctx->drive_manufacturer == NULL && ctx->drive_model == NULL && ctx->drive_serial_number == NULL &&
       ctx->drive_firmware_revision == NULL)
        return;

    ctx->metadata_block_header.blockSize =
        sizeof(MetadataBlockHeader) + ctx->metadata_block_header.creatorLength +
        ctx->metadata_block_header.commentsLength + ctx->metadata_block_header.mediaTitleLength +
        ctx->metadata_block_header.mediaManufacturerLength + ctx->metadata_block_header.mediaModelLength +
        ctx->metadata_block_header.mediaSerialNumberLength + ctx->metadata_block_header.mediaBarcodeLength +
        ctx->metadata_block_header.mediaPartNumberLength + ctx->metadata_block_header.driveManufacturerLength +
        ctx->metadata_block_header.driveModelLength + ctx->metadata_block_header.driveSerialNumberLength +
        ctx->metadata_block_header.driveFirmwareRevisionLength;

    ctx->metadata_block_header.identifier = MetadataBlock;

    int pos = sizeof(MetadataBlockHeader);

    uint8_t *buffer = calloc(1, ctx->metadata_block_header.blockSize);
    if(buffer == NULL) return;

    if(ctx->creator != NULL && ctx->metadata_block_header.creatorLength > 0)
    {
        memcpy(buffer + pos, ctx->creator, ctx->metadata_block_header.creatorLength);
        ctx->metadata_block_header.creatorOffset = pos;
        pos += ctx->metadata_block_header.creatorLength;
    }

    if(ctx->comments != NULL && ctx->metadata_block_header.commentsLength > 0)
    {
        memcpy(buffer + pos, ctx->comments, ctx->metadata_block_header.commentsLength);
        ctx->metadata_block_header.commentsOffset = pos;
        pos += ctx->metadata_block_header.commentsLength;
    }

    if(ctx->media_title != NULL && ctx->metadata_block_header.mediaTitleLength > 0)
    {
        memcpy(buffer + pos, ctx->media_title, ctx->metadata_block_header.mediaTitleLength);
        ctx->metadata_block_header.mediaTitleOffset = pos;
        pos += ctx->metadata_block_header.mediaTitleLength;
    }

    if(ctx->media_manufacturer != NULL && ctx->metadata_block_header.mediaManufacturerLength > 0)
    {
        memcpy(buffer + pos, ctx->media_manufacturer, ctx->metadata_block_header.mediaManufacturerLength);
        ctx->metadata_block_header.mediaManufacturerOffset = pos;
        pos += ctx->metadata_block_header.mediaManufacturerLength;
    }

    if(ctx->media_model != NULL && ctx->metadata_block_header.mediaModelLength > 0)
    {
        memcpy(buffer + pos, ctx->media_model, ctx->metadata_block_header.mediaModelLength);
        ctx->metadata_block_header.mediaModelOffset = pos;
        pos += ctx->metadata_block_header.mediaModelLength;
    }

    if(ctx->media_serial_number != NULL && ctx->metadata_block_header.mediaSerialNumberLength > 0)
    {
        memcpy(buffer + pos, ctx->media_serial_number, ctx->metadata_block_header.mediaSerialNumberLength);
        ctx->metadata_block_header.mediaSerialNumberOffset = pos;
        pos += ctx->metadata_block_header.mediaSerialNumberLength;
    }

    if(ctx->media_barcode != NULL && ctx->metadata_block_header.mediaBarcodeLength > 0)
    {
        memcpy(buffer + pos, ctx->media_barcode, ctx->metadata_block_header.mediaBarcodeLength);
        ctx->metadata_block_header.mediaBarcodeOffset = pos;
        pos += ctx->metadata_block_header.mediaBarcodeLength;
    }

    if(ctx->media_part_number != NULL && ctx->metadata_block_header.mediaPartNumberLength > 0)
    {
        memcpy(buffer + pos, ctx->media_part_number, ctx->metadata_block_header.mediaPartNumberLength);
        ctx->metadata_block_header.mediaPartNumberOffset = pos;
        pos += ctx->metadata_block_header.mediaPartNumberLength;
    }

    if(ctx->drive_manufacturer != NULL && ctx->metadata_block_header.driveManufacturerLength > 0)
    {
        memcpy(buffer + pos, ctx->drive_manufacturer, ctx->metadata_block_header.driveManufacturerLength);
        ctx->metadata_block_header.driveManufacturerOffset = pos;
        pos += ctx->metadata_block_header.driveManufacturerLength;
    }

    if(ctx->drive_model != NULL && ctx->metadata_block_header.driveModelLength > 0)
    {
        memcpy(buffer + pos, ctx->drive_model, ctx->metadata_block_header.driveModelLength);
        ctx->metadata_block_header.driveModelOffset = pos;
        pos += ctx->metadata_block_header.driveModelLength;
    }

    if(ctx->drive_serial_number != NULL && ctx->metadata_block_header.driveSerialNumberLength > 0)
    {
        memcpy(buffer + pos, ctx->drive_serial_number, ctx->metadata_block_header.driveSerialNumberLength);
        ctx->metadata_block_header.driveSerialNumberOffset = pos;
        pos += ctx->metadata_block_header.driveSerialNumberLength;
    }

    if(ctx->drive_firmware_revision != NULL && ctx->metadata_block_header.driveFirmwareRevisionLength > 0)
    {
        memcpy(buffer + pos, ctx->drive_firmware_revision, ctx->metadata_block_header.driveFirmwareRevisionLength);
        ctx->metadata_block_header.driveFirmwareRevisionOffset = pos;
    }

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }

    memcpy(buffer, &ctx->metadata_block_header, sizeof(MetadataBlockHeader));

    TRACE("Writing metadata block at position %ld", block_position);

    if(fwrite(buffer, ctx->metadata_block_header.blockSize, 1, ctx->imageStream) == 1)
    {
        TRACE("Successfully wrote metadata block");

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing metadata block index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == MetadataBlock && entry->dataType == 0)
            {
                TRACE("Found existing metadata block index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add metadata block to index
        TRACE("Adding metadata block to index");
        IndexEntry index_entry;
        index_entry.blockType = MetadataBlock;
        index_entry.dataType  = 0;
        index_entry.offset    = block_position;
        utarray_push_back(ctx->index_entries, &index_entry);
        ctx->dirty_index_block = true;
        TRACE("Added metadata block index entry at offset %" PRIu64, block_position);
    }

    free(buffer);
}

/**
 * @brief Serialize the dump hardware block containing acquisition environment information.
 *
 * This function writes a DumpHardwareBlock to the image file, documenting the hardware and software
 * environments used to create the image. A dump hardware block records one or more "dump environments" –
 * typically combinations of physical devices (drives, controllers, adapters) and the software stacks
 * that performed the read operations. This metadata is essential for understanding the imaging context,
 * validating acquisition integrity, reproducing imaging conditions, and supporting forensic or archival
 * documentation requirements.
 *
 * Each environment entry includes hardware identification (manufacturer, model, revision, firmware,
 * serial number), software identification (name, version, operating system), and optional extent ranges
 * that specify which logical sectors or units were contributed by that particular environment. This
 * structure supports complex imaging scenarios where multiple devices or software configurations were
 * used to create a composite image.
 *
 * The dump hardware block is optional; if no dump hardware information has been populated
 * (dumpHardwareEntriesWithData is NULL, entries count is zero, or identifier is not set to
 * DumpHardwareBlock), the function returns immediately without writing anything. This no-op behavior
 * allows the close operation to proceed gracefully whether or not dump hardware metadata was included
 * during image creation.
 *
 * **Block structure:**
 * The serialized block consists of:
 *   1. DumpHardwareHeader (16 bytes: identifier, entries count, payload length, CRC64)
 *   2. For each dump hardware entry (variable size):
 *      - DumpHardwareEntry (36 bytes: length fields for all strings and extent count)
 *      - Variable-length UTF-8 strings in order: manufacturer, model, revision, firmware, serial,
 *        software name, software version, software operating system
 *      - Array of DumpExtent structures (16 bytes each) if extent count > 0
 *
 * All strings are UTF-8 encoded and NOT null-terminated in the serialized block. String lengths
 * are measured in bytes, not character counts.
 *
 * **Serialization process:**
 * 1. Allocate a temporary buffer sized to hold the complete block (header + all payload data)
 * 2. Iterate through all dump hardware entries in ctx->dumpHardwareEntriesWithData
 * 3. For each entry, copy the DumpHardwareEntry structure followed by each non-NULL string
 *    (only if the corresponding length > 0), then copy the extent array (if extents > 0)
 * 4. Calculate CRC64-ECMA over the payload (everything after the header)
 * 5. Copy the DumpHardwareHeader with calculated CRC64 to the beginning of the buffer
 * 6. Align file position to block boundary
 * 7. Write the complete buffer to the image file
 * 8. Add index entry on successful write
 * 9. Free the temporary buffer
 *
 * **Alignment and file positioning:**
 * Before writing the block, the file position is moved to EOF and then aligned forward to the
 * next boundary satisfying (position & alignment_mask) == 0, where alignment_mask is derived
 * from ctx->userDataDdtHeader.blockAlignmentShift. This ensures the dump hardware block begins
 * on a properly aligned offset for efficient I/O and compliance with the Aaru format specification.
 *
 * **CRC64 calculation:**
 * The function calculates CRC64-ECMA over the payload portion of the buffer (everything after
 * the DumpHardwareHeader) and stores it in the header before writing. This checksum allows
 * verification of dump hardware block integrity when reading the image.
 *
 * **Index registration:**
 * After successfully writing the complete block, an IndexEntry is appended to ctx->indexEntries with:
 *   - blockType = DumpHardwareBlock
 *   - dataType = 0 (dump hardware blocks have no subtype)
 *   - offset = the aligned file position where the block was written
 *
 * **Error handling:**
 * Memory allocation failures (calloc returning NULL) cause immediate return without writing.
 * Bounds checking is performed during serialization; if calculated entry sizes exceed the allocated
 * buffer, the buffer is freed and the function returns without writing. Write errors (fwrite
 * returning < 1) are silently ignored; no index entry is added if the write fails. Diagnostic
 * TRACE logs report success or failure. The function does not propagate error codes; higher-level
 * close logic must validate overall integrity if needed.
 *
 * **No-op conditions:**
 * - ctx->dumpHardwareEntriesWithData is NULL (no hardware data loaded) OR
 * - ctx->dumpHardwareHeader.entries == 0 (no entries to write) OR
 * - ctx->dumpHardwareHeader.identifier != DumpHardwareBlock (block not properly initialized)
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->dumpHardwareHeader contains the header with identifier, entry count, and
 *            total payload length. ctx->dumpHardwareEntriesWithData contains the array of
 *            dump hardware entries with their associated string data and extents (may be NULL
 *            if no dump hardware was added). ctx->imageStream must be open and writable.
 *            ctx->indexEntries must be initialized (utarray) to accept new index entries.
 *
 * @note Dump Hardware Environments:
 *       - Each entry represents one hardware/software combination used during imaging
 *       - Multiple entries support scenarios where different devices contributed different sectors
 *       - Extent arrays specify which logical sector ranges each environment contributed
 *       - Empty extent arrays (extents == 0) indicate the environment dumped the entire medium
 *       - Overlapping extents between entries may indicate verification passes or redundancy
 *
 * @note Hardware Identification Fields:
 *       - manufacturer: Device manufacturer (e.g., "Plextor", "Sony", "Samsung")
 *       - model: Device model number (e.g., "PX-716A", "DRU-820A")
 *       - revision: Hardware revision identifier
 *       - firmware: Firmware version (e.g., "1.11", "KY08")
 *       - serial: Device serial number for unique identification
 *
 * @note Software Identification Fields:
 *       - softwareName: Dumping software name (e.g., "Aaru", "ddrescue", "IsoBuster")
 *       - softwareVersion: Software version (e.g., "5.3.0", "1.25")
 *       - softwareOperatingSystem: Host OS (e.g., "Linux 5.10.0", "Windows 10", "macOS 12.0")
 *
 * @note String Encoding:
 *       - All strings are UTF-8 encoded
 *       - Strings are NOT null-terminated in the serialized block
 *       - String lengths in DumpHardwareEntry are in bytes, not character counts
 *       - The library maintains null-terminated strings in memory for convenience
 *       - Only non-null-terminated data is written to the file
 *
 * @note Memory Management:
 *       - The function allocates a temporary buffer to serialize the entire block
 *       - The buffer is freed before the function returns, regardless of success or failure
 *       - The source data in ctx->dumpHardwareEntriesWithData is not modified
 *       - The source data is freed later during context cleanup (aaruf_close)
 *
 * @note Use Cases:
 *       - Forensic documentation requiring complete equipment chain of custody
 *       - Archival metadata for long-term preservation requirements
 *       - Reproducing imaging conditions for verification or re-imaging
 *       - Identifying firmware-specific issues or drive-specific behaviors
 *       - Multi-device imaging scenario documentation
 *       - Correlating imaging artifacts with specific hardware/software combinations
 *
 * @note Order in Close Sequence:
 *       - Dump hardware blocks are typically written after sector data but before metadata blocks
 *       - The exact position in the file depends on what other blocks precede it
 *       - The index entry ensures the dump hardware block can be located during subsequent opens
 *
 * @warning The temporary buffer allocation may fail on systems with limited memory or when the
 *          dump hardware block is extremely large (many entries with long strings and extents).
 *          Allocation failures result in silent no-op; the image is created without dump hardware.
 *
 * @warning Bounds checking during serialization protects against buffer overruns. If calculated
 *          entry sizes exceed the allocated buffer length (which should never occur if the
 *          header's length field is correct), the function aborts without writing. This is a
 *          sanity check against data corruption.
 *
 * @see DumpHardwareHeader for the block header structure definition.
 * @see DumpHardwareEntry for the per-environment entry structure definition.
 * @see DumpExtent for the extent range structure definition.
 * @see aaruf_get_dumphw() for retrieving dump hardware from opened images.
 * @see process_dumphw_block() for the loading process during image opening.
 *
 * @internal
 */
static void write_dumphw_block(aaruformat_context *ctx)
{

    if(ctx->dump_hardware_entries_with_data == NULL || ctx->dump_hardware_header.entries == 0 ||
       ctx->dump_hardware_header.identifier != DumpHardwareBlock)
        return;

    const size_t required_length = sizeof(DumpHardwareHeader) + ctx->dump_hardware_header.length;

    uint8_t *buffer = calloc(1, required_length);

    if(buffer == NULL) return;

    // Start to iterate and copy the data
    size_t offset = sizeof(DumpHardwareHeader);
    for(int i = 0; i < ctx->dump_hardware_header.entries; i++)
    {
        size_t entry_size = sizeof(DumpHardwareEntry) +
                            ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength +
                            ctx->dump_hardware_entries_with_data[i].entry.modelLength +
                            ctx->dump_hardware_entries_with_data[i].entry.revisionLength +
                            ctx->dump_hardware_entries_with_data[i].entry.firmwareLength +
                            ctx->dump_hardware_entries_with_data[i].entry.serialLength +
                            ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength +
                            ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength +
                            ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength +
                            ctx->dump_hardware_entries_with_data[i].entry.extents * sizeof(DumpExtent);

        if(offset + entry_size > required_length)
        {
            FATAL("Calculated size exceeds provided buffer length");
            free(buffer);
            return;
        }

        memcpy(buffer + offset, &ctx->dump_hardware_entries_with_data[i].entry, sizeof(DumpHardwareEntry));
        offset += sizeof(DumpHardwareEntry);
        if(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].manufacturer != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].manufacturer,
                   ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.modelLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].model != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].model,
                   ctx->dump_hardware_entries_with_data[i].entry.modelLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.modelLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.revisionLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].revision != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].revision,
                   ctx->dump_hardware_entries_with_data[i].entry.revisionLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.revisionLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.firmwareLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].firmware != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].firmware,
                   ctx->dump_hardware_entries_with_data[i].entry.firmwareLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.firmwareLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.serialLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].serial != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].serial,
                   ctx->dump_hardware_entries_with_data[i].entry.serialLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.serialLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].softwareName != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].softwareName,
                   ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].softwareVersion != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].softwareVersion,
                   ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem,
                   ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.extents > 0 &&
           ctx->dump_hardware_entries_with_data[i].extents != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].extents,
                   ctx->dump_hardware_entries_with_data[i].entry.extents * sizeof(DumpExtent));
            offset += ctx->dump_hardware_entries_with_data[i].entry.extents * sizeof(DumpExtent);
        }
    }

    // Calculate CRC64
    ctx->dump_hardware_header.crc64 =
        aaruf_crc64_data(buffer + sizeof(DumpHardwareHeader), ctx->dump_hardware_header.length);

    // Copy header
    memcpy(buffer, &ctx->dump_hardware_header, sizeof(DumpHardwareHeader));

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }
    TRACE("Writing dump hardware block at position %ld", block_position);
    if(fwrite(buffer, required_length, 1, ctx->imageStream) == 1)
    {
        TRACE("Successfully wrote dump hardware block");

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing dump hardware block index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == DumpHardwareBlock && entry->dataType == 0)
            {
                TRACE("Found existing dump hardware block index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add dump hardware block to index
        TRACE("Adding dump hardware block to index");
        IndexEntry index_entry;
        index_entry.blockType = DumpHardwareBlock;
        index_entry.dataType  = 0;
        index_entry.offset    = block_position;
        utarray_push_back(ctx->index_entries, &index_entry);
        ctx->dirty_index_block = true;
        TRACE("Added dump hardware block index entry at offset %" PRIu64, block_position);
    }

    free(buffer);
}

/**
 * @brief Serialize the CICM XML metadata block to the image file.
 *
 * This function writes a CicmBlock containing embedded CICM (Canary Islands Computer Museum) XML
 * metadata to the Aaru image file. The CICM XML format is a standardized metadata schema used for
 * documenting preservation and archival information about media and disk images. The XML payload
 * is stored in its original form without parsing, interpretation, or validation by the library,
 * preserving the exact structure and content provided during image creation.
 *
 * The CICM block is optional; if no CICM metadata has been populated (cicmBlock is NULL, length
 * is zero, or identifier is not set to CicmBlock), the function returns immediately without
 * writing anything. This no-op behavior allows the close operation to proceed gracefully whether
 * or not CICM metadata was included during image creation.
 *
 * **Block structure:**
 * The serialized block consists of:
 *   1. CicmMetadataBlock header (8 bytes: identifier + length)
 *   2. Variable-length XML payload: the raw UTF-8 encoded CICM XML data
 *
 * The header contains:
 *   - identifier: Always set to CicmBlock (0x4D434943, "CICM" in ASCII)
 *   - length: Size in bytes of the XML payload that immediately follows the header
 *
 * **Alignment and file positioning:**
 * Before writing the block, the file position is moved to EOF and then aligned forward to the
 * next boundary satisfying (position & alignment_mask) == 0, where alignment_mask is derived
 * from ctx->userDataDdtHeader.blockAlignmentShift. This ensures the CICM block begins on a
 * properly aligned offset for efficient I/O and compliance with the Aaru format specification.
 *
 * **Write sequence:**
 * The function performs a two-stage write operation:
 *   1. Write the CicmMetadataBlock header (sizeof(CicmMetadataBlock) bytes)
 *   2. Write the XML payload (ctx->cicmBlockHeader.length bytes)
 *
 * Both writes must succeed for the block to be considered successfully written. If the header
 * write fails, the payload write is skipped. Only if both writes succeed is an index entry added.
 *
 * **Index registration:**
 * After successfully writing both the header and XML payload, an IndexEntry is appended to
 * ctx->indexEntries with:
 *   - blockType = CicmBlock
 *   - dataType = 0 (CICM blocks have no subtype)
 *   - offset = the aligned file position where the CicmMetadataBlock header was written
 *
 * **Error handling:**
 * Write errors (fwrite returning < 1) are silently ignored; no index entry is added if either
 * write fails. Diagnostic TRACE logs report success or failure. The function does not propagate
 * error codes; higher-level close logic must validate overall integrity if needed.
 *
 * **No-op conditions:**
 * - ctx->cicmBlock is NULL (no XML data loaded) OR
 * - ctx->cicmBlockHeader.length == 0 (empty metadata) OR
 * - ctx->cicmBlockHeader.identifier != CicmBlock (block not properly initialized)
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->cicmBlockHeader contains the header with identifier and length fields.
 *            ctx->cicmBlock points to the actual UTF-8 encoded XML data (may be NULL if no
 *            CICM metadata was provided). ctx->imageStream must be open and writable.
 *            ctx->indexEntries must be initialized (utarray) to accept new index entries.
 *
 * @note XML Encoding and Format:
 *       - The XML payload is stored in UTF-8 encoding
 *       - The payload may or may not be null-terminated
 *       - The library treats the XML as opaque binary data
 *       - No XML parsing, interpretation, or validation is performed during write
 *       - Schema compliance is the responsibility of the code that set the CICM metadata
 *
 * @note CICM Metadata Purpose:
 *       - Developed by the Canary Islands Computer Museum for digital preservation
 *       - Documents comprehensive preservation metadata following a standardized schema
 *       - Includes checksums for data integrity verification
 *       - Records detailed device and media information
 *       - Supports archival and long-term preservation requirements
 *       - Used by cultural heritage institutions and archives
 *
 * @note Memory Management:
 *       - The function does not allocate or free any memory
 *       - ctx->cicmBlock memory is managed by the caller (typically freed during aaruf_close)
 *       - The XML data is written directly from the existing buffer
 *
 * @note Unlike data blocks (which may be compressed and include CRC64 checksums), the CICM
 *       block is written without compression or explicit integrity checking. The XML payload
 *       is written verbatim as provided, relying on file-level integrity mechanisms.
 *
 * @note Order in Close Sequence:
 *       - CICM blocks are typically written after structural data blocks but before the index
 *       - The exact position in the file depends on what other blocks precede it
 *       - The index entry ensures the CICM block can be located during subsequent opens
 *
 * @see CicmMetadataBlock for the on-disk structure definition.
 * @see aaruf_get_cicm_metadata() for retrieving CICM XML from opened images.
 *
 * @internal
 */
static void write_cicm_block(aaruformat_context *ctx)
{
    if(ctx->cicm_block == NULL || ctx->cicm_block_header.length == 0 || ctx->cicm_block_header.identifier != CicmBlock)
        return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;

    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }

    TRACE("Writing CICM XML block at position %ld", block_position);
    if(fwrite(&ctx->cicm_block_header, sizeof(CicmMetadataBlock), 1, ctx->imageStream) == 1 &&
       fwrite(ctx->cicm_block, ctx->cicm_block_header.length, 1, ctx->imageStream) == 1)
    {
        TRACE("Successfully wrote CICM XML block");

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing CICM XML block index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == CicmBlock && entry->dataType == 0)
            {
                TRACE("Found existing CICM XML block index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add CICM block to index
        TRACE("Adding CICM XML block to index");
        IndexEntry index_entry;
        index_entry.blockType = CicmBlock;
        index_entry.dataType  = 0;
        index_entry.offset    = block_position;
        utarray_push_back(ctx->index_entries, &index_entry);
        ctx->dirty_index_block = true;
        TRACE("Added CICM XML block index entry at offset %" PRIu64, block_position);
    }
}

/**
 * @brief Serialize the Aaru metadata JSON block to the image file.
 *
 * This function writes an AaruMetadataJsonBlock containing embedded Aaru metadata JSON to the
 * Aaru image file. The Aaru metadata JSON format is a structured, machine-readable representation
 * of comprehensive image metadata including media information, imaging session details, hardware
 * configuration, optical disc tracks and sessions, checksums, and preservation metadata. The JSON
 * payload is stored in its original form without parsing, interpretation, or validation by the
 * library, preserving the exact structure and content provided during image creation.
 *
 * The Aaru JSON block is optional; if no Aaru JSON metadata has been populated (jsonBlock is NULL,
 * length is zero, or identifier is not set to AaruMetadataJsonBlock), the function returns
 * immediately without writing anything. This no-op behavior allows the close operation to proceed
 * gracefully whether or not Aaru JSON metadata was included during image creation.
 *
 * **Block structure:**
 * The serialized block consists of:
 *   1. AaruMetadataJsonBlockHeader (8 bytes: identifier + length)
 *   2. Variable-length JSON payload: the raw UTF-8 encoded Aaru metadata JSON data
 *
 * The header contains:
 *   - identifier: Always set to AaruMetadataJsonBlock (0x444D534A, "JSMD" in ASCII)
 *   - length: Size in bytes of the JSON payload that immediately follows the header
 *
 * **Alignment and file positioning:**
 * Before writing the block, the file position is moved to EOF and then aligned forward to the
 * next boundary satisfying (position & alignment_mask) == 0, where alignment_mask is derived
 * from ctx->userDataDdtHeader.blockAlignmentShift. This ensures the Aaru JSON block begins on a
 * properly aligned offset for efficient I/O and compliance with the Aaru format specification.
 *
 * **Write sequence:**
 * The function performs a two-stage write operation:
 *   1. Write the AaruMetadataJsonBlockHeader (sizeof(AaruMetadataJsonBlockHeader) bytes)
 *   2. Write the JSON payload (ctx->jsonBlockHeader.length bytes)
 *
 * Both writes must succeed for the block to be considered successfully written. If the header
 * write fails, the payload write is skipped. Only if both writes succeed is an index entry added.
 *
 * **Index registration:**
 * After successfully writing both the header and JSON payload, an IndexEntry is appended to
 * ctx->indexEntries with:
 *   - blockType = AaruMetadataJsonBlock
 *   - dataType = 0 (Aaru JSON blocks have no subtype)
 *   - offset = the aligned file position where the AaruMetadataJsonBlockHeader was written
 *
 * **Error handling:**
 * Write errors (fwrite returning < 1) are silently ignored; no index entry is added if either
 * write fails. Diagnostic TRACE logs report success or failure. The function does not propagate
 * error codes; higher-level close logic must validate overall integrity if needed.
 *
 * **No-op conditions:**
 * - ctx->jsonBlock is NULL (no JSON data loaded) OR
 * - ctx->jsonBlockHeader.length == 0 (empty metadata) OR
 * - ctx->jsonBlockHeader.identifier != AaruMetadataJsonBlock (block not properly initialized)
 *
 * @param ctx Pointer to an initialized aaruformatContext in write mode. Must not be NULL.
 *            ctx->jsonBlockHeader contains the header with identifier and length fields.
 *            ctx->jsonBlock points to the actual UTF-8 encoded JSON data (may be NULL if no
 *            Aaru JSON metadata was provided). ctx->imageStream must be open and writable.
 *            ctx->indexEntries must be initialized (utarray) to accept new index entries.
 *
 * @note JSON Encoding and Format:
 *       - The JSON payload is stored in UTF-8 encoding
 *       - The payload may or may not be null-terminated
 *       - The library treats the JSON as opaque binary data
 *       - No JSON parsing, interpretation, or validation is performed during write
 *       - Schema compliance is the responsibility of the code that set the Aaru JSON metadata
 *
 * @note Aaru Metadata JSON Purpose:
 *       - Provides machine-readable structured metadata using modern JSON format
 *       - Includes comprehensive information about media, sessions, tracks, and checksums
 *       - Enables programmatic access to metadata without XML parsing overhead
 *       - Documents imaging session details, hardware configuration, and preservation data
 *       - Used by Aaru and compatible tools for metadata exchange and analysis
 *       - Complements or serves as alternative to CICM XML metadata
 *
 * @note Memory Management:
 *       - The function does not allocate or free any memory
 *       - ctx->jsonBlock memory is managed by the caller (typically freed during aaruf_close)
 *       - The JSON data is written directly from the existing buffer
 *
 * @note Unlike data blocks (which may be compressed and include CRC64 checksums), the Aaru JSON
 *       block is written without compression or explicit integrity checking. The JSON payload
 *       is written verbatim as provided, relying on file-level integrity mechanisms.
 *
 * @note Order in Close Sequence:
 *       - Aaru JSON blocks are typically written after CICM blocks but before the index
 *       - The exact position in the file depends on what other blocks precede it
 *       - The index entry ensures the Aaru JSON block can be located during subsequent opens
 *
 * @note Distinction from CICM XML:
 *       - Both CICM XML and Aaru JSON blocks can be written to the same image
 *       - CICM XML follows the Canary Islands Computer Museum schema (older format)
 *       - Aaru JSON follows the Aaru-specific metadata schema (newer format)
 *       - They serve similar purposes but with different structures and consumers
 *       - Including both provides maximum compatibility across different tools
 *
 * @see AaruMetadataJsonBlockHeader for the on-disk structure definition.
 * @see aaruf_set_aaru_json_metadata() for setting Aaru JSON during image creation.
 * @see aaruf_get_aaru_json_metadata() for retrieving Aaru JSON from opened images.
 * @see write_cicm_block() for the similar function that writes CICM XML blocks.
 *
 * @internal
 */
static void write_aaru_json_block(aaruformat_context *ctx)
{
    if(ctx->json_block == NULL || ctx->json_block_header.length == 0 ||
       ctx->json_block_header.identifier != AaruMetadataJsonBlock)
        return;

    fseek(ctx->imageStream, 0, SEEK_END);
    long           block_position = ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;

    if(block_position & alignment_mask)
    {
        const uint64_t aligned_position = block_position + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, aligned_position, SEEK_SET);
        block_position = aligned_position;
    }

    TRACE("Writing Aaru metadata JSON block at position %ld", block_position);
    if(fwrite(&ctx->json_block_header, sizeof(AaruMetadataJsonBlockHeader), 1, ctx->imageStream) == 1 &&
       fwrite(ctx->json_block, ctx->json_block_header.length, 1, ctx->imageStream) == 1)
    {
        TRACE("Successfully wrote Aaru metadata JSON block");

        // Remove any previously existing index entries of the same type before adding
        TRACE("Removing any previously existing Aaru metadata JSON block index entries");
        for(int k = utarray_len(ctx->index_entries) - 1; k >= 0; k--)
        {
            const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
            if(entry && entry->blockType == AaruMetadataJsonBlock && entry->dataType == 0)
            {
                TRACE("Found existing Aaru metadata JSON block index entry at position %d, removing", k);
                utarray_erase(ctx->index_entries, k, 1);
            }
        }

        // Add Aaru metadata JSON block to index
        TRACE("Adding Aaru metadata JSON block to index");
        IndexEntry index_entry;
        index_entry.blockType = AaruMetadataJsonBlock;
        index_entry.dataType  = 0;
        index_entry.offset    = block_position;
        utarray_push_back(ctx->index_entries, &index_entry);
        ctx->dirty_index_block = true;
        TRACE("Added Aaru metadata JSON block index entry at offset %" PRIu64, block_position);
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
static int32_t write_index_block(aaruformat_context *ctx)
{
    // Write the complete index at the end of the file
    TRACE("Writing index at the end of the file");
    fseek(ctx->imageStream, 0, SEEK_END);
    long index_position = ftell(ctx->imageStream);

    // Align index position to block boundary if needed
    uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
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
    index_header.entries    = utarray_len(ctx->index_entries);
    index_header.previous   = 0;  // No previous index for now

    TRACE("Writing index with %" PRIu64 " entries at position %ld", index_header.entries, index_position);

    // Calculate CRC64 of index entries
    crc64_ctx *index_crc64_context = aaruf_crc64_init();
    if(index_crc64_context != NULL && index_header.entries > 0)
    {
        size_t index_data_size = index_header.entries * sizeof(IndexEntry);
        aaruf_crc64_update(index_crc64_context, utarray_front(ctx->index_entries), index_data_size);
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

            for(entry = (IndexEntry *)utarray_front(ctx->index_entries); entry != NULL;
                entry = (IndexEntry *)utarray_next(ctx->index_entries, entry))
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
AARU_EXPORT int AARU_CALL aaruf_close(void *context)
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

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        errno = EINVAL;
        return -1;
    }

    if(ctx->is_writing)
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
        if(ctx->writing_buffer != NULL)
        {
            int error = aaruf_close_current_block(ctx);

            if(error != AARUF_STATUS_OK) return error;
        }

        int32_t res;
        if(ctx->is_tape)
        {
            // Write tape DDT
            if(ctx->dirty_tape_ddt)
            {
                res = write_tape_ddt(ctx);
                if(res != AARUF_STATUS_OK) return res;
            }
        }
        else
        {
            // Write cached secondary DDT table if any
            if(ctx->dirty_secondary_ddt)
            {
                res = write_cached_secondary_ddt(ctx);
                if(res != AARUF_STATUS_OK) return res;
            }

            // Write primary DDT table (multi-level) if applicable
            if(ctx->dirty_primary_ddt)
            {
                res = write_primary_ddt(ctx);
                if(res != AARUF_STATUS_OK) return res;
            }

            // Write single-level DDT table if applicable
            if(ctx->dirty_single_level_ddt)
            {
                res = write_single_level_ddt(ctx);
                if(res != AARUF_STATUS_OK) return res;
            }
        }

        // Finalize checksums and write checksum block
        if(ctx->dirty_checksum_block) write_checksum_block(ctx);

        // Write tracks block
        if(ctx->dirty_tracks_block) write_tracks_block(ctx);

        // Write MODE 2 subheader data block
        if(ctx->dirty_mode2_subheaders_block) write_mode2_subheaders_block(ctx);

        // Write CD sector prefix data block
        if(ctx->dirty_sector_prefix_block) write_sector_prefix(ctx);

        // Write sector prefix DDT (statuses + optional indexes)
        if(ctx->dirty_sector_prefix_ddt) write_sector_prefix_ddt(ctx);

        // Write CD sector suffix data block (EDC/ECC captures)
        if(ctx->dirty_sector_suffix_block) write_sector_suffix(ctx);

        // Write sector prefix DDT (EDC/ECC captures)
        if(ctx->dirty_sector_suffix_ddt) write_sector_suffix_ddt(ctx);

        // Write sector subchannel data block
        if(ctx->dirty_sector_subchannel_block) write_sector_subchannel(ctx);

        // Write DVD long sector data blocks
        if(ctx->dirty_dvd_long_sector_blocks) write_dvd_long_sector_blocks(ctx);

        // Write DVD decrypted title keys
        if(ctx->dirty_dvd_title_key_decrypted_block) write_dvd_title_key_decrypted_block(ctx);

        // Write media tags data blocks
        if(ctx->dirty_media_tags) write_media_tags(ctx);

        // Write tape files
        if(ctx->dirty_tape_file_block) write_tape_file_block(ctx);

        // Write tape partitions
        if(ctx->dirty_tape_partition_block) write_tape_partition_block(ctx);

        // Write geometry block if any
        if(ctx->dirty_geometry_block) write_geometry_block(ctx);

        // Write metadata block
        if(ctx->dirty_metadata_block) write_metadata_block(ctx);

        // Write dump hardware block if any
        if(ctx->dirty_dumphw_block) write_dumphw_block(ctx);

        // Write CICM XML block if any
        if(ctx->dirty_cicm_block) write_cicm_block(ctx);

        // Write Aaru metadata JSON block if any
        if(ctx->dirty_json_block) write_aaru_json_block(ctx);

        // Write the complete index at the end of the file
        if(ctx->dirty_index_block)
        {
            res = write_index_block(ctx);
            if(res != AARUF_STATUS_OK) return res;
        }

        if(ctx->deduplicate && ctx->sector_hash_map != NULL)
        {
            TRACE("Clearing sector hash map");
            // Clear sector hash map
            free_map(ctx->sector_hash_map);
            ctx->sector_hash_map = NULL;
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
    if(ctx->index_entries != NULL)
    {
        utarray_free(ctx->index_entries);
        ctx->index_entries = NULL;
    }

    free(ctx->sector_prefix);
    ctx->sector_prefix = NULL;
    free(ctx->sector_prefix_corrected);
    ctx->sector_prefix_corrected = NULL;
    free(ctx->sector_suffix);
    ctx->sector_suffix = NULL;
    free(ctx->sector_suffix_corrected);
    ctx->sector_suffix_corrected = NULL;
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
    if(!ctx->in_memory_ddt)
    {
        munmap(ctx->user_data_ddt, ctx->mapped_memory_ddt_size);
        ctx->user_data_ddt = NULL;
    }
#endif

    free(ctx->sector_prefix_ddt2);
    ctx->sector_prefix_ddt2 = NULL;
    free(ctx->sector_prefix_ddt);
    ctx->sector_prefix_ddt = NULL;
    free(ctx->sector_suffix_ddt2);
    ctx->sector_suffix_ddt2 = NULL;
    free(ctx->sector_suffix_ddt);
    ctx->sector_suffix_ddt = NULL;

    free(ctx->metadata_block);
    ctx->metadata_block = NULL;
    free(ctx->track_entries);
    ctx->track_entries = NULL;
    free(ctx->cicm_block);
    ctx->cicm_block = NULL;

    if(ctx->dump_hardware_entries_with_data != NULL)
    {
        for(int i = 0; i < ctx->dump_hardware_header.entries; i++)
        {
            free(ctx->dump_hardware_entries_with_data[i].extents);
            ctx->dump_hardware_entries_with_data[i].extents = NULL;
            free(ctx->dump_hardware_entries_with_data[i].manufacturer);
            ctx->dump_hardware_entries_with_data[i].manufacturer = NULL;
            free(ctx->dump_hardware_entries_with_data[i].model);
            ctx->dump_hardware_entries_with_data[i].model = NULL;
            free(ctx->dump_hardware_entries_with_data[i].revision);
            ctx->dump_hardware_entries_with_data[i].revision = NULL;
            free(ctx->dump_hardware_entries_with_data[i].firmware);
            ctx->dump_hardware_entries_with_data[i].firmware = NULL;
            free(ctx->dump_hardware_entries_with_data[i].serial);
            ctx->dump_hardware_entries_with_data[i].serial = NULL;
            free(ctx->dump_hardware_entries_with_data[i].softwareName);
            ctx->dump_hardware_entries_with_data[i].softwareName = NULL;
            free(ctx->dump_hardware_entries_with_data[i].softwareVersion);
            ctx->dump_hardware_entries_with_data[i].softwareVersion = NULL;
            free(ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem);
            ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem = NULL;
        }
        ctx->dump_hardware_entries_with_data = NULL;
    }

    free(ctx->readableSectorTags);
    ctx->readableSectorTags = NULL;

    free(ctx->ecc_cd_context);
    ctx->ecc_cd_context = NULL;

    free(ctx->checksums.spamsum);
    ctx->checksums.spamsum = NULL;

    free(ctx->sector_id);
    free(ctx->sector_ied);
    free(ctx->sector_cpr_mai);
    free(ctx->sector_edc);

    // TODO: Free caches

    free(context);

    TRACE("Exiting aaruf_close() = 0");
    return 0;
}
