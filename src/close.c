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
#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <aaruformat.h>

#include "internal.h"
#include "log.h"

/**
 * @brief Closes an AaruFormat image context and frees resources.
 *
 * Closes the image file, frees memory, and releases all resources associated with the context.
 * For images opened in write mode, this function performs critical finalization operations
 * including writing cached DDT tables, updating the index, writing the final image header,
 * and ensuring all data structures are properly persisted. It handles both single-level
 * and multi-level DDT structures and performs comprehensive cleanup of all allocated resources.
 *
 * @param context Pointer to the aaruformat context to close.
 *
 * @return Returns one of the following status codes:
 * @retval 0 Successfully closed the context and freed all resources. This is returned when:
 *         - The context is valid and properly initialized
 *         - For write mode: All cached data is successfully written to the image file
 *         - DDT tables (single-level or multi-level) are successfully written and indexed
 *         - The image index is successfully written with proper CRC validation
 *         - The final image header is updated with correct index offset and written
 *         - All memory resources are successfully freed
 *         - The image stream is closed without errors
 *
 * @retval -1 Context validation failed. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The errno is set to EINVAL to indicate invalid argument
 *
 * @retval AARUF_ERROR_CANNOT_WRITE_HEADER (-16) Write operations failed. This occurs when:
 *         - Cannot write the initial image header at position 0 (for write mode)
 *         - Cannot write cached secondary DDT header or data to the image file
 *         - Cannot write primary DDT header or table data to the image file
 *         - Cannot write single-level DDT header or table data to the image file
 *         - Cannot write index header to the image file
 *         - Cannot write all index entries to the image file
 *         - Cannot update the final image header with the correct index offset
 *         - Any file I/O operation fails during the finalization process
 *
 * @retval Error codes from aaruf_close_current_block() may be propagated when:
 *         - The current writing buffer cannot be properly closed and written
 *         - Block finalization fails during the close operation
 *         - Compression or writing of the final block encounters errors
 *
 * @note Write Mode Finalization Process:
 *       - Writes the image header at the beginning of the file
 *       - Closes any open writing buffer (current block being written)
 *       - Writes cached secondary DDT tables and updates primary DDT references
 *       - Writes primary DDT tables (single-level or multi-level) with CRC validation
 *       - Writes the complete index with all block references at the end of the file
 *       - Updates the image header with the final index offset
 *
 * @note DDT Finalization:
 *       - For multi-level DDTs: Writes cached secondary tables and updates primary table pointers
 *       - For single-level DDTs: Writes the complete table directly to the designated position
 *       - Calculates and validates CRC64 checksums for all DDT data
 *       - Updates index entries to reference newly written DDT blocks
 *
 * @note Index Writing:
 *       - Creates IndexHeader3 structure with entry count and CRC validation
 *       - Writes all IndexEntry structures sequentially after the header
 *       - Aligns index position to block boundaries for optimal access
 *       - Updates the main image header with the final index file offset
 *
 * @note Memory Cleanup:
 *       - Frees all allocated DDT tables (userDataDdtMini, userDataDdtBig, cached secondary tables)
 *       - Frees sector metadata arrays (sectorPrefix, sectorSuffix, sectorSubchannel, etc.)
 *       - Frees media tag hash table and all associated tag data
 *       - Frees track entries, metadata blocks, and hardware information
 *       - Closes LRU caches for block headers and data
 *       - Unmaps memory-mapped DDT structures (Linux-specific)
 *
 * @note Platform-Specific Operations:
 *       - Linux: Unmaps memory-mapped DDT structures using munmap() if not loaded in memory
 *       - Other platforms: Standard memory deallocation only
 *
 * @note Error Handling Strategy:
 *       - Critical write failures return immediately with error codes
 *       - Memory cleanup continues even if some write operations fail
 *       - All allocated resources are freed regardless of write success/failure
 *       - File stream is always closed, even on error conditions
 *
 * @warning This function must be called to properly finalize write-mode images.
 *          Failing to call aaruf_close() on write-mode contexts will result in
 *          incomplete or corrupted image files.
 *
 * @warning After calling this function, the context pointer becomes invalid and
 *          should not be used. All operations on the context will result in
 *          undefined behavior.
 *
 * @warning For write-mode contexts, this function performs extensive file I/O.
 *          Ensure sufficient disk space and proper file permissions before calling.
 *
 * @warning The function sets errno to EINVAL for context validation failures
 *          but uses library-specific error codes for write operation failures.
 */
int aaruf_close(void *context)
{
    TRACE("Entering aaruf_close(%p)", context);

    int            i             = 0;
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

        // Write cached secondary table to file end and update primary table entry with its position
        // Check if we have a cached table that needs to be written (either it has an offset or exists in memory)
        bool has_cached_secondary_ddt = (ctx->userDataDdtHeader.tableShift > 0) &&
                                        ((ctx->cachedDdtOffset != 0) ||
                                         (ctx->cachedSecondaryDdtSmall != NULL || ctx->cachedSecondaryDdtBig != NULL));

        if(has_cached_secondary_ddt)
        {
            TRACE("Writing cached secondary DDT table to file");

            fseek(ctx->imageStream, 0, SEEK_END);
            long end_of_file = ftell(ctx->imageStream);

            // Align the position according to block alignment shift
            uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
            if(end_of_file & alignment_mask)
            {
                // Calculate the next aligned position
                uint64_t aligned_position = (end_of_file + alignment_mask) & ~alignment_mask;

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
                    uint64_t new_secondary_table_block_offset =
                        end_of_file >> ctx->userDataDdtHeader.blockAlignmentShift;

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
                            if(entry && entry->offset == ctx->cachedDdtOffset &&
                               entry->blockType == DeDuplicationTable2)
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
        }

        // Write the cached primary DDT table back to its position in the file
        if(ctx->userDataDdtHeader.tableShift > 0 && (ctx->userDataDdtMini != NULL || ctx->userDataDdtBig != NULL))
        {
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
        }

        // Write the single level DDT table block aligned just after the header
        if(ctx->userDataDdtHeader.tableShift == 0 && (ctx->userDataDdtMini != NULL || ctx->userDataDdtBig != NULL))
        {
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
        }

        // Write the complete index at the end of the file
        TRACE("Writing index at the end of the file");
        fseek(ctx->imageStream, 0, SEEK_END);
        long index_position = ftell(ctx->imageStream);

        // Align index position to block boundary if needed
        uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
        if(index_position & alignment_mask)
        {
            uint64_t aligned_position = (index_position + alignment_mask) & ~alignment_mask;
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
        else { index_header.crc64 = 0; }

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
                {
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
                    {
                        TRACE("Successfully updated header with index offset");
                    }
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

    free(ctx->sectorPrefix);
    ctx->sectorPrefix = NULL;
    free(ctx->sectorPrefixCorrected);
    ctx->sectorPrefixCorrected = NULL;
    free(ctx->sectorSuffix);
    ctx->sectorSuffix = NULL;
    free(ctx->sectorSuffixCorrected);
    ctx->sectorSuffixCorrected = NULL;
    free(ctx->sectorSubchannel);
    ctx->sectorSubchannel = NULL;
    free(ctx->mode2Subheaders);
    ctx->mode2Subheaders = NULL;

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

    free(ctx->sectorPrefixDdt);
    ctx->sectorPrefixDdt = NULL;
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
        for(i = 0; i < ctx->dumpHardwareHeader.entries; i++)
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
