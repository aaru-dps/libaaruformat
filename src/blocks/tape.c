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
 * */

#include "aaruformat.h"
#include "log.h"

/**
 * @brief Processes a tape file metadata block from the image stream.
 *
 * Reads and parses a TapeFileBlock from the Aaru image, validates its integrity,
 * and populates the context's tape file hash table with file layout information.
 * Each tape file entry defines a logical file on the tape medium by specifying
 * its partition, file number, and block range (FirstBlock to LastBlock inclusive).
 *
 * The function performs the following operations:
 * 1. Seeks to the block position indicated by the index entry
 * 2. Reads and validates the TapeFileHeader structure
 * 3. Allocates and reads the array of TapeFileEntry structures
 * 4. Validates data integrity using CRC64-ECMA checksum
 * 5. Inserts each file entry into the context's UTHASH table with a composite key
 * 6. Updates image size statistics
 *
 * **Composite Key Construction:**
 * Each tape file is uniquely identified by a 64-bit composite key:
 *   key = (partition << 32) | file_number
 * This allows files with the same file number in different partitions to coexist
 * in the hash table without conflicts.
 *
 * **Hash Table Management:**
 * The function uses HASH_REPLACE to insert entries, which automatically:
 * - Adds new entries if the key doesn't exist
 * - Replaces existing entries if the key is found (freeing the old entry)
 * This ensures that duplicate entries (same partition/file combination) are
 * properly handled by keeping only the most recent definition.
 *
 * **Error Handling:**
 * The function treats most errors as non-fatal and continues processing:
 * - Invalid context or stream: Returns immediately (FATAL log)
 * - Seek failures: Returns immediately (FATAL log)
 * - Header read failures: Returns early (TRACE log)
 * - Incorrect block identifier: Logs warning but continues
 * - Memory allocation failures: Logs error and returns
 * - Entry read failures: Frees buffer and returns
 * - CRC64 mismatch: Logs warning, frees buffer, and returns
 * - Per-entry allocation failures: Logs error and skips that entry
 *
 * **Block Structure:**
 * The tape file block consists of:
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
 * **CRC64 Validation:**
 * The CRC64 checksum in the header is computed over the entire array of
 * TapeFileEntry structures (excluding the header itself). This provides
 * integrity verification to detect corruption in the file table.
 *
 * **Memory Management:**
 * - Allocates a temporary buffer to read all file entries
 * - Allocates individual hash table entries for each file
 * - Frees the temporary buffer before returning
 * - Frees replaced hash entries automatically
 * - Hash table entries remain in context until cleanup
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL.
 *            The context must have a valid imageStream open for reading.
 *            The ctx->tapeFiles hash table will be populated with file entries.
 *            The ctx->imageInfo.ImageSize will be updated with the block size.
 *
 * @param entry Pointer to the index entry describing the tape file block.
 *              Must not be NULL. The entry->offset field indicates the file
 *              position where the TapeFileHeader begins.
 *
 * @note This function does not return a status code. All errors are handled
 *       internally with appropriate logging and the function returns early
 *       on fatal errors.
 *
 * @note The tape file hash table (ctx->tapeFiles) must be initialized to NULL
 *       before the first call to this function. UTHASH will manage the table
 *       automatically as entries are added.
 *
 * @note Files are ordered in the hash table by their composite key value, not
 *       by insertion order. To iterate files in partition/file number order,
 *       use HASH_SORT with an appropriate comparison function.
 *
 * @note The function updates ctx->imageInfo.ImageSize by adding the size of
 *       all file entries (entries × sizeof(TapeFileEntry)). This contributes
 *       to the total reported image size but does not include the header size.
 *
 * @warning The context and imageStream must be valid. Passing NULL pointers
 *          will result in immediate return with a FATAL log message.
 *
 * @warning If the CRC64 checksum validation fails, all entries in the block
 *          are discarded. The function does not attempt partial recovery.
 *
 * @warning If memory allocation fails for a hash entry, that specific file
 *          entry is skipped but processing continues with remaining entries.
 *
 * @see TapeFileHeader for the block header structure
 * @see TapeFileEntry for individual file entry structure
 * @see tapeFileHashEntry for the hash table entry structure
 * @see process_tape_partition_block() for partition metadata processing
 */
void process_tape_files_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    long           pos              = 0;
    size_t         read_bytes       = 0;
    TapeFileHeader tape_file_header = {0};

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before
    read_bytes = fread(&tape_file_header, 1, sizeof(TapeFileHeader), ctx->imageStream);

    if(read_bytes != sizeof(TapeFileHeader))
    {
        TRACE("Could not read tape files header, continuing...\n");
        return;
    }

    if(tape_file_header.identifier != TapeFileBlock)
        TRACE("Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);

    ctx->imageInfo.ImageSize += sizeof(TapeFileEntry) * tape_file_header.entries;

    uint8_t *buffer = malloc(sizeof(TapeFileEntry) * tape_file_header.entries);
    if(buffer == NULL)
    {
        FATAL("Could not allocate memory for tape files block, continuing...\n");
        return;
    }
    read_bytes = fread(buffer, sizeof(TapeFileEntry), tape_file_header.entries, ctx->imageStream);
    if(read_bytes != tape_file_header.entries)
    {
        free(buffer);
        FATAL("Could not read tape files block, continuing...\n");
        return;
    }
    // Check CRC64
    uint64_t crc64 = aaruf_crc64_data(buffer, sizeof(TapeFileEntry) * tape_file_header.entries);

    if(crc64 != tape_file_header.crc64)
    {
        TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...\n", crc64,
              tape_file_header.crc64);
        free(buffer);
        return;
    }

    // Insert entries into UTHASH array indexed by partition << 32 | file number
    const TapeFileEntry *entries = (TapeFileEntry *)buffer;

    for(uint32_t i = 0; i < tape_file_header.entries; i++)
    {
        // Create hash table entry
        tapeFileHashEntry *hash_entry = malloc(sizeof(tapeFileHashEntry));
        if(hash_entry == NULL)
        {
            FATAL("Could not allocate memory for tape file hash entry\n");
            continue;
        }

        // Create composite key: partition << 32 | file number
        hash_entry->key = (uint64_t)entries[i].Partition << 32 | entries[i].File;

        // Copy the tape file entry data
        hash_entry->fileEntry = entries[i];

        // Replace if exists, add if new
        tapeFileHashEntry *old_entry = NULL;
        HASH_REPLACE(hh, ctx->tapeFiles, key, sizeof(uint64_t), hash_entry, old_entry);

        // Free old entry if it was replaced
        if(old_entry != NULL)
        {
            TRACE("Replaced existing tape file entry for partition %u, file %u\n", entries[i].Partition,
                  entries[i].File);
            free(old_entry);
        }
        else
            TRACE("Added new tape file entry for partition %u, file %u\n", entries[i].Partition, entries[i].File);
    }

    free(buffer);
}