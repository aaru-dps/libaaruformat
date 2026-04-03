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
 */
/**
 * @file close.c
 * @brief Resource cleanup and public ::aaruf_close() entry point for libaaruformat.
 *
 * Performs orderly teardown of dynamically allocated resources when closing
 * an Aaru image context, regardless of read or write mode. For write-mode
 * contexts the writer finalization hook (::aaruf_finalize_write(), defined in
 * close_write.c) is invoked via a function pointer before cleanup begins.
 *
 * @see close_write.c
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <aaruformat.h>

#include "internal.h"
#include "log.h"

static inline void aaruf_set_close_error(const int error_code)
{
    errno = error_code;
#if defined(_WIN32) || defined(_WIN64)
    SetLastError((DWORD)error_code);
#endif
}

/**
 * @brief Close an Aaru image context, flushing pending data structures and releasing resources.
 *
 * Public API entry point used to finalize an image being written or simply dispose of a context
 * opened for reading. For write-mode contexts the finalize_write function pointer (set during
 * aaruf_create() or resume-mode aaruf_open()) is invoked to flush all pending blocks, DDTs, and
 * the global index before cleanup proceeds.
 *
 * Afterwards (or for read-mode contexts) all dynamically allocated buffers, arrays, hash tables
 * and mapping structures are freed/unmapped. Media tags are removed from their hash table.
 *
 * Error Handling:
 *   - Returns -1 with errno = EINVAL if the provided pointer is NULL or not a valid context.
 *   - If the writer finalization hook returns an error status, that error value is
 *     propagated directly.
 *
 * @param context Opaque pointer returned by earlier open/create calls (must be an aaruformatContext).
 * @return 0 on success; -1 or negative libaaruformat error code on failure.
 * @retval 0 All pending data flushed (if writing) and resources released successfully.
 * @retval -1 Invalid context pointer (errno = EINVAL).
 * @retval <negative libaaruformat code> Propagated from the writer finalization hook.
 * @note On success the context memory itself is freed; the caller must not reuse the pointer.
 */
AARU_EXPORT int AARU_CALL aaruf_close(void *context)
{
    TRACE("Entering aaruf_close(%p)", context);
    aaruf_set_close_error(0);

    mediaTagEntry *media_tag     = NULL;
    mediaTagEntry *tmp_media_tag = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");
        aaruf_set_close_error(EINVAL);
        return -1;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        aaruf_set_close_error(EINVAL);
        return -1;
    }

    if(ctx->finalize_write != NULL)
    {
        int32_t res = ctx->finalize_write(ctx);
        if(res != AARUF_STATUS_OK)
        {
            aaruf_set_close_error(errno != 0 ? errno : res);
            return res;
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
    free(ctx->data_tracks);
    ctx->data_tracks = NULL;
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
        free(ctx->dump_hardware_entries_with_data);  // Free the array itself
        ctx->dump_hardware_entries_with_data = NULL;
    }

    free(ctx->readableSectorTags);
    ctx->readableSectorTags = NULL;

    aaruf_ecc_cd_free(ctx->ecc_cd_context);
    ctx->ecc_cd_context = NULL;

    free(ctx->checksums.spamsum);
    ctx->checksums.spamsum = NULL;

    // Free PS3 encryption context
    if(ctx->ps3_disc_key != NULL)
    {
        memset(ctx->ps3_disc_key, 0, 16);
        free(ctx->ps3_disc_key);
        ctx->ps3_disc_key = NULL;
    }
    free(ctx->ps3_plaintext_regions);
    ctx->ps3_plaintext_regions      = NULL;
    ctx->ps3_plaintext_region_count = 0;
    ctx->ps3_encryption_initialized = false;

    // Free Wii U encryption context
    if(ctx->wiiu_disc_key != NULL)
    {
        memset(ctx->wiiu_disc_key, 0, 16);
        free(ctx->wiiu_disc_key);
        ctx->wiiu_disc_key = NULL;
    }
    if(ctx->wiiu_partition_regions != NULL)
    {
        // Wipe keys from partition regions before freeing
        uint32_t wiiu_count = ctx->wiiu_partition_region_count;
        uint8_t *region_mem = (uint8_t *)ctx->wiiu_partition_regions;
        // Each region entry contains a 16-byte key at offset 8; wipe the entire block
        memset(region_mem, 0, wiiu_count * 24);
        free(ctx->wiiu_partition_regions);
        ctx->wiiu_partition_regions = NULL;
    }
    ctx->wiiu_partition_region_count = 0;
    ctx->wiiu_encryption_initialized = false;
    free(ctx->wiiu_encrypted_block_cache);
    ctx->wiiu_encrypted_block_cache = NULL;
    ctx->wiiu_cache_valid           = false;

    // Free Nintendo GC/Wii junk map context
    if(ctx->ngcw_junk_entries != NULL)
    {
        free(ctx->ngcw_junk_entries);
        ctx->ngcw_junk_entries = NULL;
    }
    ctx->ngcw_junk_entry_count = 0;
    ctx->ngcw_junk_seed_size   = 0;
    ctx->ngcw_junk_initialized = false;

    // Free Wii encryption context
    if(ctx->wii_partition_regions != NULL)
    {
        // Wipe keys from partition regions before freeing
        uint32_t wii_count  = ctx->wii_partition_region_count;
        uint8_t *wii_region = (uint8_t *)ctx->wii_partition_regions;
        memset(wii_region, 0, wii_count * 24);
        free(ctx->wii_partition_regions);
        ctx->wii_partition_regions = NULL;
    }
    ctx->wii_partition_region_count = 0;
    ctx->wii_encryption_initialized = false;
    free(ctx->wii_encrypted_group_cache);
    ctx->wii_encrypted_group_cache = NULL;
    ctx->wii_cache_valid           = false;

    free(ctx->sector_id);
    free(ctx->sector_ied);
    free(ctx->sector_cpr_mai);
    free(ctx->sector_edc);

    // Free DDT allocations (v1 and v2)
    free(ctx->user_data_ddt);          // Legacy v1 DDT
    free(ctx->user_data_ddt2);         // v2 DDT primary/secondary
    free(ctx->cached_secondary_ddt2);  // Cached secondary DDT (read operations)

    // Free LRU caches (uses cache->free_func to free cached values)
    free_cache(&ctx->block_header_cache);
    free_cache(&ctx->block_cache);

    free(context);

    TRACE("Exiting aaruf_close() = 0");
    return 0;
}
