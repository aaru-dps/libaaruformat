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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat/consts.h"
#include "aaruformat/context.h"
#include "aaruformat/decls.h"
#include "aaruformat/endian.h"
#include "aaruformat/enums.h"
#include "aaruformat/structs/dump.h"
#include "aaruformat/structs/index.h"
#include "internal.h"
#include "log.h"

static void free_dump_hardware_entries_array(DumpHardwareEntriesWithData *entries, uint16_t count)
{
    if(entries == NULL) return;

    for(uint16_t e = 0; e < count; e++)
    {
        free(entries[e].manufacturer);
        free(entries[e].model);
        free(entries[e].revision);
        free(entries[e].firmware);
        free(entries[e].serial);
        free(entries[e].softwareName);
        free(entries[e].softwareVersion);
        free(entries[e].softwareOperatingSystem);
        free(entries[e].extents);
    }
}

static void reset_dump_hardware_context(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    free_dump_hardware_entries_array(ctx->dump_hardware_entries_with_data, ctx->dump_hardware_header.entries);
    free(ctx->dump_hardware_entries_with_data);
    ctx->dump_hardware_entries_with_data = NULL;
    memset(&ctx->dump_hardware_header, 0, sizeof(ctx->dump_hardware_header));
}

static bool read_dump_string(FILE *stream, const char *field_name, const uint32_t length, uint32_t *remaining,
                             uint8_t **destination)
{
    if(length == 0) return true;

    if(*remaining < length)
    {
        TRACE("Dump hardware %s length %u exceeds remaining payload %u", field_name, length,
              remaining == NULL ? 0 : *remaining);
        return false;
    }

    uint8_t *buffer = malloc(length);

    if(buffer == NULL)
    {
        TRACE("Could not allocate %s buffer of length %u", field_name, length);
        return false;
    }

    const size_t bytes_read = fread(buffer, 1, length, stream);

    if(bytes_read != length)
    {
        TRACE("Could not read %s field, expected %u bytes got %zu", field_name, length, bytes_read);
        free(buffer);
        return false;
    }

    *remaining -= length;
    *destination = buffer;

    return true;
}

/**
 * @brief Processes a dump hardware block from the image stream.
 *
 * Reads a dump hardware block from the image and updates the context with its contents.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the dump hardware block.
 */
void process_dumphw_block(aaruformat_context *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_dumphw_block(%p, %p)", ctx, entry);
    size_t read_bytes = 0;

    if(ctx == NULL || ctx->imageStream == NULL || entry == NULL)
    {
        FATAL("Invalid context, image stream, or index entry pointer.");
        TRACE("Exiting process_dumphw_block()");
        if(ctx != NULL) reset_dump_hardware_context(ctx);
        return;
    }

    if(entry->blockType != DumpHardwareBlock)
    {
        TRACE("Index entry block type %u is not DumpHardwareBlock, skipping.", entry->blockType);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    if(fseek(ctx->imageStream, entry->offset, SEEK_SET) < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);
        reset_dump_hardware_context(ctx);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    TRACE("Reading dump hardware block header at position %" PRIu64, entry->offset);
    DumpHardwareHeader header;
    read_bytes = fread(&header, 1, sizeof(header), ctx->imageStream);

    if(read_bytes != sizeof(header))
    {
        TRACE("Could not read dump hardware block header (read %zu bytes)", read_bytes);
        reset_dump_hardware_context(ctx);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    if(header.identifier != DumpHardwareBlock)
    {
        TRACE("Incorrect identifier 0x%08" PRIx32 " for dump hardware block at position %" PRIu64, header.identifier,
              entry->offset);
        reset_dump_hardware_context(ctx);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    if(header.entries > 0 && header.length == 0)
    {
        TRACE("Dump hardware header indicates %u entries but zero payload length", header.entries);
        reset_dump_hardware_context(ctx);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    const uint32_t payload_length = header.length;

    if(payload_length > 0)
    {
        uint8_t *payload = malloc(payload_length);

        if(payload == NULL)
        {
            TRACE("Could not allocate %u bytes for dump hardware payload", payload_length);
            reset_dump_hardware_context(ctx);
            TRACE("Exiting process_dumphw_block()");
            return;
        }

        read_bytes = fread(payload, 1, payload_length, ctx->imageStream);

        if(read_bytes != payload_length)
        {
            TRACE("Could not read dump hardware payload, expected %u bytes got %zu", payload_length, read_bytes);
            free(payload);
            reset_dump_hardware_context(ctx);
            TRACE("Exiting process_dumphw_block()");
            return;
        }

        uint64_t crc64 = aaruf_crc64_data(payload, payload_length);

        if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

        if(crc64 != header.crc64)
        {
            TRACE("Dump hardware block CRC mismatch: computed 0x%" PRIx64 " expected 0x%" PRIx64, crc64, header.crc64);
            free(payload);
            reset_dump_hardware_context(ctx);
            TRACE("Exiting process_dumphw_block()");
            return;
        }

        free(payload);

        if(fseek(ctx->imageStream, -(long)payload_length, SEEK_CUR) != 0)
        {
            TRACE("Could not rewind after CRC verification");
            reset_dump_hardware_context(ctx);
            TRACE("Exiting process_dumphw_block()");
            return;
        }
    }

    if(header.entries == 0)
    {
        reset_dump_hardware_context(ctx);
        ctx->dump_hardware_header = header;
        TRACE("Dump hardware block contains no entries. Clearing existing metadata.");
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    const size_t allocation_size = (size_t)header.entries * sizeof(DumpHardwareEntriesWithData);

    if(allocation_size / sizeof(DumpHardwareEntriesWithData) != header.entries)
    {
        TRACE("Dump hardware entries multiplication overflow (%u entries)", header.entries);
        reset_dump_hardware_context(ctx);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    DumpHardwareEntriesWithData *entries = calloc(header.entries, sizeof(DumpHardwareEntriesWithData));

    if(entries == NULL)
    {
        TRACE("Could not allocate %zu bytes for dump hardware entries", allocation_size);
        reset_dump_hardware_context(ctx);
        TRACE("Exiting process_dumphw_block()");
        return;
    }

    uint32_t remaining_payload = payload_length;
    uint16_t processed_entry   = 0;

    TRACE("Processing %u dump hardware block entries", header.entries);

    for(uint16_t e = 0; e < header.entries; e++)
    {
        processed_entry                      = e;
        DumpHardwareEntriesWithData *current = &entries[e];

        if(remaining_payload < sizeof(DumpHardwareEntry))
        {
            TRACE("Remaining payload %u too small for dump hardware entry %u", remaining_payload, e);
            goto parse_failure;
        }

        read_bytes = fread(&current->entry, 1, sizeof(DumpHardwareEntry), ctx->imageStream);

        if(read_bytes != sizeof(DumpHardwareEntry))
        {
            TRACE("Could not read dump hardware entry %u header (read %zu bytes)", e, read_bytes);
            goto parse_failure;
        }

        remaining_payload -= sizeof(DumpHardwareEntry);

        if(!read_dump_string(ctx->imageStream, "manufacturer", current->entry.manufacturerLength, &remaining_payload,
                             &current->manufacturer))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "model", current->entry.modelLength, &remaining_payload,
                             &current->model))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "revision", current->entry.revisionLength, &remaining_payload,
                             &current->revision))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "firmware", current->entry.firmwareLength, &remaining_payload,
                             &current->firmware))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "serial", current->entry.serialLength, &remaining_payload,
                             &current->serial))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "software name", current->entry.softwareNameLength, &remaining_payload,
                             &current->softwareName))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "software version", current->entry.softwareVersionLength,
                             &remaining_payload, &current->softwareVersion))
            goto parse_failure;

        if(!read_dump_string(ctx->imageStream, "software operating system",
                             current->entry.softwareOperatingSystemLength, &remaining_payload,
                             &current->softwareOperatingSystem))
            goto parse_failure;

        const uint32_t extent_count = current->entry.extents;

        if(extent_count == 0) continue;

        const size_t extent_bytes = (size_t)extent_count * sizeof(DumpExtent);

        if(extent_bytes / sizeof(DumpExtent) != extent_count || extent_bytes > remaining_payload)
        {
            TRACE("Extent array for entry %u exceeds remaining payload (%zu bytes requested, %u left)", e, extent_bytes,
                  remaining_payload);
            goto parse_failure;
        }

        current->extents = (DumpExtent *)malloc(extent_bytes);

        if(current->extents == NULL)
        {
            TRACE("Could not allocate %zu bytes for dump hardware entry %u extents", extent_bytes, e);
            goto parse_failure;
        }

        const size_t extents_read = fread(current->extents, sizeof(DumpExtent), extent_count, ctx->imageStream);

        if(extents_read != extent_count)
        {
            TRACE("Could not read %u dump hardware extents for entry %u (read %zu)", extent_count, e, extents_read);
            goto parse_failure;
        }

        remaining_payload -= (uint32_t)extent_bytes;

        qsort(current->extents, extent_count, sizeof(DumpExtent), compare_extents);
        TRACE("Sorted %u extents for entry %u", extent_count, e);
    }

    reset_dump_hardware_context(ctx);
    ctx->dump_hardware_entries_with_data = entries;
    ctx->dump_hardware_header            = header;

    if(remaining_payload != 0)
    {
        TRACE("Dump hardware block parsing completed with %u trailing payload bytes", remaining_payload);
    }

    TRACE("Exiting process_dumphw_block()");
    return;

parse_failure:
    free_dump_hardware_entries_array(entries, processed_entry + 1);
    free(entries);
    reset_dump_hardware_context(ctx);
    TRACE("Exiting process_dumphw_block()");
}