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

int aaruf_close(void *context)
{
    TRACE("Entering aaruf_close(%p)", context);

    int            i           = 0;
    mediaTagEntry *mediaTag    = NULL;
    mediaTagEntry *tmpMediaTag = NULL;

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
        if(ctx->userDataDdtHeader.tableShift > 0 && ctx->cachedDdtOffset != 0 &&
           (ctx->cachedSecondaryDdtSmall != NULL || ctx->cachedSecondaryDdtBig != NULL))
        {
            TRACE("Writing cached secondary DDT table to file");

            fseek(ctx->imageStream, 0, SEEK_END);
            long endOfFile = ftell(ctx->imageStream);

            // Align the position according to block alignment shift
            uint64_t alignmentMask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
            if(endOfFile & alignmentMask)
            {
                // Calculate the next aligned position
                uint64_t alignedPosition = (endOfFile + alignmentMask) & ~alignmentMask;

                // Seek to the aligned position and pad with zeros if necessary
                fseek(ctx->imageStream, alignedPosition, SEEK_SET);
                endOfFile = alignedPosition;

                TRACE("Aligned DDT write position from %ld to %" PRIu64 " (alignment shift: %d)",
                      ftell(ctx->imageStream) - (alignedPosition - endOfFile), alignedPosition,
                      ctx->userDataDdtHeader.blockAlignmentShift);
            }

            // Prepare DDT header for the cached table
            DdtHeader2 ddtHeader          = {0};
            ddtHeader.identifier          = DeDuplicationTable2;
            ddtHeader.type                = UserData;
            ddtHeader.compression         = None;
            ddtHeader.levels              = ctx->userDataDdtHeader.levels;
            ddtHeader.tableLevel          = ctx->userDataDdtHeader.tableLevel + 1;
            ddtHeader.previousLevelOffset = ctx->primaryDdtOffset;
            ddtHeader.negative            = ctx->userDataDdtHeader.negative;
            ddtHeader.overflow            = ctx->userDataDdtHeader.overflow;
            ddtHeader.blockAlignmentShift = ctx->userDataDdtHeader.blockAlignmentShift;
            ddtHeader.dataShift           = ctx->userDataDdtHeader.dataShift;
            ddtHeader.tableShift          = 0;  // Secondary tables are single level
            ddtHeader.sizeType            = ctx->userDataDdtHeader.sizeType;

            uint64_t itemsPerDdtEntry = 1 << ctx->userDataDdtHeader.tableShift;
            ddtHeader.blocks          = itemsPerDdtEntry;
            ddtHeader.entries         = itemsPerDdtEntry;

            // Calculate which DDT position this cached table represents
            uint64_t cachedDdtPosition = ctx->cachedDdtOffset >> ctx->userDataDdtHeader.blockAlignmentShift;
            ddtHeader.start            = cachedDdtPosition * itemsPerDdtEntry;

            // Calculate data size
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                ddtHeader.length = itemsPerDdtEntry * sizeof(uint16_t);
            else
                ddtHeader.length = itemsPerDdtEntry * sizeof(uint32_t);

            ddtHeader.cmpLength = ddtHeader.length;

            // Calculate CRC64 of the data
            crc64_ctx *crc64_context = aaruf_crc64_init();
            if(crc64_context != NULL)
            {
                if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                    aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtSmall, ddtHeader.length);
                else
                    aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtBig, ddtHeader.length);

                uint64_t crc64;
                aaruf_crc64_final(crc64_context, &crc64);
                ddtHeader.crc64    = crc64;
                ddtHeader.cmpCrc64 = crc64;
            }

            // Write header
            if(fwrite(&ddtHeader, sizeof(DdtHeader2), 1, ctx->imageStream) == 1)
            {
                // Write data
                size_t writtenBytes = 0;
                if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                    writtenBytes = fwrite(ctx->cachedSecondaryDdtSmall, ddtHeader.length, 1, ctx->imageStream);
                else
                    writtenBytes = fwrite(ctx->cachedSecondaryDdtBig, ddtHeader.length, 1, ctx->imageStream);

                if(writtenBytes == 1)
                {
                    // Update primary table entry to point to new location
                    uint64_t newSecondaryTableBlockOffset = endOfFile >> ctx->userDataDdtHeader.blockAlignmentShift;

                    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                        ctx->userDataDdtMini[cachedDdtPosition] = (uint16_t)newSecondaryTableBlockOffset;
                    else
                        ctx->userDataDdtBig[cachedDdtPosition] = (uint32_t)newSecondaryTableBlockOffset;

                    // Update index: remove old entry for cached DDT and add new one
                    TRACE("Updating index for cached secondary DDT");

                    // Remove old index entry for the cached DDT
                    if(ctx->cachedDdtOffset != 0)
                    {
                        TRACE("Removing old index entry for DDT at offset %" PRIu64, ctx->cachedDdtOffset);
                        IndexEntry *entry = NULL;

                        // Find and remove the old index entry
                        for(unsigned int i = 0; i < utarray_len(ctx->indexEntries); i++)
                        {
                            entry = (IndexEntry *)utarray_eltptr(ctx->indexEntries, i);
                            if(entry && entry->offset == ctx->cachedDdtOffset &&
                               entry->blockType == DeDuplicationTable2)
                            {
                                TRACE("Found old DDT index entry at position %u, removing", i);
                                utarray_erase(ctx->indexEntries, i, 1);
                                break;
                            }
                        }
                    }

                    // Add new index entry for the newly written secondary DDT
                    IndexEntry newDdtEntry;
                    newDdtEntry.blockType = DeDuplicationTable2;
                    newDdtEntry.dataType  = UserData;
                    newDdtEntry.offset    = endOfFile;

                    utarray_push_back(ctx->indexEntries, &newDdtEntry);
                    TRACE("Added new DDT index entry at offset %" PRIu64, endOfFile);

                    // Write the updated primary table back to its original position in the file
                    long savedPos = ftell(ctx->imageStream);
                    fseek(ctx->imageStream, ctx->primaryDdtOffset + sizeof(DdtHeader2), SEEK_SET);

                    size_t primaryTableSize = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                                  ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                                  : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

                    size_t primaryWrittenBytes = 0;
                    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                        primaryWrittenBytes = fwrite(ctx->userDataDdtMini, primaryTableSize, 1, ctx->imageStream);
                    else
                        primaryWrittenBytes = fwrite(ctx->userDataDdtBig, primaryTableSize, 1, ctx->imageStream);

                    if(primaryWrittenBytes != 1)
                    {
                        TRACE("Could not flush primary DDT table to file.");
                        return AARUF_ERROR_CANNOT_WRITE_HEADER;
                    }

                    fseek(ctx->imageStream, savedPos, SEEK_SET);
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
                size_t primaryTableSize = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                              ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                              : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

                if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                    aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtMini, primaryTableSize);
                else
                    aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtBig, primaryTableSize);

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
                ctx->userDataDdtHeader.length      = primaryTableSize;
                ctx->userDataDdtHeader.cmpLength   = primaryTableSize;

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
            size_t primaryTableSize = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                          ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                          : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

            // Write the primary table data
            size_t writtenBytes = 0;
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                writtenBytes = fwrite(ctx->userDataDdtMini, primaryTableSize, 1, ctx->imageStream);
            else
                writtenBytes = fwrite(ctx->userDataDdtBig, primaryTableSize, 1, ctx->imageStream);

            if(writtenBytes == 1)
            {
                TRACE("Successfully wrote primary DDT header and table to file (%" PRIu64 " entries, %zu bytes)",
                      ctx->userDataDdtHeader.entries, primaryTableSize);

                // Add primary DDT to index
                TRACE("Adding primary DDT to index");
                IndexEntry primaryDdtEntry;
                primaryDdtEntry.blockType = DeDuplicationTable2;
                primaryDdtEntry.dataType  = UserData;
                primaryDdtEntry.offset    = ctx->primaryDdtOffset;

                utarray_push_back(ctx->indexEntries, &primaryDdtEntry);
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
                size_t primaryTableSize = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                              ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                              : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

                if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                    aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtMini, primaryTableSize);
                else
                    aaruf_crc64_update(crc64_context, (uint8_t *)ctx->userDataDdtBig, primaryTableSize);

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
                ctx->userDataDdtHeader.length              = primaryTableSize;
                ctx->userDataDdtHeader.cmpLength           = primaryTableSize;

                TRACE("Calculated CRC64 for single-level DDT: 0x%16lX", crc64);
            }

            // Write the DDT header first
            fseek(ctx->imageStream, ctx->primaryDdtOffset, SEEK_SET);

            size_t headerWritten = fwrite(&ctx->userDataDdtHeader, sizeof(DdtHeader2), 1, ctx->imageStream);
            if(headerWritten != 1)
            {
                TRACE("Failed to write single-level DDT header to file");
                return AARUF_ERROR_CANNOT_WRITE_HEADER;
            }

            // Then write the table data (position is already after the header)
            size_t primaryTableSize = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                          ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                          : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

            // Write the primary table data
            size_t writtenBytes = 0;
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                writtenBytes = fwrite(ctx->userDataDdtMini, primaryTableSize, 1, ctx->imageStream);
            else
                writtenBytes = fwrite(ctx->userDataDdtBig, primaryTableSize, 1, ctx->imageStream);

            if(writtenBytes == 1)
            {
                TRACE("Successfully wrote single-level DDT header and table to file (%" PRIu64 " entries, %zu bytes)",
                      ctx->userDataDdtHeader.entries, primaryTableSize);

                // Add single-level DDT to index
                TRACE("Adding single-level DDT to index");
                IndexEntry singleDdtEntry;
                singleDdtEntry.blockType = DeDuplicationTable2;
                singleDdtEntry.dataType  = UserData;
                singleDdtEntry.offset    = ctx->primaryDdtOffset;

                utarray_push_back(ctx->indexEntries, &singleDdtEntry);
                TRACE("Added single-level DDT index entry at offset %" PRIu64, ctx->primaryDdtOffset);
            }
            else
                TRACE("Failed to write single-level DDT table data to file");
        }

        // Write the complete index at the end of the file
        TRACE("Writing index at the end of the file");
        fseek(ctx->imageStream, 0, SEEK_END);
        long indexPosition = ftell(ctx->imageStream);

        // Align index position to block boundary if needed
        uint64_t alignmentMask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
        if(indexPosition & alignmentMask)
        {
            uint64_t alignedPosition = (indexPosition + alignmentMask) & ~alignmentMask;
            fseek(ctx->imageStream, alignedPosition, SEEK_SET);
            indexPosition = alignedPosition;
            TRACE("Aligned index position to %" PRIu64, alignedPosition);
        }

        // Prepare index header
        IndexHeader3 indexHeader;
        indexHeader.identifier = IndexBlock3;
        indexHeader.entries    = utarray_len(ctx->indexEntries);
        indexHeader.previous   = 0;  // No previous index for now

        TRACE("Writing index with %" PRIu64 " entries at position %ld", indexHeader.entries, indexPosition);

        // Calculate CRC64 of index entries
        crc64_ctx *indexCrc64Context = aaruf_crc64_init();
        if(indexCrc64Context != NULL && indexHeader.entries > 0)
        {
            size_t indexDataSize = indexHeader.entries * sizeof(IndexEntry);
            aaruf_crc64_update(indexCrc64Context, (uint8_t *)utarray_front(ctx->indexEntries), indexDataSize);
            aaruf_crc64_final(indexCrc64Context, &indexHeader.crc64);
            TRACE("Calculated index CRC64: 0x%16lX", indexHeader.crc64);
        }
        else { indexHeader.crc64 = 0; }

        // Write index header
        if(fwrite(&indexHeader, sizeof(IndexHeader3), 1, ctx->imageStream) == 1)
        {
            TRACE("Successfully wrote index header");

            // Write index entries
            if(indexHeader.entries > 0)
            {
                size_t      entriesWritten = 0;
                IndexEntry *entry          = NULL;

                for(entry = (IndexEntry *)utarray_front(ctx->indexEntries); entry != NULL;
                    entry = (IndexEntry *)utarray_next(ctx->indexEntries, entry))
                {
                    if(fwrite(entry, sizeof(IndexEntry), 1, ctx->imageStream) == 1)
                    {
                        entriesWritten++;
                        TRACE("Wrote index entry: blockType=0x%08X dataType=%u offset=%" PRIu64, entry->blockType,
                              entry->dataType, entry->offset);
                    }
                    else
                    {
                        TRACE("Failed to write index entry %zu", entriesWritten);
                        break;
                    }
                }

                if(entriesWritten == indexHeader.entries)
                {
                    TRACE("Successfully wrote all %zu index entries", entriesWritten);

                    // Update header with index offset and rewrite it
                    ctx->header.indexOffset = indexPosition;
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
                    TRACE("Failed to write all index entries (wrote %zu of %" PRIu64 ")", entriesWritten,
                          indexHeader.entries);
                    return AARUF_ERROR_CANNOT_WRITE_HEADER;
                }
            }
        }
        else
        {
            TRACE("Failed to write index header");
            return AARUF_ERROR_CANNOT_WRITE_HEADER;
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
    if(ctx->mediaTags != NULL) HASH_ITER(hh, ctx->mediaTags, mediaTag, tmpMediaTag)
        {
            HASH_DEL(ctx->mediaTags, mediaTag);
            free(mediaTag->data);
            free(mediaTag);
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
