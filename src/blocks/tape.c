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
 * */

#include "aaruformat.h"
#include "internal.h"
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
void process_tape_files_block(aaruformat_context *ctx, const IndexEntry *entry)
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
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before
    read_bytes = fread(&tape_file_header, 1, sizeof(TapeFileHeader), ctx->imageStream);

    if(read_bytes != sizeof(TapeFileHeader))
    {
        TRACE("Could not read tape files header, continuing...");
        return;
    }

    if(tape_file_header.identifier != TapeFileBlock)
    {
        TRACE("Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
        return;
    }

    ctx->image_info.ImageSize += sizeof(TapeFileEntry) * tape_file_header.entries;

    uint8_t *buffer = malloc(sizeof(TapeFileEntry) * tape_file_header.entries);
    if(buffer == NULL)
    {
        FATAL("Could not allocate memory for tape files block, continuing...");
        return;
    }
    read_bytes = fread(buffer, sizeof(TapeFileEntry), tape_file_header.entries, ctx->imageStream);
    if(read_bytes != tape_file_header.entries)
    {
        free(buffer);
        FATAL("Could not read tape files block, continuing...");
        return;
    }
    // Check CRC64
    uint64_t crc64 = aaruf_crc64_data(buffer, sizeof(TapeFileEntry) * tape_file_header.entries);

    if(crc64 != tape_file_header.crc64)
    {
        TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...", crc64,
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
            FATAL("Could not allocate memory for tape file hash entry");
            continue;
        }

        // Create composite key: partition << 32 | file number
        hash_entry->key = (uint64_t)entries[i].Partition << 32 | entries[i].File;

        // Copy the tape file entry data
        hash_entry->fileEntry = entries[i];

        // Replace if exists, add if new
        tapeFileHashEntry *old_entry = NULL;
        HASH_REPLACE(hh, ctx->tape_files, key, sizeof(uint64_t), hash_entry, old_entry);
        ctx->dirty_tape_file_block = true;  // Mark tape file block as dirty

        // Free old entry if it was replaced
        if(old_entry != NULL)
        {
            TRACE("Replaced existing tape file entry for partition %u, file %u", entries[i].Partition, entries[i].File);
            free(old_entry);
        }
        else
            TRACE("Added new tape file entry for partition %u, file %u", entries[i].Partition, entries[i].File);
    }

    free(buffer);
}

/**
 * @brief Processes a tape partition metadata block from the image stream.
 *
 * Reads and parses a TapePartitionBlock from the Aaru image, validates its integrity,
 * and populates the context's tape partition hash table with partition layout information.
 * Each tape partition entry defines a physical division of the tape medium by specifying
 * its partition number and block range (FirstBlock to LastBlock inclusive).
 *
 * The function performs the following operations:
 * 1. Seeks to the block position indicated by the index entry
 * 2. Reads and validates the TapePartitionHeader structure
 * 3. Allocates and reads the array of TapePartitionEntry structures
 * 4. Validates data integrity using CRC64-ECMA checksum
 * 5. Inserts each partition entry into the context's UTHASH table with partition number as key
 * 6. Updates image size statistics
 *
 * **Partition Identification:**
 * Each tape partition is uniquely identified by its partition number (0-255).
 * This number serves as the hash table key for fast lookup operations. Most tapes
 * have a single partition (partition 0), but multi-partition formats like LTO, DLT,
 * and AIT support multiple partitions with independent block address spaces.
 *
 * **Hash Table Management:**
 * The function uses HASH_REPLACE to insert entries, which automatically:
 * - Adds new entries if the partition number doesn't exist
 * - Replaces existing entries if the partition number is found (freeing the old entry)
 * This ensures that duplicate partition definitions are properly handled by keeping
 * only the most recent definition.
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
 * The tape partition block consists of:
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
 * **CRC64 Validation:**
 * The CRC64 checksum in the header is computed over the entire array of
 * TapePartitionEntry structures (excluding the header itself). This provides
 * integrity verification to detect corruption in the partition table.
 *
 * **Partition Block Ranges:**
 * Each partition defines a block address space:
 * - FirstBlock: Starting block address (often 0, but format-dependent)
 * - LastBlock: Ending block address (inclusive)
 * - Block count: (LastBlock - FirstBlock + 1)
 *
 * Block addresses are local to each partition. Different partitions may have
 * overlapping logical block numbers (e.g., both partition 0 and partition 1
 * can have blocks numbered 0-1000).
 *
 * **Memory Management:**
 * - Allocates a temporary buffer to read all partition entries
 * - Allocates individual hash table entries for each partition
 * - Frees the temporary buffer before returning
 * - Frees replaced hash entries automatically
 * - Hash table entries remain in context until cleanup
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL.
 *            The context must have a valid imageStream open for reading.
 *            The ctx->tapePartitions hash table will be populated with partition entries.
 *            The ctx->imageInfo.ImageSize will be updated with the block size.
 *
 * @param entry Pointer to the index entry describing the tape partition block.
 *              Must not be NULL. The entry->offset field indicates the file
 *              position where the TapePartitionHeader begins.
 *
 * @note This function does not return a status code. All errors are handled
 *       internally with appropriate logging and the function returns early
 *       on fatal errors.
 *
 * @note The tape partition hash table (ctx->tapePartitions) must be initialized to NULL
 *       before the first call to this function. UTHASH will manage the table
 *       automatically as entries are added.
 *
 * @note Partitions are ordered in the hash table by their partition number, not
 *       by insertion order. To iterate partitions in numerical order, use HASH_SORT
 *       with an appropriate comparison function.
 *
 * @note The function updates ctx->imageInfo.ImageSize by adding the size of
 *       all partition entries (entries × sizeof(TapePartitionEntry)). This contributes
 *       to the total reported image size but does not include the header size.
 *
 * @note The partition metadata is essential for correctly interpreting tape file
 *       locations, as files reference partition numbers in their definitions.
 *       Without partition metadata, tape file block ranges may be ambiguous.
 *
 * @warning The context and imageStream must be valid. Passing NULL pointers
 *          will result in immediate return with a FATAL log message.
 *
 * @warning If the CRC64 checksum validation fails, all entries in the block
 *          are discarded. The function does not attempt partial recovery.
 *
 * @warning If memory allocation fails for a hash entry, that specific partition
 *          entry is skipped but processing continues with remaining entries.
 *
 * @warning If multiple partition entries have the same Number field, only the
 *          last occurrence is retained. This should not occur in valid images.
 *
 * @see TapePartitionHeader for the block header structure
 * @see TapePartitionEntry for individual partition entry structure
 * @see TapePartitionHashEntry for the hash table entry structure
 * @see process_tape_files_block() for tape file metadata processing
 */
void process_tape_partitions_block(aaruformat_context *ctx, const IndexEntry *entry)
{
    long                pos                   = 0;
    size_t              read_bytes            = 0;
    TapePartitionHeader tape_partition_header = {0};

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
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before
    read_bytes = fread(&tape_partition_header, 1, sizeof(TapePartitionHeader), ctx->imageStream);

    if(read_bytes != sizeof(TapePartitionHeader))
    {
        TRACE("Could not read tape partitions header, continuing...");
        return;
    }

    if(tape_partition_header.identifier != TapePartitionBlock)
    {
        TRACE("Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
        return;
    }

    ctx->image_info.ImageSize += sizeof(TapePartitionEntry) * tape_partition_header.entries;

    uint8_t *buffer = malloc(sizeof(TapePartitionEntry) * tape_partition_header.entries);
    if(buffer == NULL)
    {
        FATAL("Could not allocate memory for tape partitions block, continuing...");
        return;
    }
    read_bytes = fread(buffer, sizeof(TapePartitionEntry), tape_partition_header.entries, ctx->imageStream);
    if(read_bytes != tape_partition_header.entries)
    {
        free(buffer);
        FATAL("Could not read tape partitions block, continuing...");
        return;
    }
    // Check CRC64
    uint64_t crc64 = aaruf_crc64_data(buffer, sizeof(TapePartitionEntry) * tape_partition_header.entries);

    if(crc64 != tape_partition_header.crc64)
    {
        TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...", crc64,
              tape_partition_header.crc64);
        free(buffer);
        return;
    }

    // Insert entries into UTHASH array indexed by partition
    const TapePartitionEntry *entries = (TapePartitionEntry *)buffer;

    for(uint32_t i = 0; i < tape_partition_header.entries; i++)
    {
        // Create hash table entry
        TapePartitionHashEntry *hash_entry = malloc(sizeof(TapePartitionHashEntry));
        if(hash_entry == NULL)
        {
            FATAL("Could not allocate memory for tape partition hash entry");
            continue;
        }

        // Create key: partition
        hash_entry->key = entries[i].Number;

        // Copy the tape partition entry data
        hash_entry->partitionEntry = entries[i];

        // Replace if exists, add if new
        TapePartitionHashEntry *old_entry = NULL;
        HASH_REPLACE(hh, ctx->tape_partitions, key, sizeof(uint8_t), hash_entry, old_entry);
        ctx->dirty_tape_partition_block = true;  // Mark tape partition block as dirty

        // Free old entry if it was replaced
        if(old_entry != NULL)
        {
            TRACE("Replaced existing tape partition entry for partition %u", entries[i].Number);
            free(old_entry);
        }
        else
            TRACE("Added new tape partition entry for partition %u", entries[i].Number);
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
AARU_EXPORT int32_t AARU_CALL aaruf_get_tape_file(const void *context, const uint8_t partition, const uint32_t file,
                                                  uint64_t *starting_block, uint64_t *ending_block)
{
    TRACE("Entering aaruf_get_tape_file(%p, %d, %d, %llu, %llu)", context, partition, file, *starting_block,
          *ending_block);

    const aaruformat_context *ctx = NULL;

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
    HASH_FIND(hh, ctx->tape_files, &key, sizeof(uint64_t), entry);

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

/**
 * @brief Sets or updates the block range for a specific tape file in an Aaru tape image.
 *
 * Creates or modifies a tape file entry in the context's hash table, defining the logical
 * file's extent on the tape medium by its partition number, file number, and block range.
 * This function is the write-mode counterpart to aaruf_get_tape_file() and is used when
 * creating or modifying tape images to establish the file structure metadata.
 *
 * **Tape File Registration:**
 * When writing a tape image, this function should be called for each logical file to
 * register its location. Each file is uniquely identified by:
 * - **Partition number** (8-bit): The tape partition containing the file
 * - **File number** (32-bit): The sequential file number within that partition
 *
 * These values are combined into a 64-bit composite key:
 *   key = (partition << 32) | file_number
 *
 * The file entry (including the block range) is then stored in the context's tapeFiles
 * hash table and will be written to the image's TapeFileBlock during finalization.
 *
 * **Block Range Definition:**
 * The block range [starting_block, ending_block] defines the file's extent:
 * - starting_block: The first block address where the file begins (inclusive)
 * - ending_block: The final block address where the file ends (inclusive)
 * - Block count: (ending_block - starting_block + 1)
 *
 * Block addresses must be absolute positions within the tape image's logical
 * block space. The caller is responsible for ensuring:
 * - starting_block <= ending_block (no validation is performed)
 * - Block ranges don't conflict with other files (no validation is performed)
 * - All blocks in the range have been or will be written to the image
 *
 * **Typical Usage Flow:**
 * 1. Open or create an Aaru tape image with write access
 * 2. Write the file's data blocks to the image
 * 3. Call aaruf_set_tape_file() to register the file's block range
 * 4. Repeat for all files on the tape
 * 5. Close the image (TapeFileBlock will be written during finalization)
 *
 * **Update/Replace Behavior:**
 * If a file entry with the same partition/file combination already exists:
 * - The old entry is automatically freed
 * - The new entry replaces it in the hash table
 * - A TRACE message indicates the replacement
 *
 * This allows updating file metadata or correcting errors without manual deletion.
 *
 * **Error Handling:**
 * The function performs validation in the following order:
 * 1. Context pointer validation (NULL check)
 * 2. Magic number verification (ensures valid aaruformat context)
 * 3. Write mode verification (ensures image is opened for writing)
 * 4. Memory allocation for the hash entry
 *
 * If any validation fails, an appropriate error code is returned and no
 * modifications are made to the context's tape file table.
 *
 * **Thread Safety:**
 * This function modifies the shared context's hash table and is NOT thread-safe.
 * Concurrent calls to aaruf_set_tape_file() or other functions that modify
 * ctx->tapeFiles may result in data corruption or memory leaks. The caller must
 * ensure exclusive access through external synchronization if needed.
 *
 * **Memory Management:**
 * - Allocates a new hash table entry (tapeFileHashEntry) for each call
 * - HASH_REPLACE automatically frees replaced entries
 * - All hash entries are freed when the context is closed
 * - On allocation failure, no entry is added and an error is returned
 *
 * **Performance Characteristics:**
 * - Hash table insertion/replacement: O(1) average case
 * - No I/O operations performed (metadata written during image close)
 * - Minimal stack usage
 * - Suitable for bulk file registration operations
 *
 * @param context Pointer to an initialized aaruformat context. Must not be NULL.
 *                The context must have been opened with write access (isWriting=true).
 *                The context's tapeFiles hash table will be updated with the new entry.
 *
 * @param partition The partition number (0-255) where the file is located.
 *                  For single-partition tapes, this is typically 0. Multi-partition
 *                  tapes can have files in different partitions with potentially
 *                  overlapping file numbers.
 *
 * @param file The file number within the specified partition. File numbers are
 *             typically sequential (0, 1, 2, ...) but gaps are allowed. The same
 *             file number can exist in different partitions without conflict.
 *
 * @param starting_block The first block address of the file (inclusive). This should
 *                       be the absolute block number in the image where the file's
 *                       first byte begins. Must be <= ending_block (not validated).
 *
 * @param ending_block The last block address of the file (inclusive). This should
 *                     be the absolute block number in the image where the file's
 *                     last byte ends. Must be >= starting_block (not validated).
 *
 * @retval AARUF_STATUS_OK (0) Successfully set tape file information. The hash table
 *         has been updated with the file entry. If an entry with the same partition/file
 *         combination existed, it has been replaced. The metadata will be written to
 *         the TapeFileBlock when the image is closed.
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) Invalid context or context validation failed.
 *         This is returned when:
 *         - The context pointer is NULL
 *         - The context magic number doesn't match AARU_MAGIC (corrupted or wrong type)
 *         No modifications are made to the context.
 *
 * @retval AARUF_READ_ONLY (-22) The context is not opened for writing. This is returned
 *         when ctx->isWriting is false, indicating the image was opened in read-only mode.
 *         Tape file metadata cannot be modified in read-only mode. No modifications are
 *         made to the context.
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. The system could
 *         not allocate memory for the hash table entry (tapeFileHashEntry). This is a
 *         critical error indicating low memory conditions. No modifications are made to
 *         the context.
 *
 * @note The function logs entry and exit points via TRACE macros when tracing is
 *       enabled, including parameter values and return codes for debugging.
 *
 * @note The tape file metadata is not immediately written to disk. It remains in the
 *       context's hash table until the image is closed, at which point all entries
 *       are serialized and written to a TapeFileBlock.
 *
 * @note No validation is performed on the block range values. The caller is responsible
 *       for ensuring that starting_block <= ending_block and that the range is valid
 *       for the image being created.
 *
 * @note No validation is performed to detect overlapping file ranges. Multiple files
 *       can reference the same or overlapping block ranges, which may be intentional
 *       (e.g., for multi-track or multi-partition scenarios).
 *
 * @note If the same partition/file combination is set multiple times, only the last
 *       values are retained. This can be used to update file metadata but also means
 *       accidental duplicate calls will silently overwrite previous values.
 *
 * @warning This function is NOT thread-safe. Concurrent modifications to the tape file
 *          table may result in undefined behavior, memory corruption, or memory leaks.
 *
 * @warning The caller must ensure the image is opened with write access before calling
 *          this function. Attempting to modify read-only images will fail with
 *          AARUF_READ_ONLY.
 *
 * @warning Parameter validation is minimal. Invalid block ranges (starting_block >
 *          ending_block) are accepted and will be written to the image, potentially
 *          causing problems when reading the image later.
 *
 * @warning If memory allocation fails (AARUF_ERROR_NOT_ENOUGH_MEMORY), the file entry
 *          is not added. The caller should handle this error appropriately, potentially
 *          by freeing memory and retrying or aborting the write operation.
 *
 * @see aaruf_get_tape_file() for retrieving tape file information from images
 * @see process_tape_files_block() for tape file table initialization during read
 * @see TapeFileEntry for the structure defining file block ranges
 * @see tapeFileHashEntry for the hash table entry structure
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_tape_file(void *context, const uint8_t partition, const uint32_t file,
                                                  const uint64_t starting_block, const uint64_t ending_block)
{
    TRACE("Entering aaruf_set_tape_file(%p, %d, %d, %llu, %llu)", context, partition, file, starting_block,
          ending_block);

    aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_tape_file() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_tape_file() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_tape_file() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Create hash table entry
    tapeFileHashEntry *hash_entry = malloc(sizeof(tapeFileHashEntry));
    if(hash_entry == NULL)
    {
        FATAL("Could not allocate memory for tape file hash entry");

        TRACE("Exiting aaruf_set_tape_file() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Create composite key: partition << 32 | file number
    hash_entry->key = (uint64_t)partition << 32 | file;

    // Copy the tape file entry data
    hash_entry->fileEntry.File       = file;
    hash_entry->fileEntry.Partition  = partition;
    hash_entry->fileEntry.FirstBlock = starting_block;
    hash_entry->fileEntry.LastBlock  = ending_block;

    // Replace if exists, add if new
    tapeFileHashEntry *old_entry = NULL;
    HASH_REPLACE(hh, ctx->tape_files, key, sizeof(uint64_t), hash_entry, old_entry);
    ctx->dirty_tape_file_block = true;  // Mark tape file block as dirty

    // Free old entry if it was replaced
    if(old_entry != NULL)
    {
        TRACE("Replaced existing tape file entry for partition %u, file %u", partition, file);
        free(old_entry);
    }
    else
        TRACE("Added new tape file entry for partition %u, file %u", partition, file);

    TRACE("Exiting aaruf_set_tape_file(%p, %d, %d, %llu, %llu) = AARUF_STATUS_OK", context, partition, file,
          starting_block, ending_block);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the block range for a specific tape partition from an Aaru tape image.
 *
 * Queries the tape partition hash table to locate a partition by its partition number,
 * returning the first and last block addresses that define the partition's extent on the
 * tape medium. This function provides the core lookup mechanism for accessing partition
 * layout information in tape images.
 *
 * **Tape Partition Identification:**
 * Each tape partition is uniquely identified by its partition number (0-255). Most tapes
 * have a single partition (partition 0), but multi-partition formats like LTO, DLT, and
 * AIT support multiple partitions with independent block address spaces.
 *
 * The partition number is used as the hash table key to perform a lookup in the context's
 * tapePartitions table, which was previously populated by process_tape_partitions_block()
 * during image initialization.
 *
 * **Block Range Semantics:**
 * The returned block range [FirstBlock, LastBlock] is inclusive on both ends:
 * - FirstBlock: The first block address in the partition (often 0, but format-dependent)
 * - LastBlock: The final block address in the partition (inclusive)
 * - Block count: (LastBlock - FirstBlock + 1)
 *
 * Block addresses are local to each partition. Different partitions may have overlapping
 * logical block numbers (e.g., both partition 0 and partition 1 can have blocks 0-1000).
 * When accessing blocks, both the partition number and block number are required for
 * unique identification.
 *
 * **Typical Usage Flow:**
 * 1. Open an Aaru tape image with aaruf_open()
 * 2. Call aaruf_get_tape_partition() to get the block range for a specific partition
 * 3. Use the returned block range to understand partition boundaries
 * 4. Access files within the partition using aaruf_get_tape_file()
 * 5. Repeat for other partitions as needed
 *
 * **Error Handling:**
 * The function performs validation in the following order:
 * 1. Context pointer validation (NULL check)
 * 2. Magic number verification (ensures valid aaruformat context)
 * 3. Hash table lookup for the specified partition number
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
 * **Partition Layout Information:**
 * The partition metadata is essential for:
 * - Understanding the physical organization of the tape
 * - Determining partition boundaries for file access
 * - Validating that file block ranges fall within partition limits
 * - Supporting multi-partition tape formats correctly
 * - Preserving original tape partitioning schemes
 *
 * @param context Pointer to an initialized aaruformat context. Must not be NULL.
 *                The context must have been successfully opened with aaruf_open()
 *                and contain a valid tape partition hash table. The context is treated
 *                as const and is not modified by this operation.
 *
 * @param partition The partition number (0-255) to query. For single-partition tapes,
 *                  this is typically 0. Multi-partition tapes may have multiple
 *                  partitions numbered sequentially from 0.
 *
 * @param[out] starting_block Pointer to receive the first block address of the partition.
 *                            Must not be NULL. Only modified on success.
 *                            The value written represents the inclusive start of the
 *                            partition's block range (often 0, but format-dependent).
 *
 * @param[out] ending_block Pointer to receive the last block address of the partition.
 *                          Must not be NULL. Only modified on success.
 *                          The value written represents the inclusive end of the
 *                          partition's block range.
 *
 * @retval AARUF_STATUS_OK (0) Successfully retrieved tape partition information. Both
 *         output parameters have been populated with valid block addresses. The
 *         requested partition exists in the image's partition table.
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) Invalid context or context validation failed.
 *         This is returned when:
 *         - The context pointer is NULL
 *         - The context magic number doesn't match AARU_MAGIC (corrupted or wrong type)
 *         The output parameters are not modified.
 *
 * @retval AARUF_ERROR_TAPE_PARTITION_NOT_FOUND (-29) The requested partition number
 *         does not exist in the image's tape partition table. This is returned when:
 *         - The specified partition number was not defined in the TapePartitionBlock
 *         - The tape partition block was not present or failed to load during image open
 *         - The partition number is out of range for this tape
 *         The output parameters are not modified.
 *
 * @note The function logs entry and exit points via TRACE macros when tracing is
 *       enabled, including parameter values and return codes for debugging.
 *
 * @note The tape partition hash table (ctx->tapePartitions) must have been populated
 *       during image initialization. If the image doesn't contain a TapePartitionBlock,
 *       or if that block failed to load, all queries will return
 *       AARUF_ERROR_TAPE_PARTITION_NOT_FOUND.
 *
 * @note For images without tape partition metadata, the entire tape may be treated
 *       as a single implicit partition, and applications should handle the absence
 *       of partition information gracefully.
 *
 * @note The returned block addresses are logical block numbers within the partition's
 *       address space. To read actual data, these must be combined with the partition
 *       number and translated through the appropriate read functions.
 *
 * @note Partition metadata is primarily informational and used for validation. File
 *       access is primarily driven by file metadata (TapeFileBlock), which references
 *       partition numbers to establish context.
 *
 * @warning The output parameter pointers must be valid. Passing NULL for either
 *          starting_block or ending_block will cause undefined behavior (likely
 *          a crash when the function attempts to dereference them on success).
 *
 * @warning If the same partition number appears multiple times in the TapePartitionBlock,
 *          only the last occurrence is retained (due to HASH_REPLACE behavior in
 *          process_tape_partitions_block). This should not occur in valid images but
 *          may happen with corrupted or malformed partition metadata.
 *
 * @warning Single-partition tapes may not include a TapePartitionBlock at all, in which
 *          case this function will always return AARUF_ERROR_TAPE_PARTITION_NOT_FOUND.
 *          Applications should handle this case and assume a default partition 0
 *          spanning the entire tape.
 *
 * @see process_tape_partitions_block() for partition table initialization
 * @see TapePartitionEntry for the structure defining partition block ranges
 * @see TapePartitionHashEntry for the hash table entry structure
 * @see aaruf_get_tape_file() for file-level queries within partitions
 * @see aaruf_set_tape_partition() for setting partition information during write
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_tape_partition(const void *context, const uint8_t partition,
                                                       uint64_t *starting_block, uint64_t *ending_block)
{
    TRACE("Entering aaruf_get_tape_partition(%p, %d, %llu, %llu)", context, partition, *starting_block, *ending_block);

    const aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tape_partition() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tape_partition() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    uint8_t                 key   = partition;
    TapePartitionHashEntry *entry = NULL;
    HASH_FIND(hh, ctx->tape_partitions, &key, sizeof(uint8_t), entry);

    if(entry == NULL)
    {
        TRACE("Tape partition not found");
        return AARUF_ERROR_TAPE_PARTITION_NOT_FOUND;
    }

    *starting_block = entry->partitionEntry.FirstBlock;
    *ending_block   = entry->partitionEntry.LastBlock;

    TRACE("Exiting aaruf_get_tape_partition(%p, %d, %llu, %llu) = AARUF_STATUS_OK", context, partition, *starting_block,
          *ending_block);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets or updates the block range for a specific tape partition in an Aaru tape image.
 *
 * Creates or modifies a tape partition entry in the context's hash table, defining the
 * physical partition's extent on the tape medium by its partition number and block range.
 * This function is the write-mode counterpart to aaruf_get_tape_partition() and is used
 * when creating or modifying tape images to establish the partition structure metadata.
 *
 * **Tape Partition Registration:**
 * When writing a tape image, this function should be called for each physical partition
 * to register its block range. Each partition is uniquely identified by its partition
 * number (0-255), and most tapes have a single partition (partition 0), though formats
 * like LTO, DLT, and AIT support multiple partitions.
 *
 * The partition entry (including the block range) is stored in the context's tapePartitions
 * hash table and will be written to the image's TapePartitionBlock during finalization.
 *
 * **Block Range Definition:**
 * The block range [starting_block, ending_block] defines the partition's extent:
 * - starting_block: The first block address in the partition (often 0, but format-dependent)
 * - ending_block: The final block address in the partition (inclusive)
 * - Block count: (ending_block - starting_block + 1)
 *
 * Block addresses are local to each partition. The caller is responsible for ensuring:
 * - starting_block <= ending_block (no validation is performed)
 * - Partition ranges don't overlap (no validation is performed)
 * - All blocks in the range have been or will be written to the image
 * - Files referencing this partition have block addresses within this range
 *
 * **Typical Usage Flow:**
 * 1. Open or create an Aaru tape image with write access
 * 2. Define partition layout by calling aaruf_set_tape_partition() for each partition
 * 3. Write data blocks to the image within the defined partition ranges
 * 4. Register files within partitions using aaruf_set_tape_file()
 * 5. Close the image (TapePartitionBlock will be written during finalization)
 *
 * **Update/Replace Behavior:**
 * If a partition entry with the same partition number already exists:
 * - The old entry is automatically freed
 * - The new entry replaces it in the hash table
 * - A TRACE message indicates the replacement
 *
 * This allows updating partition metadata or correcting errors without manual deletion.
 *
 * **Error Handling:**
 * The function performs validation in the following order:
 * 1. Context pointer validation (NULL check)
 * 2. Magic number verification (ensures valid aaruformat context)
 * 3. Write mode verification (ensures image is opened for writing)
 * 4. Memory allocation for the hash entry
 *
 * If any validation fails, an appropriate error code is returned and no
 * modifications are made to the context's tape partition table.
 *
 * **Thread Safety:**
 * This function modifies the shared context's hash table and is NOT thread-safe.
 * Concurrent calls to aaruf_set_tape_partition() or other functions that modify
 * ctx->tapePartitions may result in data corruption or memory leaks. The caller must
 * ensure exclusive access through external synchronization if needed.
 *
 * **Memory Management:**
 * - Allocates a new hash table entry (TapePartitionHashEntry) for each call
 * - HASH_REPLACE automatically frees replaced entries
 * - All hash entries are freed when the context is closed
 * - On allocation failure, no entry is added and an error is returned
 *
 * **Performance Characteristics:**
 * - Hash table insertion/replacement: O(1) average case
 * - No I/O operations performed (metadata written during image close)
 * - Minimal stack usage
 * - Suitable for bulk partition registration operations
 *
 * **Partition Organization:**
 * Proper partition metadata is essential for:
 * - Documenting the physical layout of multi-partition tapes
 * - Validating file block ranges against partition boundaries
 * - Preserving the original tape's partitioning scheme for archival purposes
 * - Supporting tape formats that require specific partition structures
 * - Enabling applications to understand and navigate complex tape layouts
 *
 * @param context Pointer to an initialized aaruformat context. Must not be NULL.
 *                The context must have been opened with write access (isWriting=true).
 *                The context's tapePartitions hash table will be updated with the new entry.
 *
 * @param partition The partition number (0-255) to set. For single-partition tapes,
 *                  this is typically 0. Multi-partition tapes use sequential numbers
 *                  (0, 1, 2, ...) though the numbering scheme is format-specific.
 *
 * @param starting_block The first block address of the partition (inclusive). This
 *                       defines where the partition begins in the tape's block address
 *                       space. Often 0 for the first partition, but format-dependent.
 *                       Must be <= ending_block (not validated).
 *
 * @param ending_block The last block address of the partition (inclusive). This defines
 *                     where the partition ends in the tape's block address space.
 *                     Must be >= starting_block (not validated).
 *
 * @retval AARUF_STATUS_OK (0) Successfully set tape partition information. The hash table
 *         has been updated with the partition entry. If an entry with the same partition
 *         number existed, it has been replaced. The metadata will be written to the
 *         TapePartitionBlock when the image is closed.
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) Invalid context or context validation failed.
 *         This is returned when:
 *         - The context pointer is NULL
 *         - The context magic number doesn't match AARU_MAGIC (corrupted or wrong type)
 *         No modifications are made to the context.
 *
 * @retval AARUF_READ_ONLY (-22) The context is not opened for writing. This is returned
 *         when ctx->isWriting is false, indicating the image was opened in read-only mode.
 *         Tape partition metadata cannot be modified in read-only mode. No modifications
 *         are made to the context.
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. The system could
 *         not allocate memory for the hash table entry (TapePartitionHashEntry). This is
 *         a critical error indicating low memory conditions. No modifications are made to
 *         the context.
 *
 * @note The function logs entry and exit points via TRACE macros when tracing is
 *       enabled, including parameter values and return codes for debugging.
 *
 * @note The tape partition metadata is not immediately written to disk. It remains in the
 *       context's hash table until the image is closed, at which point all entries
 *       are serialized and written to a TapePartitionBlock.
 *
 * @note No validation is performed on the block range values. The caller is responsible
 *       for ensuring that starting_block <= ending_block and that the range is valid
 *       for the image being created.
 *
 * @note No validation is performed to detect overlapping partition ranges. Partitions
 *       with overlapping block addresses may be accepted but will likely cause problems
 *       when reading the image or accessing files.
 *
 * @note If the same partition number is set multiple times, only the last values are
 *       retained. This can be used to update partition metadata but also means
 *       accidental duplicate calls will silently overwrite previous values.
 *
 * @note For single-partition tapes, it may be acceptable to omit the TapePartitionBlock
 *       entirely. Applications reading such images should assume a default partition 0
 *       spanning the entire tape if no partition metadata is present.
 *
 * @note Block addresses are local to each partition. Files within a partition reference
 *       blocks relative to that partition's address space, not the global tape address
 *       space (though in practice, many formats use absolute addressing).
 *
 * @warning This function is NOT thread-safe. Concurrent modifications to the tape partition
 *          table may result in undefined behavior, memory corruption, or memory leaks.
 *
 * @warning The caller must ensure the image is opened with write access before calling
 *          this function. Attempting to modify read-only images will fail with
 *          AARUF_READ_ONLY.
 *
 * @warning Parameter validation is minimal. Invalid block ranges (starting_block >
 *          ending_block) are accepted and will be written to the image, potentially
 *          causing problems when reading the image later.
 *
 * @warning If memory allocation fails (AARUF_ERROR_NOT_ENOUGH_MEMORY), the partition entry
 *          is not added. The caller should handle this error appropriately, potentially
 *          by freeing memory and retrying or aborting the write operation.
 *
 * @warning Partition metadata should be consistent with file metadata. Files should only
 *          reference partitions that have been defined, and their block ranges should
 *          fall within the partition boundaries. No automatic validation is performed.
 *
 * @see aaruf_get_tape_partition() for retrieving tape partition information from images
 * @see process_tape_partitions_block() for partition table initialization during read
 * @see TapePartitionEntry for the structure defining partition block ranges
 * @see TapePartitionHashEntry for the hash table entry structure
 * @see aaruf_set_tape_file() for setting file metadata within partitions
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_tape_partition(void *context, const uint8_t partition,
                                                       const uint64_t starting_block, const uint64_t ending_block)
{
    TRACE("Entering aaruf_set_tape_partition(%p, %d, %llu, %llu)", context, partition, starting_block, ending_block);

    aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_tape_partition() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_tape_partition() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_tape_partition() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Create hash table entry
    TapePartitionHashEntry *hash_entry = malloc(sizeof(TapePartitionHashEntry));
    if(hash_entry == NULL)
    {
        FATAL("Could not allocate memory for tape partition hash entry");

        TRACE("Exiting aaruf_set_tape_partition() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Create key: partition
    hash_entry->key = partition;

    // Copy the tape partition entry data
    hash_entry->partitionEntry.Number     = partition;
    hash_entry->partitionEntry.FirstBlock = starting_block;
    hash_entry->partitionEntry.LastBlock  = ending_block;

    // Replace if exists, add if new
    TapePartitionHashEntry *old_entry = NULL;
    HASH_REPLACE(hh, ctx->tape_partitions, key, sizeof(uint8_t), hash_entry, old_entry);
    ctx->dirty_tape_partition_block = true;  // Mark tape partition block as dirty

    // Free old entry if it was replaced
    if(old_entry != NULL)
    {
        TRACE("Replaced existing tape partition entry for partition %u", partition);
        free(old_entry);
    }
    else
        TRACE("Added new tape partition entry for partition %u", partition);

    TRACE("Exiting aaruf_set_tape_partition(%p, %d, %llu, %llu) = AARUF_STATUS_OK", context, partition, starting_block,
          ending_block);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves all tape file entries from the image.
 *
 * Extracts the complete set of tape file metadata entries from an Aaru tape image, returning
 * an array of TapeFileEntry structures. Tape files represent logical file divisions on sequential
 * access media (magnetic tapes) where data is organized into numbered files within numbered partitions.
 * Each file entry defines a contiguous range of tape blocks (FirstBlock to LastBlock inclusive) that
 * comprise one logical file unit on the tape medium.
 *
 * Tape file organization is hierarchical:
 * - **Partition**: Physical or logical division of the tape (0-255)
 * - **File**: Numbered file within a partition (0-4,294,967,295)
 * - **Block Range**: Contiguous sequence of tape blocks containing the file data
 *
 * This function is essential for applications that need to:
 * - Enumerate all logical files stored on a tape image
 * - Build file system catalogs or directory structures for tape media
 * - Implement tape navigation and seeking operations
 * - Extract individual files from multi-file tape archives
 * - Verify tape image completeness and structure
 * - Generate tape content manifests for archival documentation
 *
 * The function supports a two-call pattern for buffer size determination:
 * 1. First call with NULL buffer or insufficient length returns AARUF_ERROR_BUFFER_TOO_SMALL
 *    and sets *length to the required size (entry_count × sizeof(TapeFileEntry))
 * 2. Second call with properly sized buffer retrieves all file entries
 *
 * Alternatively, if the caller knows or pre-allocates sufficient buffer space, a single call
 * will succeed and populate the buffer with all tape file entries.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened tape image context).
 * @param buffer Pointer to a buffer that will receive the array of TapeFileEntry structures.
 *               May be NULL to query the required buffer size without retrieving data.
 * @param length Pointer to a size_t that serves dual purpose:
 *               - On input: size of the provided buffer in bytes (ignored if buffer is NULL)
 *               - On output: actual size required/used for all entries in bytes
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved all tape file entries.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_ERROR_TAPE_FILE_NOT_FOUND (-19) No tape file metadata exists.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-12) The provided buffer is insufficient.
 *
 * @see aaruf_get_tape_file() to retrieve a specific file's block range
 * @see aaruf_set_tape_file() to define or update a tape file entry
 * @see aaruf_get_all_tape_partitions() to retrieve partition boundaries
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_all_tape_files(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_all_tape_files(%p, %p, %zu)", context, buffer, (length ? *length : 0));

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_get_all_tape_files() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_get_all_tape_files() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->tape_files == NULL)
    {
        FATAL("Image contains no tape files");
        TRACE("Exiting aaruf_get_all_tape_files() = AARUF_ERROR_TAPE_FILE_NOT_FOUND");
        return AARUF_ERROR_TAPE_FILE_NOT_FOUND;
    }

    // Iterate all tape files to count how many do we have
    const size_t count         = HASH_COUNT(ctx->tape_files);
    const size_t required_size = count * sizeof(TapeFileEntry);

    if(buffer == NULL || length == NULL || *length < required_size)
    {
        if(length) *length = required_size;
        TRACE("Buffer too small for tape files, required %zu bytes", required_size);
        TRACE("Exiting aaruf_get_all_tape_files() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    size_t                   index = 0;
    const tapeFileHashEntry *entry;
    const tapeFileHashEntry *tmp;
    HASH_ITER(hh, ctx->tape_files, entry, tmp)
    {
        if(index < count)
        {
            memcpy(&((TapeFileEntry *)buffer)[index], &entry->fileEntry, sizeof(TapeFileEntry));
            index++;
        }
    }
    *length = required_size;

    TRACE("Exiting aaruf_get_all_tape_files(%p, %p, %zu) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves all tape partition entries from the image.
 *
 * Extracts the complete set of tape partition metadata entries from an Aaru tape image, returning
 * an array of TapePartitionEntry structures. Tape partitions represent physical or logical divisions
 * of sequential access media (magnetic tapes) that segment the tape into independent data regions.
 * Each partition entry defines a contiguous range of tape blocks (FirstBlock to LastBlock inclusive)
 * that comprise one partition on the tape medium.
 *
 * Tape partitions enable:
 * - **Multi-partition tapes**: Modern tape formats (LTO-5+, SDLT) support multiple partitions
 * - **Data organization**: Separating metadata, indexes, and data into distinct partitions
 * - **Fast access**: Partition switching is faster than sequential seeking across the entire tape
 * - **Logical separation**: Isolating different data sets or file systems on the same tape
 *
 * This function is essential for applications that need to:
 * - Enumerate all partitions on a multi-partition tape image
 * - Determine partition boundaries and sizes for navigation
 * - Implement partition-aware file system drivers or extraction tools
 * - Validate tape structure and partition layout integrity
 * - Generate tape structure documentation for archival purposes
 *
 * The function supports a two-call pattern for buffer size determination:
 * 1. First call with NULL buffer or insufficient length returns AARUF_ERROR_BUFFER_TOO_SMALL
 *    and sets *length to the required size (partition_count × sizeof(TapePartitionEntry))
 * 2. Second call with properly sized buffer retrieves all partition entries
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened tape image context).
 * @param buffer Pointer to a buffer that will receive the array of TapePartitionEntry structures.
 *               May be NULL to query the required buffer size without retrieving data.
 * @param length Pointer to a size_t that serves dual purpose:
 *               - On input: size of the provided buffer in bytes (ignored if buffer is NULL)
 *               - On output: actual size required/used for all entries in bytes
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved all tape partition entries.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_ERROR_TAPE_PARTITION_NOT_FOUND (-20) No tape partition metadata exists.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-12) The provided buffer is insufficient.
 *
 * @see aaruf_get_tape_partition() to retrieve a specific partition's block range
 * @see aaruf_set_tape_partition() to define or update a tape partition entry
 * @see aaruf_get_all_tape_files() to retrieve all tape file entries
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_all_tape_partitions(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_all_tape_partitions(%p, %p, %zu)", context, buffer, (length ? *length : 0));

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_get_all_tape_partitions() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_get_all_tape_partitions() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->tape_partitions == NULL)
    {
        FATAL("Image contains no tape partitions");
        TRACE("Exiting aaruf_get_all_tape_partitions() = AARUF_ERROR_TAPE_PARTITION_NOT_FOUND");
        return AARUF_ERROR_TAPE_PARTITION_NOT_FOUND;
    }

    // Iterate all tape partitions to count how many do we have
    const size_t count         = HASH_COUNT(ctx->tape_partitions);
    const size_t required_size = count * sizeof(TapePartitionEntry);

    if(buffer == NULL || length == NULL || *length < required_size)
    {
        if(length) *length = required_size;
        TRACE("Buffer too small for tape partitions, required %zu bytes", required_size);
        TRACE("Exiting aaruf_get_all_tape_partitions() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    size_t                        index = 0;
    const TapePartitionHashEntry *entry;
    const TapePartitionHashEntry *tmp;
    HASH_ITER(hh, ctx->tape_partitions, entry, tmp)
    {
        if(index < count)
        {
            memcpy(&((TapePartitionEntry *)buffer)[index], &entry->partitionEntry, sizeof(TapePartitionEntry));
            index++;
        }
    }
    *length = required_size;

    TRACE("Exiting aaruf_get_all_tape_partitions(%p, %p, %zu) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}
