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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "log.h"

/**
 * @brief Processes a tracks block from the image stream.
 *
 * Reads a tracks block from the image and updates the context with its contents.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the tracks block.
 */
void process_tracks_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    int      pos        = 0;
    size_t   read_bytes = 0;
    uint64_t crc64      = 0;
    int      j = 0, k = 0;

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
    read_bytes = fread(&ctx->tracksHeader, 1, sizeof(TracksHeader), ctx->imageStream);

    if(read_bytes != sizeof(TracksHeader))
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        TRACE("Could not read tracks header, continuing...\n");
        return;
    }

    if(ctx->tracksHeader.identifier != TracksBlock)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
    }

    ctx->imageInfo.ImageSize += sizeof(TrackEntry) * ctx->tracksHeader.entries;

    ctx->trackEntries = (TrackEntry *)malloc(sizeof(TrackEntry) * ctx->tracksHeader.entries);

    if(ctx->trackEntries == NULL)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        FATAL("Could not allocate memory for metadata block, continuing...\n");
        return;
    }

    read_bytes = fread(ctx->trackEntries, sizeof(TrackEntry), ctx->tracksHeader.entries, ctx->imageStream);

    if(read_bytes != ctx->tracksHeader.entries)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        free(ctx->trackEntries);
        FATAL("Could not read metadata block, continuing...\n");

        return;
    }

    crc64 = aaruf_crc64_data((const uint8_t *)ctx->trackEntries, ctx->tracksHeader.entries * sizeof(TrackEntry));

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

    if(crc64 != ctx->tracksHeader.crc64)
    {
        TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...\n", crc64,
              ctx->tracksHeader.crc64);
        return;
    }

    TRACE("Found %d tracks at position %" PRIu64 ".\n", ctx->tracksHeader.entries, entry->offset);

    ctx->imageInfo.HasPartitions = true;
    ctx->imageInfo.HasSessions   = true;

    ctx->numberOfDataTracks = 0;

    for(j = 0; j < ctx->tracksHeader.entries; j++)
    {
        if(ctx->trackEntries[j].sequence > 0 && ctx->trackEntries[j].sequence <= 99) ctx->numberOfDataTracks++;
    }

    if(ctx->numberOfDataTracks > 0)
        ctx->dataTracks = malloc(sizeof(TrackEntry) * ctx->numberOfDataTracks);
    else
        ctx->dataTracks = NULL;

    k = 0;
    for(j = 0; j < ctx->tracksHeader.entries; j++)
    {
        if(ctx->trackEntries[j].sequence > 0 && ctx->trackEntries[j].sequence <= 99)
            memcpy(&ctx->dataTracks[k++], &ctx->trackEntries[j], sizeof(TrackEntry));
    }
}

/**
 * @brief Retrieve the array of track descriptors contained in an opened AaruFormat image.
 *
 * Provides the caller with a contiguous array of all track entries found in the image. The function
 * follows a two-step usage pattern allowing callers to query the required buffer size before
 * performing the actual copy.
 *
 * Usage pattern:
 *  - First call with buffer == NULL (or *length smaller than required). The function sets *length to the
 *    required size and returns AARUF_ERROR_BUFFER_TOO_SMALL.
 *  - Allocate a buffer of at least *length bytes and call again. On success, the function copies all
 *    TrackEntry structures and returns AARUF_STATUS_OK with *length set to the bytes copied.
 *
 * The returned data is a raw copy of internal TrackEntry structures (host endianness). The caller does
 * not assume ownership of internal memory; only the caller-provided buffer should be freed by the caller.
 *
 * Preconditions (@pre):
 *  - @pre context is a valid pointer to an aaruformatContext previously returned by an open function.
 *  - @pre context->magic == AARU_MAGIC.
 *  - @pre process_tracks_block() has run (implicitly done during image open) populating trackEntries.
 *
 * Thread safety:
 *  - Read-only access; safe for concurrent calls on different contexts.
 *  - Concurrent calls on the same context are safe only if no other thread is modifying/destroying it.
 *
 * Buffer sizing logic:
 *  - required_length = ctx->tracksHeader.entries * sizeof(TrackEntry)
 *  - If buffer == NULL OR *length < required_length => *length updated, return AARUF_ERROR_BUFFER_TOO_SMALL.
 *  - On success *length == required_length.
 *
 * Error strategy:
 *  - Validation failures: return specific error codes and log through FATAL()/TRACE().
 *  - No partial copies are performed.
 *
 * @param context Opaque pointer that MUST point to a valid aaruformatContext.
 * @param buffer  Destination buffer for a copy of all TrackEntry structures, or NULL to query size.
 * @param length  In/Out: On entry capacity of buffer (ignored if buffer == NULL). On return required or
 *                copied size in bytes. Must not be NULL.
 *
 * @return int32_t API status code.
 * @retval AARUF_STATUS_OK              Success; buffer filled.
 * @retval AARUF_ERROR_NOT_AARUFORMAT   context is NULL or not a valid libaaruformat context.
 * @retval AARUF_ERROR_TRACK_NOT_FOUND  No tracks present (entries == 0 or internal array NULL).
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL Sizing query / provided buffer insufficient; *length updated.
 *
 * @warning Passing an invalid context yields an error; no data is written to buffer.
 * @warning The function never performs partial copies.
 *
 * @note Order of TrackEntry elements matches on-disk order.
 * @note Caller may further filter (e.g., data vs audio) after retrieval.
 *
 * @since 1.0
 *
 * Usage example (conceptual):
 * 1. Open an image obtaining a valid aaruformatContext pointer.
 * 2. Invoke aaruf_get_tracks(ctx, NULL, &size) to query required buffer size (expect AARUF_ERROR_BUFFER_TOO_SMALL).
 * 3. Allocate a buffer of "size" bytes.
 * 4. Invoke aaruf_get_tracks(ctx, buffer, &size) again; on AARUF_STATUS_OK iterate over
 *    (size / sizeof(TrackEntry)) entries.
 * 5. Free the buffer and close the image when done.
 */
int32_t aaruf_get_tracks(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_tracks(%p, %p, %zu)", context, buffer, (length ? *length : 0));

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tracks() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tracks() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->tracksHeader.entries == 0 || ctx->trackEntries == NULL)
    {
        FATAL("Image contains no tracks");

        TRACE("Exiting aaruf_get_tracks() = AARUF_ERROR_TRACK_NOT_FOUND");
        return AARUF_ERROR_TRACK_NOT_FOUND;
    }

    size_t required_length = ctx->tracksHeader.entries * sizeof(TrackEntry);

    if(buffer == NULL || length == NULL || *length < required_length)
    {
        if(length) *length = required_length;
        TRACE("Buffer too small for tracks, required %zu bytes", required_length);

        TRACE("Exiting aaruf_get_tracks() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, ctx->trackEntries, required_length);
    *length = required_length;

    TRACE("Exiting aaruf_get_tracks(%p, %p, %zu) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}


/**
 * @brief Replace (or clear) the in-memory track table for an AaruFormat image context.
 *
 * Copies an array of caller-provided TrackEntry structures into the internal context, replacing any
 * previously stored track metadata. A CRC64 is recomputed over the new table and stored in the
 * associated TracksHeader. Passing a count of 0 clears existing track information.
 *
 * Typical usage:
 *  - Prepare an array of TrackEntry structures describing each track (filled by the caller).
 *  - Call aaruf_set_tracks() with the array and the track count to load them into the context.
 *  - Subsequently the table can be retrieved with aaruf_get_tracks().
 *
 * Memory ownership:
 *  - The function allocates (or frees when clearing) internal storage sized to (count * sizeof(TrackEntry)).
 *  - The caller retains ownership of the input array (if any) and may free or reuse it after the call.
 *
 * Validation performed:
 *  - @p context must be non-NULL and reference aaruformatContext with magic == AARU_MAGIC.
 *  - @p tracks must be non-NULL when @p count > 0.
 *  - @p count must be >= 0. (Negative values produce AARUF_ERROR_INVALID_TRACK_FORMAT.)
 *  - (Implementation detail) count is truncated to uint16_t for header storage; values > UINT16_MAX
 *    will silently wrap. Callers should ensure count <= 65535. This behavior may change in a future version.
 *
 * Concurrency & thread-safety:
 *  - Not thread-safe. Mutates shared context state. External synchronization is required if multiple
 *    threads access the same context.
 *
 * Side effects:
 *  - Frees any existing internal track table before allocating the new one.
 *  - Updates ctx->tracksHeader.identifier, entries, crc64.
 *  - When clearing (count == 0) sets header to zero and frees internal table.
 *
 * Error handling & atomicity:
 *  - On allocation failure the previous track table is already freed (non-atomic replace) and the
 *    header is zeroed (no partial new state left). Caller must repopulate.
 *  - No partial copies: either all tracks are stored or none.
 *
 * @param context  Opaque pointer that MUST point to a valid aaruformatContext returned by an open/create call.
 * @param tracks   Pointer to an array of TrackEntry structures to copy. Must not be NULL if count > 0.
 *                 Ignored (may be NULL) when count == 0.
 * @param count    Number of TrackEntry elements in @p tracks. If 0, existing tracks are cleared.
 *                 Must be >= 0 and (recommended) <= UINT16_MAX.
 *
 * @return int32_t API status code indicating success or the nature of the failure.
 * @retval AARUF_STATUS_OK                 Success (tracks replaced or cleared).
 * @retval AARUF_ERROR_NOT_AARUFORMAT      @p context is NULL or not a recognized libaaruformat context.
 * @retval AARUF_ERROR_INVALID_TRACK_FORMAT Invalid input (tracks NULL with count > 0, or count < 0).
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY   Memory allocation failed while copying tracks.
 *
 * @warning Not thread-safe. Do not invoke concurrently with readers/writers of the same context.
 * @warning Counts above 65535 will be truncated to 16-bit without error (potential data loss of extra entries).
 * @note After success, aaruf_get_tracks() can be used to read back the stored table.
 * @see aaruf_get_tracks()
 *
 * @since 1.0
 *
 * Usage example (conceptual):
 * 1. Prepare an array of TrackEntry structures (N elements) and fill the fields.
 * 2. Call aaruf_set_tracks(ctx, array, N) to store them; check for AARUF_STATUS_OK.
 * 3. To clear all tracks later call aaruf_set_tracks(ctx, NULL, 0).
 * 4. Use aaruf_get_tracks() afterwards to retrieve them if needed.
 */
int32_t aaruf_set_tracks(void *context, TrackEntry *tracks, const int count)
{
    TRACE("Entering aaruf_set_tracks(%p, %p, %d)", context, tracks, count);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tracks() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_tracks() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Clearing existing tracks
    if(count == 0)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        free(ctx->trackEntries);
        ctx->trackEntries = NULL;

        TRACE("Exiting aaruf_set_tracks() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(tracks == NULL || count < 0)
    {
        FATAL("Invalid tracks data");

        TRACE("Exiting aaruf_set_tracks() = AARUF_ERROR_INVALID_TRACK_FORMAT");
        return AARUF_ERROR_INVALID_TRACK_FORMAT;
    }

    ctx->tracksHeader.identifier = TracksBlock;
    ctx->tracksHeader.entries    = (uint16_t)count;
    free(ctx->trackEntries);
    ctx->trackEntries = malloc(sizeof(TrackEntry) * count);
    if(ctx->trackEntries == NULL)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        FATAL("Could not allocate memory for tracks");

        TRACE("Exiting aaruf_set_tracks() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(ctx->trackEntries, tracks, sizeof(TrackEntry) * count);
    ctx->tracksHeader.crc64 = aaruf_crc64_data((const uint8_t *)ctx->trackEntries, sizeof(TrackEntry) * count);

    TRACE("Exiting aaruf_set_tracks() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}