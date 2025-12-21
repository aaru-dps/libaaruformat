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

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "aaruformat/context.h"
#include "aaruformat/errors.h"
#include "aaruformat/structs/index.h"
#include "consts.h"
#include "decls.h"
#include "log.h"
#include "utarray.h"
#include "uthash.h"

struct FluxCaptureMapEntry
{
    FluxCaptureKey key;
    uint32_t       index;
    UT_hash_handle hh;
};

/**
 * @brief Destructor callback for FluxCaptureRecord elements in a utarray.
 *
 * This function is used by the utarray library to clean up dynamically allocated
 * memory when a FluxCaptureRecord is removed from or when the array is freed.
 * It safely frees both the data_buffer and index_buffer fields if they are non-NULL,
 * and then nullifies the pointers to prevent double-free errors.
 *
 * @param element Pointer to the FluxCaptureRecord element to destroy. May be NULL.
 *
 * @note This function is registered with utarray via FLUX_CAPTURE_RECORD_ICD and
 *       is called automatically by utarray_free() and utarray_erase() operations.
 * @internal
 */
static void flux_capture_record_dtor(void *element)
{
    if(element == NULL) return;

    FluxCaptureRecord *record = element;
    free(record->data_buffer);
    free(record->index_buffer);
    record->data_buffer  = NULL;
    record->index_buffer = NULL;
}

static const UT_icd FLUX_CAPTURE_RECORD_ICD = {sizeof(FluxCaptureRecord), NULL, NULL, flux_capture_record_dtor};

/**
 * @brief Clear and deallocate the flux capture lookup map.
 *
 * This function iterates through all entries in the UTHASH-based flux capture
 * lookup map, removes each entry from the hash table, frees its memory, and
 * sets the map pointer to NULL. This is used during cleanup operations and when
 * rebuilding the map from scratch.
 *
 * The function is safe to call even if the map is already NULL or empty.
 *
 * @param ctx Pointer to the aaruformat context containing the flux_map to clear.
 *            Must not be NULL.
 *
 * @note This function does not free the flux_entries array or flux_captures utarray;
 *       it only clears the hash table used for O(1) lookup of flux captures.
 * @internal
 */
static void flux_map_clear(aaruformat_context *ctx)
{
    if(ctx->flux_map == NULL) return;

    FluxCaptureMapEntry *entry;
    FluxCaptureMapEntry *tmp;
    HASH_ITER(hh, ctx->flux_map, entry, tmp)
    {
        HASH_DEL(ctx->flux_map, entry);
        free(entry);
    }

    ctx->flux_map = NULL;
}

/**
 * @brief Add or update a flux capture entry in the lookup map.
 *
 * This function adds a new entry to the UTHASH-based flux capture lookup map,
 * or updates an existing entry if a matching key is already present. The map
 * enables O(1) lookup of flux captures by their (head, track, subtrack, captureIndex)
 * identifier tuple.
 *
 * If an entry with the same key already exists, its index is updated to the new value.
 * If no entry exists, a new FluxCaptureMapEntry is allocated and added to the map.
 *
 * @param ctx   Pointer to the aaruformat context containing the flux_map. Must not be NULL.
 * @param key   Pointer to the FluxCaptureKey identifying the flux capture (head, track,
 *              subtrack, captureIndex). Must not be NULL.
 * @param index The array index in ctx->flux_entries that corresponds to this flux capture.
 *
 * @return 0 on success, -1 on memory allocation failure.
 *
 * @note On allocation failure, the function returns -1 and the map remains unchanged.
 * @note This function is used internally during map rebuilding and when adding new
 *       flux captures in write mode.
 * @internal
 */
static int flux_map_add(aaruformat_context *ctx, const FluxCaptureKey *key, uint32_t index)
{
    FluxCaptureMapEntry *entry = NULL;
    HASH_FIND(hh, ctx->flux_map, key, sizeof(FluxCaptureKey), entry);

    if(entry == NULL)
    {
        entry = malloc(sizeof(FluxCaptureMapEntry));
        if(entry == NULL) return -1;
        entry->key = *key;
        HASH_ADD(hh, ctx->flux_map, key, sizeof(FluxCaptureKey), entry);
    }

    entry->index = index;
    return 0;
}

/**
 * @brief Rebuild the flux capture lookup map from the flux_entries array.
 *
 * This function clears any existing flux capture lookup map and rebuilds it from
 * the ctx->flux_entries array. The map provides O(1) lookup of flux captures by
 * their (head, track, subtrack, captureIndex) identifier tuple, which is used by
 * aaruf_read_flux_capture() and other flux access functions.
 *
 * The function iterates through all entries in ctx->flux_entries and adds each one
 * to the hash table, mapping its identifier to its array index. If any entry fails
 * to be added (due to memory allocation failure), the entire map is cleared and an
 * error is returned.
 *
 * @param ctx Pointer to the aaruformat context containing flux_entries and flux_map.
 *            Must not be NULL.
 *
 * @return AARUF_STATUS_OK on success, or AARUF_ERROR_NOT_ENOUGH_MEMORY if memory
 *         allocation fails during map construction.
 *
 * @retval AARUF_STATUS_OK The map was successfully rebuilt (or no entries to process).
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY Memory allocation failed; the map is cleared.
 *
 * @note If ctx->flux_entries is NULL or ctx->flux_data_header.entries is 0, the
 *       function clears the map and returns AARUF_STATUS_OK (no-op).
 * @note On failure, the map is cleared to ensure a consistent state.
 * @note This function is called automatically by process_flux_data_block() after
 *       successfully reading flux entries from the image.
 *
 * @see flux_map_clear() for clearing the map
 * @see flux_map_add() for adding individual entries
 * @see process_flux_data_block() for when this is called during image opening
 */
int32_t flux_map_rebuild_from_entries(aaruformat_context *ctx)
{
    flux_map_clear(ctx);

    if(ctx->flux_entries == NULL || ctx->flux_data_header.entries == 0) return AARUF_STATUS_OK;

    for(uint32_t i = 0; i < ctx->flux_data_header.entries; i++)
    {
        const FluxEntry *entry = &ctx->flux_entries[i];
        FluxCaptureKey   key   = {entry->head, entry->track, entry->subtrack, entry->captureIndex};

        if(flux_map_add(ctx, &key, i) != 0)
        {
            FATAL("Could not add flux capture to lookup map");
            flux_map_clear(ctx);
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Parse and integrate a Flux Data block from the image stream into the context.
 *
 * This function seeks to the byte offset specified by the supplied @p entry, reads a
 * FluxHeader followed by the declared number of FluxEntry records, validates the block
 * through its CRC64, rebuilds the flux capture lookup map, and populates multiple fields
 * in the provided @p ctx:
 *
 *  - ctx->flux_data_header (identifier, entries, crc64)
 *  - ctx->flux_entries (raw array with ALL flux entries in on-disk order)
 *  - ctx->flux_capture_map (hash table mapping (head, track, subtrack, captureIndex) to entry indices)
 *  - ctx->imageInfo.ImageSize (incremented by sizeof(FluxEntry) * entries)
 *
 * Before reading new data, the function performs cleanup of any existing flux-related state:
 *  - Frees and clears ctx->flux_captures if it exists (write-mode remnants)
 *  - Frees and clears ctx->flux_entries if it exists (from previous calls or partial reads)
 *  - Clears ctx->flux_capture_map (via flux_map_clear())
 *  - Zeros ctx->flux_data_header
 *
 * After successfully reading and validating the flux entries, the function rebuilds the
 * in-memory flux capture lookup map by calling flux_map_rebuild_from_entries(). This map
 * enables O(1) lookup of flux captures by their identifiers (head, track, subtrack, captureIndex),
 * which is used by aaruf_read_flux_capture() and other flux access functions.
 *
 * The function is intended for internal library use during image opening / indexing and is NOT
 * part of the stable public API (no versioning guarantees). Callers outside the library should use
 * the higher-level image open helpers that trigger this parsing implicitly.
 *
 * **Processing Flow:**
 * 1. **Validation:** Check ctx and ctx->imageStream are valid
 * 2. **Cleanup:** Free existing flux_captures, flux_entries, and clear flux_capture_map
 * 3. **Seek:** Seek to entry->offset in the image stream
 * 4. **Read Header:** Read FluxHeader structure
 * 5. **Validate Identifier:** Verify identifier == FluxDataBlock
 * 6. **Allocate Entries:** Allocate memory for flux_entries array
 * 7. **Read Entries:** Read all FluxEntry structures from the stream
 * 8. **Validate CRC64:** Compute and verify CRC64 checksum over entries
 * 9. **Rebuild Map:** Call flux_map_rebuild_from_entries() to build lookup map
 * 10. **Update ImageSize:** Increment ctx->image_info.ImageSize
 *
 * **Error & early-return behavior (no exception mechanism, all via logging + early return):**
 *  - NULL @p ctx or NULL ctx->imageStream: Logs FATAL and returns immediately; context left untouched.
 *  - Seek failure: FATAL + return; context left untouched (cleanup already performed).
 *  - FluxHeader read short: flux_data_header zeroed, TRACE logged, return.
 *  - Identifier mismatch: flux_data_header zeroed, TRACE logged, return.
 *  - Allocation failure for flux_entries: flux_data_header zeroed, FATAL logged, return.
 *  - Short read of FluxEntry array: flux_data_header zeroed, allocated flux_entries freed, FATAL logged, return.
 *  - CRC mismatch: TRACE logged and return; (NOTE: at this point flux_entries remain allocated and
 *    flux_data_header retains the just-read values, but the lookup map is not built, so entries
 *    are not accessible via lookup functions. Caller may wish to discard them or trigger re-read.)
 *  - Map rebuild failure: flux_entries freed, flux_data_header zeroed, FATAL logged, return.
 *
 * **Memory management:**
 *  - Frees any pre-existing ctx->flux_captures (utarray) before processing
 *  - Frees any pre-existing ctx->flux_entries before allocating new ones
 *  - Allocates ctx->flux_entries with malloc() sized to entries * sizeof(FluxEntry)
 *  - On certain failure paths (short reads, map rebuild failure) allocated memory is freed
 *  - On CRC mismatch, allocated memory is kept but map is not built (entries not accessible via lookup)
 *  - The function is idempotent in terms of memory: it cleans up before processing, so repeated
 *    calls will not leak memory. However, the function is expected to be called exactly once
 *    per context lifetime during image opening.
 *
 * **Flux Capture Lookup Map:**
 * After successfully reading and validating flux entries, the function rebuilds the in-memory
 * hash table (ctx->flux_capture_map) that maps flux capture identifiers to array indices. This
 * enables efficient lookup of flux captures by their (head, track, subtrack, captureIndex) tuple.
 * The map is built using UTHASH and is used by aaruf_read_flux_capture() and other flux access
 * functions. If map rebuild fails, the function cleans up all allocated resources and returns.
 *
 * **Thread safety:**
 *  - Not thread-safe: mutates shared state in @p ctx without synchronization.
 *  - Must not be called concurrently with readers/writers referencing the same context.
 *
 * **Preconditions (@pre):**
 *  - @p ctx != NULL
 *  - @p ctx->imageStream is a valid FILE* opened for reading at least up to the block region.
 *  - @p entry != NULL and entry->offset points to the start of a well-formed Flux Data block.
 *
 * **Postconditions (@post) on success (CRC valid and map rebuilt):**
 *  - ctx->flux_data_header.identifier == FluxDataBlock
 *  - ctx->flux_data_header.entries > 0 implies ctx->flux_entries != NULL
 *  - ctx->flux_capture_map is populated with entries mapping identifiers to indices
 *  - ctx->imageInfo.ImageSize incremented by flux data size
 *  - ctx->flux_captures == NULL (cleared if it existed)
 *
 * **Limitations / Caveats:**
 *  - No explicit status code: callers infer success by inspecting ctx->flux_data_header.entries and
 *    presence of ctx->flux_entries after invocation.
 *  - In case of CRC mismatch, flux_entries are retained but flux_capture_map is not built, so
 *    entries are not accessible via lookup functions. Caller may wish to discard them or trigger re-read.
 *  - The function is idempotent in terms of memory (cleans up before processing), but is expected
 *    to be called exactly once per context lifetime during image opening.
 *
 * **Logging strategy:**
 *  - FATAL used for unrecoverable structural or resource errors (seek failure, allocation failure,
 *    map rebuild failure).
 *  - TRACE used for informational / soft failures (e.g., CRC mismatch, identifier mismatch, short read).
 *
 * @param ctx   Mutable pointer to an aaruformatContext receiving parsed flux metadata.
 * @param entry Pointer to the index entry describing this Flux Data block (offset required; size not
 *              strictly used beyond informational logging and sequential reading).
 *
 * @return void This function does not return a status code; errors are reported via logging side effects.
 *
 * @warning Absence of a returned status requires defensive post-call validation by the caller.
 * @warning CRC mismatch leaves possibly invalid data in ctx->flux_entries, and the lookup map
 *          is not built, making entries inaccessible via lookup functions.
 * @warning Map rebuild failure results in complete cleanup of flux-related state, leaving the
 *          context without flux data even if the entries were successfully read and validated.
 *
 * @see flux_map_rebuild_from_entries() for the map rebuilding logic
 * @see flux_map_clear() for the map clearing logic
 * @see aaruf_read_flux_capture() for using the lookup map to access flux captures
 */
void process_flux_data_block(aaruformat_context *ctx, const IndexEntry *entry)
{
    int      pos        = 0;
    size_t   read_bytes = 0;
    uint64_t crc64      = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return;
    }

    if(ctx->flux_captures != NULL)
    {
        utarray_free(ctx->flux_captures);
        ctx->flux_captures = NULL;
    }

    if(ctx->flux_entries != NULL)
    {
        free(ctx->flux_entries);
        ctx->flux_entries = NULL;
    }

    flux_map_clear(ctx);

    memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);
        return;
    }

    // Even if those two checks shall have been done before
    read_bytes = fread(&ctx->flux_data_header, 1, sizeof(FluxHeader), ctx->imageStream);

    if(read_bytes != sizeof(FluxHeader))
    {
        memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));
        TRACE("Could not read flux data header, continuing...\n");
        return;
    }

    if(ctx->flux_data_header.identifier != FluxDataBlock)
    {
        memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));
        TRACE("Incorrect identifier for flux data block at position %" PRIu64 "\n", entry->offset);
        return;
    }

    ctx->image_info.ImageSize += sizeof(FluxEntry) * ctx->flux_data_header.entries;

    ctx->flux_entries = (FluxEntry *)malloc(sizeof(FluxEntry) * ctx->flux_data_header.entries);

    if(ctx->flux_entries == NULL)
    {
        memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));
        FATAL("Could not allocate memory for flux data block, continuing...\n");
        return;
    }

    read_bytes = fread(ctx->flux_entries, sizeof(FluxEntry), ctx->flux_data_header.entries, ctx->imageStream);

    if(read_bytes != ctx->flux_data_header.entries)
    {
        memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));
        free(ctx->flux_entries);
        ctx->flux_entries = NULL;
        FATAL("Could not read flux data block, continuing...\n");
        return;
    }

    crc64 = aaruf_crc64_data((const uint8_t *)ctx->flux_entries, ctx->flux_data_header.entries * sizeof(FluxEntry));

    if(crc64 != ctx->flux_data_header.crc64)
    {
        TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...\n", crc64,
              ctx->flux_data_header.crc64);
        return;
    }

    if(flux_map_rebuild_from_entries(ctx) != AARUF_STATUS_OK)
    {
        free(ctx->flux_entries);
        ctx->flux_entries = NULL;
        memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));
        return;
    }

    TRACE("Found %d flux entries at position %" PRIu64 ".\n", ctx->flux_data_header.entries, entry->offset);
}

/**
 * @brief Retrieve metadata for all flux captures in the image.
 *
 * This function retrieves metadata for all flux captures stored in the AaruFormat image.
 * The metadata includes head, track, subtrack, captureIndex, indexResolution, and
 * dataResolution for each capture, but does not include the actual flux data or index
 * buffers (use aaruf_read_flux_capture() to retrieve those).
 *
 * The function can be called with a NULL buffer to determine the required buffer size.
 * In this case, the required length is written to *length and AARUF_ERROR_BUFFER_TOO_SMALL
 * is returned.
 *
 * @param context Pointer to an initialized aaruformat context opened for reading.
 *                Must not be NULL.
 * @param buffer  Pointer to a buffer to receive the FluxCaptureMeta array. May be NULL
 *                to query the required size.
 * @param length  Pointer to the size of the buffer (in bytes). On input, must contain
 *                the buffer size. On output, contains the required size (if buffer is too
 *                small) or the actual size written. Must not be NULL.
 *
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 *
 * @retval AARUF_STATUS_OK Metadata successfully retrieved and written to buffer.
 * @retval AARUF_ERROR_NOT_AARUFORMAT Invalid context or context magic mismatch.
 * @retval AARUF_ERROR_FLUX_DATA_NOT_FOUND Image contains no flux captures.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL Buffer is NULL or too small; *length contains
 *                                      the required size.
 *
 * @note The buffer must be large enough to hold ctx->flux_data_header.entries *
 *       sizeof(FluxCaptureMeta) bytes.
 * @note The returned metadata array is in the same order as the flux entries in the
 *       FluxDataBlock (on-disk order).
 * @note This function only returns metadata; use aaruf_read_flux_capture() to retrieve
 *       the actual flux data and index buffers.
 *
 * @see aaruf_read_flux_capture() to retrieve actual flux data for a specific capture
 * @see FluxCaptureMeta for the structure of each metadata entry
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_flux_captures(void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_flux_captures(%p, %p, %zu)", context, buffer, (length ? *length : 0));

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->flux_data_header.entries == 0 || ctx->flux_entries == NULL)
    {
        FATAL("Image contains no flux captures");
        TRACE("Exiting aaruf_get_flux_captures() = AARUF_ERROR_FLUX_DATA_NOT_FOUND");
        return AARUF_ERROR_FLUX_DATA_NOT_FOUND;
    }

    size_t required_length = ctx->flux_data_header.entries * sizeof(FluxCaptureMeta);

    if(length == NULL)
    {
        TRACE("Buffer too small for flux captures, required %zu bytes", required_length);
        TRACE("Exiting aaruf_get_flux_captures() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    if(buffer == NULL || *length < required_length)
    {
        *length = required_length;
        TRACE("Buffer too small for flux captures, required %zu bytes", required_length);
        TRACE("Exiting aaruf_get_flux_captures() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    FluxCaptureMeta *out_entries = (FluxCaptureMeta *)buffer;
    for(uint16_t i = 0; i < ctx->flux_data_header.entries; i++)
    {
        const FluxEntry *entry         = &ctx->flux_entries[i];
        out_entries[i].head            = entry->head;
        out_entries[i].track           = entry->track;
        out_entries[i].subtrack        = entry->subtrack;
        out_entries[i].captureIndex    = entry->captureIndex;
        out_entries[i].indexResolution = entry->indexResolution;
        out_entries[i].dataResolution  = entry->dataResolution;
    }

    *length = required_length;

    TRACE("Exiting aaruf_get_flux_captures(%p, %p, %zu) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Add a flux capture to the image during write mode.
 *
 * This function adds a new flux capture to the image being written. The capture
 * includes both data and index buffers, along with metadata specifying the head,
 * track, subtrack, capture index, and resolution information for both streams.
 *
 * The function copies the provided data and index buffers into internal storage
 * (utarray) and creates a corresponding FluxEntry in the flux_entries array. The
 * actual payload data is written to the image later during image finalization
 * (in write_flux_blocks() and write_flux_capture_payload()).
 *
 * The flux capture lookup map is updated to enable efficient retrieval of the
 * capture by its identifier tuple.
 *
 * @param context        Pointer to an initialized aaruformat context opened for writing.
 *                       Must not be NULL.
 * @param head           Head number the flux capture corresponds to.
 * @param track          Track number the flux capture corresponds to.
 * @param subtrack       Subtrack number the flux capture corresponds to.
 * @param capture_index  Capture index, allowing multiple captures for the same location.
 * @param data_resolution Resolution in picoseconds for the data stream.
 * @param index_resolution Resolution in picoseconds for the index stream.
 * @param data           Pointer to the flux data buffer. May be NULL if data_length is 0.
 * @param data_length    Length of the data buffer in bytes.
 * @param index          Pointer to the flux index buffer. May be NULL if index_length is 0.
 * @param index_length   Length of the index buffer in bytes.
 *
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 *
 * @retval AARUF_STATUS_OK Flux capture successfully added to the context.
 * @retval AARUF_ERROR_NOT_AARUFORMAT Invalid context or context magic mismatch.
 * @retval AARUF_READ_ONLY Context is not in write mode.
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE Invalid buffer pointers or combined size exceeds UINT32_MAX.
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY Memory allocation failed for buffers or map entry.
 *
 * @note The function copies the data and index buffers; the caller retains ownership
 *       of the original buffers and may free them after this call.
 * @note The combined size of data_length + index_length must not exceed UINT32_MAX.
 * @note The maximum number of flux captures is limited to UINT16_MAX (65535).
 * @note The payloadOffset field in the FluxEntry is set to 0 initially and is
 *       populated later when the payload block is written during image finalization.
 * @note If adding the capture to the lookup map fails, the function performs cleanup
 *       and restores the previous state.
 *
 * @see aaruf_read_flux_capture() to read flux captures from an image
 * @see aaruf_clear_flux_captures() to remove all flux captures
 * @see write_flux_blocks() for when payload blocks are written
 */
AARU_EXPORT int32_t AARU_CALL aaruf_write_flux_capture(void *context, uint32_t head, uint16_t track, uint8_t subtrack,
                                                       uint32_t capture_index, uint64_t data_resolution,
                                                       uint64_t index_resolution, const uint8_t *data,
                                                       uint32_t data_length, const uint8_t *index,
                                                       uint32_t index_length)
{
    TRACE("Entering aaruf_add_flux_capture(%p, %u, %u, %u, %u, %" PRIu64 ", %" PRIu64 ", %p, %u, %p, %u)", context,
          head, track, subtrack, capture_index, data_resolution, index_resolution, data, data_length, index,
          index_length);

    if(context == NULL)
    {
        FATAL("Invalid context");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(!ctx->is_writing)
    {
        FATAL("Flux captures can only be added when writing");
        return AARUF_READ_ONLY;
    }

    if((index_length != 0 && index == NULL) || (data_length != 0 && data == NULL))
    {
        FATAL("Invalid flux capture buffers");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    if((uint64_t)data_length + index_length > UINT32_MAX)
    {
        FATAL("Flux capture too large (%" PRIu64 " bytes)", (uint64_t)data_length + index_length);
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    if(ctx->flux_captures == NULL)
    {
        utarray_new(ctx->flux_captures, &FLUX_CAPTURE_RECORD_ICD);
        if(ctx->flux_captures == NULL)
        {
            FATAL("Could not allocate flux capture storage");
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    size_t existing_captures = utarray_len(ctx->flux_captures);
    if(existing_captures >= UINT16_MAX)
    {
        FATAL("Flux capture limit exceeded (%zu >= %u)", existing_captures, UINT16_MAX);
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    uint8_t *data_copy  = NULL;
    uint8_t *index_copy = NULL;

    if(data_length != 0)
    {
        data_copy = malloc(data_length);
        if(data_copy == NULL)
        {
            FATAL("Could not allocate %u bytes for flux data", data_length);
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
        memcpy(data_copy, data, data_length);
    }

    if(index_length != 0)
    {
        index_copy = malloc(index_length);
        if(index_copy == NULL)
        {
            free(data_copy);
            FATAL("Could not allocate %u bytes for flux index", index_length);
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
        memcpy(index_copy, index, index_length);
    }

    FluxCaptureRecord record     = {0};
    record.entry.head            = head;
    record.entry.track           = track;
    record.entry.subtrack        = subtrack;
    record.entry.captureIndex    = capture_index;
    record.entry.dataResolution  = data_resolution;
    record.entry.indexResolution = index_resolution;
    record.entry.indexOffset     = data_length;
    record.entry.payloadOffset   = 0;
    record.data_buffer           = data_copy;
    record.data_length           = data_length;
    record.index_buffer          = index_copy;
    record.index_length          = index_length;

    FluxEntry *new_entries = realloc(ctx->flux_entries, (existing_captures + 1) * sizeof(FluxEntry));
    if(new_entries == NULL)
    {
        free(data_copy);
        free(index_copy);
        FATAL("Could not grow flux entry array");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    ctx->flux_entries = new_entries;
    utarray_push_back(ctx->flux_captures, &record);

    ctx->flux_entries[existing_captures] = record.entry;
    ctx->flux_data_header.identifier     = FluxDataBlock;
    ctx->flux_data_header.entries        = (uint16_t)(existing_captures + 1);
    ctx->flux_data_header.crc64 =
        aaruf_crc64_data((const uint8_t *)ctx->flux_entries, ctx->flux_data_header.entries * sizeof(FluxEntry));
    ctx->dirty_flux_block                = true;

    FluxCaptureKey key = {head, track, subtrack, capture_index};
    if(flux_map_add(ctx, &key, (uint32_t)existing_captures) != 0)
    {
        FATAL("Could not add flux capture to lookup map");

        size_t len = utarray_len(ctx->flux_captures);
        if(len > 0) utarray_erase(ctx->flux_captures, len - 1, 1);

        if(existing_captures == 0)
        {
            free(ctx->flux_entries);
            ctx->flux_entries = NULL;
        }
        else
        {
            FluxEntry *shrunk = realloc(ctx->flux_entries, existing_captures * sizeof(FluxEntry));
            if(shrunk != NULL) ctx->flux_entries = shrunk;
        }

        ctx->flux_data_header.entries = (uint16_t)existing_captures;
        ctx->flux_data_header.crc64   = existing_captures == 0 ? 0
                                                               : aaruf_crc64_data((const uint8_t *)ctx->flux_entries,
                                                                                  existing_captures * sizeof(FluxEntry));
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    TRACE("Exiting aaruf_add_flux_capture() = AARUF_STATUS_OK (captures=%u)", ctx->flux_data_header.entries);
    return AARUF_STATUS_OK;
}

/**
 * @brief Clear all flux captures from the context.
 *
 * This function removes all flux captures from the context, freeing all associated
 * memory including the flux_captures utarray, flux_entries array, and the flux
 * capture lookup map. The flux_data_header is zeroed.
 *
 * This function is useful for resetting the flux capture state, particularly in
 * write mode when starting a new image or when discarding previously added captures.
 *
 * @param context Pointer to an initialized aaruformat context. Must not be NULL.
 *
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 *
 * @retval AARUF_STATUS_OK All flux captures successfully cleared.
 * @retval AARUF_ERROR_NOT_AARUFORMAT Invalid context or context magic mismatch.
 *
 * @note This function is safe to call even if no flux captures are present (no-op).
 * @note After this call, ctx->flux_captures, ctx->flux_entries, and ctx->flux_map
 *       are all NULL/cleared, and ctx->flux_data_header is zeroed.
 * @note The function does not affect the image file itself; it only clears in-memory
 *       state. To remove flux data from an image file, the image must be rewritten.
 *
 * @see aaruf_write_flux_capture() to add flux captures
 * @see aaruf_get_flux_captures() to retrieve flux capture metadata
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_flux_captures(void *context)
{
    TRACE("Entering aaruf_clear_flux_captures(%p)", context);

    if(context == NULL)
    {
        FATAL("Invalid context");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->flux_captures != NULL)
    {
        utarray_free(ctx->flux_captures);
        ctx->flux_captures = NULL;
    }

    flux_map_clear(ctx);

    free(ctx->flux_entries);
    ctx->flux_entries = NULL;

    memset(&ctx->flux_data_header, 0, sizeof(FluxHeader));

    TRACE("Exiting aaruf_clear_flux_captures() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Find a flux entry by its identifier key.
 *
 * @param ctx Pointer to the aaruformat context containing flux data. Must not be NULL.
 * @param key Pointer to the FluxCaptureKey identifying the flux capture. Must not be NULL.
 * @return Pointer to the matching FluxEntry, or NULL if not found.
 * @internal
 */
static const FluxEntry *find_flux_entry_by_key(const aaruformat_context *ctx, const FluxCaptureKey *key)
{
    // Try lookup map first (O(1))
    if(ctx->flux_map != NULL)
    {
        FluxCaptureMapEntry *map_entry = NULL;
        HASH_FIND(hh, ctx->flux_map, key, sizeof(FluxCaptureKey), map_entry);
        if(map_entry != NULL && map_entry->index < ctx->flux_data_header.entries)
            return &ctx->flux_entries[map_entry->index];
    }

    // Fall back to linear search (O(n))
    for(uint32_t i = 0; i < ctx->flux_data_header.entries; i++)
    {
        const FluxEntry *entry = &ctx->flux_entries[i];
        if(entry->head == key->head && entry->track == key->track && entry->subtrack == key->subtrack &&
           entry->captureIndex == key->captureIndex)
            return entry;
    }

    return NULL;
}

/**
 * @brief Read and validate a flux payload block header from the image stream.
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL.
 * @param payload_offset File offset where the payload block starts.
 * @param header Output parameter for the read header. Must not be NULL.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 * @internal
 */
static int32_t read_flux_payload_header(const aaruformat_context *ctx, uint64_t payload_offset,
                                        DataStreamPayloadHeader *header)
{
    if(fseek(ctx->imageStream, payload_offset, SEEK_SET) < 0)
    {
        FATAL("Could not seek to flux payload at offset %" PRIu64, payload_offset);
        TRACE("Exiting read_flux_payload_header() = AARUF_ERROR_CANNOT_READ_BLOCK\n");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    long file_position = ftell(ctx->imageStream);
    if(file_position < 0 || (uint64_t)file_position != payload_offset)
    {
        FATAL("Invalid flux payload position (expected %" PRIu64 ", got %ld)", payload_offset, file_position);
        TRACE("Exiting read_flux_payload_header() = AARUF_ERROR_CANNOT_READ_BLOCK\n");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    size_t read_bytes = fread(header, 1, sizeof(DataStreamPayloadHeader), ctx->imageStream);
    if(read_bytes != sizeof(DataStreamPayloadHeader))
    {
        FATAL("Could not read flux payload header at offset %" PRIu64, payload_offset);
        TRACE("Exiting read_flux_payload_header() = AARUF_ERROR_CANNOT_READ_BLOCK\n");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(header->identifier != DataStreamPayloadBlock)
    {
        FATAL("Incorrect identifier 0x%08" PRIx32 " for flux payload at offset %" PRIu64, header->identifier,
              payload_offset);
        TRACE("Exiting read_flux_payload_header() = AARUF_ERROR_CANNOT_READ_BLOCK\n");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Read an uncompressed flux payload from the image stream.
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL.
 * @param cmp_length Compressed length (should equal raw_length for uncompressed).
 * @param raw_length Uncompressed length.
 * @param cmp_buffer Output parameter for compressed buffer pointer (caller must free). Can be NULL if allocation fails.
 * @param payload Output parameter for payload buffer pointer (same as cmp_buffer for uncompressed).
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 * @internal
 */
static int32_t read_uncompressed_payload(const aaruformat_context *ctx, size_t cmp_length, size_t raw_length,
                                         uint8_t **cmp_buffer, uint8_t **payload)
{
    if(cmp_length != raw_length)
    {
        FATAL("Flux payload lengths mismatch for uncompressed block (cmp=%zu, raw=%zu)", cmp_length, raw_length);
        TRACE("Exiting read_uncompressed_payload() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK\n");
        return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
    }

    if(cmp_length == 0)
    {
        *cmp_buffer = NULL;
        *payload     = NULL;
        return AARUF_STATUS_OK;
    }

    *cmp_buffer = (uint8_t *)malloc(cmp_length);
    if(*cmp_buffer == NULL)
    {
        FATAL("Could not allocate %zu bytes for flux payload", cmp_length);
        TRACE("Exiting read_uncompressed_payload() = AARUF_ERROR_NOT_ENOUGH_MEMORY\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    size_t read_bytes = fread(*cmp_buffer, 1, cmp_length, ctx->imageStream);
    if(read_bytes != cmp_length)
    {
        FATAL("Could not read %zu bytes of flux payload", cmp_length);
        free(*cmp_buffer);
        *cmp_buffer = NULL;
        TRACE("Exiting read_uncompressed_payload() = AARUF_ERROR_CANNOT_READ_BLOCK\n");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *payload = *cmp_buffer;
    return AARUF_STATUS_OK;
}

/**
 * @brief Read and decompress an LZMA-compressed flux payload from the image stream.
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL.
 * @param cmp_length Compressed length including LZMA properties.
 * @param raw_length Expected uncompressed length.
 * @param cmp_buffer Output parameter for compressed buffer pointer (caller must free). Can be NULL if allocation fails.
 * @param payload Output parameter for decompressed payload buffer pointer (caller must free). Can be NULL if allocation fails.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 * @internal
 */
static int32_t read_lzma_compressed_payload(const aaruformat_context *ctx, size_t cmp_length, size_t raw_length,
                                            uint8_t **cmp_buffer, uint8_t **payload)
{
    if(cmp_length <= LZMA_PROPERTIES_LENGTH)
    {
        FATAL("Flux payload compressed length %zu too small for LZMA", cmp_length);
        TRACE("Exiting read_lzma_compressed_payload() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK\n");
        return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
    }

    *cmp_buffer = (uint8_t *)malloc(cmp_length);
    if(*cmp_buffer == NULL)
    {
        FATAL("Could not allocate %zu bytes for flux payload", cmp_length);
        TRACE("Exiting read_lzma_compressed_payload() = AARUF_ERROR_NOT_ENOUGH_MEMORY\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    size_t read_bytes = fread(*cmp_buffer, 1, cmp_length, ctx->imageStream);
    if(read_bytes != cmp_length)
    {
        FATAL("Could not read %zu bytes of flux payload", cmp_length);
        free(*cmp_buffer);
        *cmp_buffer = NULL;
        TRACE("Exiting read_lzma_compressed_payload() = AARUF_ERROR_CANNOT_READ_BLOCK\n");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(raw_length == 0)
    {
        *payload = NULL;
        return AARUF_STATUS_OK;
    }

    *payload = (uint8_t *)malloc(raw_length);
    if(*payload == NULL)
    {
        FATAL("Could not allocate %zu bytes for decompressed flux payload", raw_length);
        free(*cmp_buffer);
        *cmp_buffer = NULL;
        TRACE("Exiting read_lzma_compressed_payload() = AARUF_ERROR_NOT_ENOUGH_MEMORY\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    size_t         cmp_stream_len = cmp_length - LZMA_PROPERTIES_LENGTH;
    size_t         dst_len        = raw_length;
    size_t         src_len        = cmp_stream_len;
    const uint8_t *cmp_props      = *cmp_buffer;
    const uint8_t *cmp_stream     = *cmp_buffer + LZMA_PROPERTIES_LENGTH;
    int32_t        error_no =
        aaruf_lzma_decode_buffer(*payload, &dst_len, cmp_stream, &src_len, cmp_props, LZMA_PROPERTIES_LENGTH);
    if(error_no != 0 || dst_len != raw_length)
    {
        FATAL("LZMA decompression failed for flux payload (err=%d, dst=%zu/%zu)", error_no, dst_len, raw_length);
        free(*payload);
        free(*cmp_buffer);
        *payload  = NULL;
        *cmp_buffer = NULL;
        TRACE("Exiting read_lzma_compressed_payload() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK\n");
        return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Validate CRC64 checksums for a flux payload block.
 *
 * @param cmp_buffer Compressed buffer to validate. Can be NULL if cmp_length is 0.
 * @param cmp_length Compressed length.
 * @param payload Uncompressed payload buffer to validate. Can be NULL if raw_length is 0.
 * @param raw_length Uncompressed length.
 * @param expected_cmp_crc Expected CRC64 of compressed data.
 * @param expected_raw_crc Expected CRC64 of uncompressed data.
 * @return AARUF_STATUS_OK on success, or AARUF_ERROR_INVALID_BLOCK_CRC on mismatch.
 * @internal
 */
static int32_t validate_flux_payload_crcs(const uint8_t *cmp_buffer, size_t cmp_length, const uint8_t *payload,
                                          size_t raw_length, uint64_t expected_cmp_crc, uint64_t expected_raw_crc)
{
    uint64_t cmp_crc = 0;
    if(cmp_length != 0 && cmp_buffer != NULL) cmp_crc = aaruf_crc64_data(cmp_buffer, cmp_length);

    if(cmp_crc != expected_cmp_crc)
    {
        FATAL("Flux payload compressed CRC mismatch (expected 0x%" PRIx64 ", got 0x%" PRIx64 ")", expected_cmp_crc,
              cmp_crc);
        TRACE("Exiting validate_flux_payload_crcs() = AARUF_ERROR_INVALID_BLOCK_CRC\n");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    uint64_t raw_crc = 0;
    if(raw_length != 0 && payload != NULL) raw_crc = aaruf_crc64_data(payload, raw_length);

    if(raw_crc != expected_raw_crc)
    {
        FATAL("Flux payload raw CRC mismatch (expected 0x%" PRIx64 ", got 0x%" PRIx64 ")", expected_raw_crc, raw_crc);
        TRACE("Exiting validate_flux_payload_crcs() = AARUF_ERROR_INVALID_BLOCK_CRC\n");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Extract data and index buffers from decompressed payload and copy to output buffers.
 *
 * @param flux_entry Pointer to the flux entry containing index offset information. Must not be NULL.
 * @param payload Pointer to the decompressed payload buffer. Can be NULL if raw_length is 0.
 * @param raw_length Length of the decompressed payload.
 * @param data_data Output buffer for data portion. Can be NULL for size query.
 * @param data_length Input/output parameter for data buffer size/required size. Must not be NULL.
 * @param index_data Output buffer for index portion. Can be NULL for size query.
 * @param index_length Input/output parameter for index buffer size/required size. Must not be NULL.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 * @internal
 */
static int32_t extract_flux_data_buffers(const FluxEntry *flux_entry, const uint8_t *payload, size_t raw_length,
                                         uint8_t *data_data, uint32_t *data_length, uint8_t *index_data,
                                         uint32_t *index_length)
{
    if(flux_entry->indexOffset > raw_length)
    {
        FATAL("Flux index offset %" PRIu64 " beyond payload length %zu", flux_entry->indexOffset, raw_length);
        TRACE("Exiting extract_flux_data_buffers() = AARUF_ERROR_INVALID_BLOCK_CRC\n");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    uint64_t data_length_required64  = flux_entry->indexOffset;
    uint64_t index_length_required64 = raw_length - flux_entry->indexOffset;

    if(data_length_required64 > UINT32_MAX || index_length_required64 > UINT32_MAX)
    {
        FATAL("Flux payload section length exceeds 32-bit limits (data=%" PRIu64 ", index=%" PRIu64 ")",
              data_length_required64, index_length_required64);
        TRACE("Exiting extract_flux_data_buffers() = AARUF_ERROR_INCORRECT_DATA_SIZE\n");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    uint32_t data_required  = (uint32_t)data_length_required64;
    uint32_t index_required = (uint32_t)index_length_required64;

    uint32_t data_capacity  = *data_length;
    uint32_t index_capacity = *index_length;

    *data_length  = data_required;
    *index_length = index_required;

    if(data_data == NULL || index_data == NULL || data_capacity < data_required || index_capacity < index_required)
    {
        TRACE("Returning required flux capture sizes (data=%u, index=%u)\n", data_required, index_required);
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    const uint8_t *index_ptr = payload ? payload + data_length_required64 : NULL;
    const uint8_t *data_ptr  = payload;

    if(data_required != 0 && data_ptr != NULL) memcpy(data_data, data_ptr, data_required);
    if(index_required != 0 && index_ptr != NULL) memcpy(index_data, index_ptr, index_required);

    return AARUF_STATUS_OK;
}

/**
 * @brief Read a specific flux capture's data and index buffers from the image.
 *
 * This function retrieves the actual flux data and index buffers for a specific
 * flux capture identified by its head, track, subtrack, and capture_index. The
 * function locates the corresponding FluxEntry in the flux_entries array (using
 * the lookup map for efficiency), seeks to the DataStreamPayloadBlock at the specified
 * payloadOffset, reads and decompresses the payload, validates CRC64 checksums,
 * and extracts the data and index buffers.
 *
 * The function supports both uncompressed and LZMA-compressed payload blocks.
 * CRC64 validation is performed on both the compressed and uncompressed data.
 *
 * The function can be called with NULL data buffers to determine the required
 * buffer sizes. In this case, the required lengths are written to *data_length
 * and *index_length, and AARUF_ERROR_BUFFER_TOO_SMALL is returned.
 *
 * @param context      Pointer to an initialized aaruformat context opened for reading.
 *                     Must not be NULL.
 * @param head         Head number of the flux capture to retrieve.
 * @param track        Track number of the flux capture to retrieve.
 * @param subtrack     Subtrack number of the flux capture to retrieve.
 * @param capture_index Capture index of the flux capture to retrieve.
 * @param index_data   Pointer to a buffer to receive the index buffer. May be NULL
 *                     to query the required size.
 * @param index_length Pointer to the size of the index buffer (in bytes). On input,
 *                     must contain the buffer size. On output, contains the required
 *                     size (if buffer is too small) or the actual size written.
 *                     Must not be NULL.
 * @param data_data    Pointer to a buffer to receive the data buffer. May be NULL
 *                     to query the required size.
 * @param data_length  Pointer to the size of the data buffer (in bytes). On input,
 *                     must contain the buffer size. On output, contains the required
 *                     size (if buffer is too small) or the actual size written.
 *                     Must not be NULL.
 *
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 *
 * @retval AARUF_STATUS_OK Flux capture data successfully retrieved and written to buffers.
 * @retval AARUF_ERROR_NOT_AARUFORMAT Invalid context, context magic mismatch, or invalid image stream.
 * @retval AARUF_ERROR_FLUX_DATA_NOT_FOUND No flux captures in image or specified capture not found.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL One or both buffers are NULL or too small;
 *                                      *data_length and *index_length contain required sizes.
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK Failed to seek to payload or read payload header.
 * @retval AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK LZMA decompression failed or invalid compression format.
 * @retval AARUF_ERROR_INVALID_BLOCK_CRC CRC64 checksum mismatch (compressed or uncompressed).
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE Payload section length exceeds 32-bit limits.
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY Memory allocation failed for decompression buffers.
 * @retval AARUF_ERROR_UNSUPPORTED_COMPRESSION Unsupported compression type in payload header.
 *
 * @note The function first attempts to locate the flux entry using the lookup map
 *       (O(1) lookup). If the map is not available or the entry is not found, it falls
 *       back to a linear search through flux_entries (O(n)).
 * @note The payload data is stored as [data_buffer][index_buffer] concatenated, with
 *       indexOffset indicating where the index buffer starts.
 * @note Both buffers must be large enough to hold the respective data. The function
 *       validates buffer sizes before copying data.
 * @note The caller is responsible for freeing the returned buffers if they were
 *       allocated by the caller.
 * @note CRC64 validation is performed on both compressed and uncompressed data to
 *       ensure data integrity.
 *
 * @see aaruf_get_flux_captures() to retrieve metadata for all flux captures
 * @see aaruf_write_flux_capture() to add flux captures during write mode
 * @see DataStreamPayloadHeader for the structure of payload blocks
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_flux_capture(void *context, uint32_t head, uint16_t track, uint8_t subtrack,
                                                      uint32_t capture_index, uint8_t *index_data,
                                                      uint32_t *index_length, uint8_t *data_data, uint32_t *data_length)
{
    TRACE("Entering aaruf_read_flux_capture(%p, %u, %u, %u, %u, %p, %p, %p, %p)", context, head, track, subtrack,
          capture_index, index_data, index_length, data_data, data_length);

    if(context == NULL)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->flux_data_header.entries == 0 || ctx->flux_entries == NULL)
    {
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_FLUX_DATA_NOT_FOUND");
        return AARUF_ERROR_FLUX_DATA_NOT_FOUND;
    }

    if(index_length == NULL || data_length == NULL)
    {
        FATAL("index_length or data_length pointers are NULL");
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_BUFFER_TOO_SMALL\n");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    if(ctx->imageStream == NULL)
    {
        FATAL("Invalid image stream");
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_NOT_AARUFORMAT\n");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    FluxCaptureKey        key        = {head, track, subtrack, capture_index};
    const FluxEntry      *flux_entry = find_flux_entry_by_key(ctx, &key);
    if(flux_entry == NULL)
    {
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_FLUX_DATA_NOT_FOUND\n");
        return AARUF_ERROR_FLUX_DATA_NOT_FOUND;
    }

    TRACE("Requested flux capture: head=%u track=%u subtrack=%u captureIndex=%u payloadOffset=%" PRIu64 "\n",
          flux_entry->head, flux_entry->track, flux_entry->subtrack, flux_entry->captureIndex,
          flux_entry->payloadOffset);

    DataStreamPayloadHeader payload_header;
    int32_t                 res = read_flux_payload_header(ctx, flux_entry->payloadOffset, &payload_header);
    if(res != AARUF_STATUS_OK)
    {
        TRACE("Exiting aaruf_read_flux_capture() = %d\n", res);
        return res;
    }

    const CompressionType compression = (CompressionType)payload_header.compression;
    uint8_t              *cmp_buffer  = NULL;
    uint8_t              *payload     = NULL;
    size_t                cmp_length  = payload_header.cmpLength;
    size_t                raw_length  = payload_header.length;

    if(compression == None)
    {
        res = read_uncompressed_payload(ctx, cmp_length, raw_length, &cmp_buffer, &payload);
    }
    else if(compression == Lzma)
    {
        res = read_lzma_compressed_payload(ctx, cmp_length, raw_length, &cmp_buffer, &payload);
    }
    else
    {
        FATAL("Unsupported flux payload compression type %u", payload_header.compression);
        TRACE("Exiting aaruf_read_flux_capture() = AARUF_ERROR_UNSUPPORTED_COMPRESSION\n");
        return AARUF_ERROR_UNSUPPORTED_COMPRESSION;
    }

    if(res != AARUF_STATUS_OK)
    {
        TRACE("Exiting aaruf_read_flux_capture() = %d\n", res);
        return res;
    }

    res = validate_flux_payload_crcs(cmp_buffer, cmp_length, payload, raw_length, payload_header.cmpCrc64,
                                      payload_header.crc64);
    if(res != AARUF_STATUS_OK)
    {
        if(payload != NULL && payload != cmp_buffer) free(payload);
        if(cmp_buffer != NULL) free(cmp_buffer);
        TRACE("Exiting aaruf_read_flux_capture() = %d\n", res);
        return res;
    }

    res = extract_flux_data_buffers(flux_entry, payload, raw_length, data_data, data_length, index_data,
                                    index_length);
    if(res != AARUF_STATUS_OK)
    {
        if(payload != NULL && payload != cmp_buffer) free(payload);
        if(cmp_buffer != NULL) free(cmp_buffer);
        TRACE("Exiting aaruf_read_flux_capture() = %d\n", res);
        return res;
    }

    if(payload != NULL && payload != cmp_buffer) free(payload);
    if(cmp_buffer != NULL) free(cmp_buffer);

    TRACE("Exiting aaruf_read_flux_capture() = AARUF_STATUS_OK\n");
    return AARUF_STATUS_OK;
}
