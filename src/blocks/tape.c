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

/**
 * @brief Retrieves the block range for a specific tape file from an Aaru tape image.
 *
 * Queries the tape file hash table to locate a file by its partition and file number,
 * returning the first and last block addresses that define the file's extent on the
 * tape medium. This function provides the core lookup mechanism for accessing logical
 * files stored in tape images.
 *
 * **Tape File Identification:**
 * Each tape file is uniquely identified by a combination of:
 * - **Partition number** (8-bit): The tape partition containing the file
 * - **File number** (32-bit): The sequential file number within that partition
 *
 * These two values are combined into a 64-bit composite key:
 *   key = (partition << 32) | file_number
 *
 * This composite key is used to perform a hash table lookup in the context's
 * tapeFiles table, which was previously populated by process_tape_files_block()
 * during image initialization.
 *
 * **Block Range Semantics:**
 * The returned block range [FirstBlock, LastBlock] is inclusive on both ends:
 * - FirstBlock: The first block address where the file begins
 * - LastBlock: The final block address where the file ends (inclusive)
 * - Block count: (LastBlock - FirstBlock + 1)
 *
 * Block addresses are absolute positions within the tape image's logical
 * block space, not relative to the partition or file.
 *
 * **Typical Usage Flow:**
 * 1. Open an Aaru tape image with aaruf_open()
 * 2. Call aaruf_get_tape_file() to get the block range for a specific file
 * 3. Use the returned block range to read the file's data blocks
 * 4. Repeat for other files as needed
 *
 * **Error Handling:**
 * The function performs validation in the following order:
 * 1. Context pointer validation (NULL check)
 * 2. Magic number verification (ensures valid aaruformat context)
 * 3. Hash table lookup for the specified partition/file combination
 *
 * If any validation fails, an appropriate error code is returned and the
 * output parameters (starting_block, ending_block) are left unmodified.
 *
 * **Thread Safety:**
 * This function performs read-only operations on the context and is safe
 * to call from multiple threads concurrently, provided the context is not
 * being modified by other operations (e.g., during image opening/closing).
 *
 * **Performance Characteristics:**
 * - Hash table lookup: O(1) average case
 * - No I/O operations performed
 * - Minimal stack usage
 * - Suitable for high-frequency queries
 *
 * @param context Pointer to an initialized aaruformat context. Must not be NULL.
 *                The context must have been successfully opened with aaruf_open()
 *                and contain a valid tape file hash table. The context is treated
 *                as const and is not modified by this operation.
 *
 * @param partition The partition number (0-255) containing the requested file.
 *                  For single-partition tapes, this is typically 0. Multi-partition
 *                  tapes may have files in different partitions with potentially
 *                  overlapping file numbers.
 *
 * @param file The file number within the specified partition. File numbers are
 *             typically sequential starting from 0 or 1, but gaps may exist if
 *             files were deleted or the tape was written non-sequentially.
 *
 * @param[out] starting_block Pointer to receive the first block address of the file.
 *                            Must not be NULL. Only modified on success.
 *                            The value written represents the inclusive start of the
 *                            file's block range.
 *
 * @param[out] ending_block Pointer to receive the last block address of the file.
 *                          Must not be NULL. Only modified on success.
 *                          The value written represents the inclusive end of the
 *                          file's block range.
 *
 * @retval AARUF_STATUS_OK (0) Successfully retrieved tape file information. Both
 *         output parameters have been populated with valid block addresses. The
 *         requested partition/file combination exists in the image's file table.
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) Invalid context or context validation failed.
 *         This is returned when:
 *         - The context pointer is NULL
 *         - The context magic number doesn't match AARU_MAGIC (corrupted or wrong type)
 *         The output parameters are not modified.
 *
 * @retval AARUF_ERROR_TAPE_FILE_NOT_FOUND (-28) The requested partition/file combination
 *         does not exist in the image's tape file table. This is returned when:
 *         - The specified partition number has no files
 *         - The specified file number doesn't exist in the given partition
 *         - The tape file block was not present or failed to load during image open
 *         The output parameters are not modified.
 *
 * @note The function logs entry and exit points via TRACE macros when tracing is
 *       enabled, including parameter values and return codes for debugging.
 *
 * @note The tape file hash table (ctx->tapeFiles) must have been populated during
 *       image initialization. If the image doesn't contain a TapeFileBlock, or if
 *       that block failed to load, all queries will return AARUF_ERROR_TAPE_FILE_NOT_FOUND.
 *
 * @note For images without tape file metadata, applications should fall back to
 *       direct block-based access or partition-level operations.
 *
 * @note The returned block addresses are logical block numbers. To read actual data,
 *       these must be translated through the appropriate read functions that handle
 *       the image's block encoding, compression, and DDT mapping.
 *
 * @warning The output parameter pointers must be valid. Passing NULL for either
 *          starting_block or ending_block will cause undefined behavior (likely
 *          a crash when the function attempts to dereference them on success).
 *
 * @warning If the same partition/file combination appears multiple times in the
 *          TapeFileBlock, only the last occurrence is retained (due to HASH_REPLACE
 *          behavior in process_tape_files_block). This should not occur in valid
 *          images but may happen with corrupted or malformed tape file metadata.
 *
 * @see process_tape_files_block() for tape file table initialization
 * @see TapeFileEntry for the structure defining file block ranges
 * @see tapeFileHashEntry for the hash table entry structure
 * @see aaruf_get_tape_partition() for partition-level queries (if available)
 */
int32_t aaruf_get_tape_file(const void *context, const uint8_t partition, const uint32_t file, uint64_t *starting_block,
                            uint64_t *ending_block)
{
    TRACE("Entering aaruf_get_tape_file(%p, %d, %d, %llu, %llu)", context, partition, file, *starting_block,
          *ending_block);

    const aaruformatContext *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tape_file() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tape_file() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    uint64_t           key   = (uint64_t)partition << 32 | file;
    tapeFileHashEntry *entry = NULL;
    HASH_FIND(hh, ctx->tapeFiles, &key, sizeof(uint64_t), entry);

    if(entry == NULL)
    {
        TRACE("Tape file not found");
        return AARUF_ERROR_TAPE_FILE_NOT_FOUND;
    }

    *starting_block = entry->fileEntry.FirstBlock;
    *ending_block   = entry->fileEntry.LastBlock;

    TRACE("Exiting aaruf_get_tape_file(%p, %d, %d, %llu, %llu) = AARUF_STATUS_OK", context, partition, file,
          *starting_block, *ending_block);
    return AARUF_STATUS_OK;
}