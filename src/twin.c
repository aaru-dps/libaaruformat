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
 * @file twin.c
 * @brief Twin sectors: several physical sectors answering to the same sector address.
 *
 * Twin #0 is stored as any other sector. Twins #1..n-1 are kept in memory as TwinInstance records (one value per
 * component: user data, CD prefix/suffix, subheader, subchannel, DVD/BD long-sector fields, Apple/Priam tags) and
 * serialized into a single twin sector table block (TWTB) when the image is closed.
 *
 * To avoid duplicating the decomposition/recomposition logic of the long-sector and sector-tag functions, a twin
 * operation temporarily "swaps" the twin's components into the per-sector storage slots of the address, runs the
 * normal function, captures the resulting slot values back into the twin (write) and restores the slots of twin #0.
 * User data is redirected through ctx->twin_write_* (write) and ctx->twin_read_* (read), which aaruf_write_sector()
 * and aaruf_read_sector() honour.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>
#include "erasure_internal.h"
#include "internal.h"
#include "log.h"
#include "uthash.h"

#define TWIN_ENTRY_MASK 0x0FFFFFFFFFFFFFFFULL


/* ------------------------------------------------------------------------------------------------------------------ */
/* Helpers                                                                                                            */
/* ------------------------------------------------------------------------------------------------------------------ */

static uint64_t twin_total_sectors(const aaruformat_context *ctx)
{ return ctx->user_data_ddt_header.negative + ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow; }

uint64_t twin_internal_index(const aaruformat_context *ctx, const uint64_t sector_address, const bool negative)
{
    return negative ? ctx->user_data_ddt_header.negative - sector_address
                    : sector_address + ctx->user_data_ddt_header.negative;
}

TwinSectorGroupEntry *twin_find_group(const aaruformat_context *ctx, uint64_t internal_index)
{
    if(ctx == NULL || ctx->twin_sectors == NULL) return NULL;

    TwinSectorGroupEntry *group = NULL;
    HASH_FIND(hh, ctx->twin_sectors, &internal_index, sizeof(uint64_t), group);
    return group;
}

uint8_t twin_resolve_twin0_status(const aaruformat_context *ctx, const uint64_t sector_address, const bool negative)
{
    const TwinSectorGroupEntry *group = twin_find_group(ctx, twin_internal_index(ctx, sector_address, negative));

    // A Twin status without a table entry means the table was lost; the pointer still references twin #0.
    if(group == NULL) return SectorStatusDumped;

    return group->twin0_status;
}

/** Size of the Apple/Priam tag carried by the media of the context, 0 if none. */
static uint32_t twin_tag_size(const aaruformat_context *ctx)
{
    if(ctx->image_info.MetadataMediaType != BlockMedia) return 0;

    switch(ctx->image_info.MediaType)
    {
        case AppleFileWare:
        case AppleProfile:
        case AppleWidget:
            return 20;
        case AppleSonySS:
        case AppleSonyDS:
            return 12;
        case PriamDataTower:
            return 24;
        default:
            return 0;
    }
}

static uint16_t twin_tag_data_type(const uint32_t size)
{
    switch(size)
    {
        case 12:
            return kDataTypeAppleSonyTag;
        case 20:
            return kDataTypeAppleProfileTag;
        case 24:
            return kDataTypePriamDataTowerTag;
        default:
            return kDataTypeNone;
    }
}

/** Size in bytes of a component (0 if not applicable to the media). */
static uint32_t twin_component_size(const aaruformat_context *ctx, const int comp)
{
    switch(comp)
    {
        case kTwinCompData:
        case kTwinCompPrefix:
        case kTwinCompSuffix:
            return 8;
        case kTwinCompSubheader:
            return 8;
        case kTwinCompSubchannel:
            return ctx->image_info.MetadataMediaType == OpticalDisc ? 96 : 0;
        case kTwinCompDvdId:
            return 4;
        case kTwinCompDvdIed:
            return 2;
        case kTwinCompDvdCprMai:
            return 6;
        case kTwinCompSectorEdc:
            return 4;
        case kTwinCompTitleKey:
            return 5;
        case kTwinCompTag:
            return twin_tag_size(ctx);
        default:
            return 0;
    }
}

/** Pointer to the storage of a component inside a twin instance. */
static uint8_t *twin_instance_field(TwinInstance *instance, const int comp)
{
    switch(comp)
    {
        case kTwinCompData:
            return (uint8_t *)&instance->data;
        case kTwinCompPrefix:
            return (uint8_t *)&instance->prefix;
        case kTwinCompSuffix:
            return (uint8_t *)&instance->suffix;
        case kTwinCompSubheader:
            return instance->subheader;
        case kTwinCompSubchannel:
            return instance->subchannel;
        case kTwinCompDvdId:
            return instance->dvd_id;
        case kTwinCompDvdIed:
            return instance->dvd_ied;
        case kTwinCompDvdCprMai:
            return instance->dvd_cpr_mai;
        case kTwinCompSectorEdc:
            return instance->sector_edc;
        case kTwinCompTitleKey:
            return instance->title_key;
        case kTwinCompTag:
            return instance->tag;
        default:
            return NULL;
    }
}

/**
 * Pointer to the per-sector storage slot of a component for the given internal index, or NULL if the storage does
 * not exist (and allocate is false or the allocation failed). User data has no slot.
 */
static uint8_t *twin_slot(aaruformat_context *ctx, const int comp, uint64_t index, const bool allocate)
{
    const uint32_t size  = twin_component_size(ctx, comp);
    uint64_t       total = twin_total_sectors(ctx);
    uint8_t      **array = NULL;

    // Apple / Priam tags cover the user area and overflow sectors only, indexed by sector address
    if(comp == kTwinCompTag)
    {
        if(index < ctx->user_data_ddt_header.negative) return NULL;

        index -= ctx->user_data_ddt_header.negative;
        total = ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow;
    }

    if(size == 0 || index >= total) return NULL;

    switch(comp)
    {
        case kTwinCompPrefix:
            array = (uint8_t **)&ctx->sector_prefix_ddt2;
            break;
        case kTwinCompSuffix:
            array = (uint8_t **)&ctx->sector_suffix_ddt2;
            break;
        case kTwinCompSubheader:
            array = &ctx->mode2_subheaders;
            break;
        case kTwinCompSubchannel:
        case kTwinCompTag:
            array = &ctx->sector_subchannel;
            break;
        case kTwinCompDvdId:
            array = &ctx->sector_id;
            break;
        case kTwinCompDvdIed:
            array = &ctx->sector_ied;
            break;
        case kTwinCompDvdCprMai:
            array = &ctx->sector_cpr_mai;
            break;
        case kTwinCompSectorEdc:
            array = &ctx->sector_edc;
            break;
        case kTwinCompTitleKey:
            array = &ctx->sector_decrypted_title_key;
            break;
        default:
            return NULL;
    }

    if(*array == NULL)
    {
        if(!allocate) return NULL;

        *array = calloc(total, size);
        if(*array == NULL) return NULL;
    }

    return *array + index * size;
}

/** Saved contents of the slots of twin #0 while a twin operation runs. */
typedef struct
{
    bool    had[kTwinCompCount];
    uint8_t bytes[kTwinCompCount][96];
} TwinSlotBackup;

/** Places the components of instance (or nothing, when NULL) into the slots of the sector. */
static void twin_swap_in(aaruformat_context *ctx, const uint64_t index, TwinInstance *instance, TwinSlotBackup *backup)
{
    for(int comp = kTwinCompData + 1; comp < kTwinCompCount; comp++)
    {
        const uint32_t size    = twin_component_size(ctx, comp);
        const bool     present = instance != NULL && (instance->present & 1U << comp) != 0;

        backup->had[comp] = false;
        if(size == 0) continue;

        // Storage that did not exist before only gets created when the twin carries a value for it
        uint8_t *existing = twin_slot(ctx, comp, index, false);
        uint8_t *slot     = existing != NULL ? existing : twin_slot(ctx, comp, index, present);

        if(slot == NULL) continue;

        if(existing != NULL)
        {
            backup->had[comp] = true;
            memcpy(backup->bytes[comp], slot, size);
        }

        if(present)
            memcpy(slot, twin_instance_field(instance, comp), size);
        else
            memset(slot, 0, size);
    }
}

/** Copies the slot values of the sector into instance. */
static void twin_capture(aaruformat_context *ctx, const uint64_t index, TwinInstance *instance)
{
    for(int comp = kTwinCompData + 1; comp < kTwinCompCount; comp++)
    {
        const uint32_t size = twin_component_size(ctx, comp);
        uint8_t       *slot = size == 0 ? NULL : twin_slot(ctx, comp, index, false);

        if(slot == NULL)
        {
            instance->present &= ~(1U << comp);
            continue;
        }

        memcpy(twin_instance_field(instance, comp), slot, size);
        instance->present |= 1U << comp;
    }
}

/** Restores the slots of twin #0. Slots whose storage was created by the twin operation are zeroed. */
static void twin_restore(aaruformat_context *ctx, const uint64_t index, const TwinSlotBackup *backup)
{
    for(int comp = kTwinCompData + 1; comp < kTwinCompCount; comp++)
    {
        const uint32_t size = twin_component_size(ctx, comp);
        uint8_t       *slot = size == 0 ? NULL : twin_slot(ctx, comp, index, false);

        if(slot == NULL) continue;

        if(backup->had[comp])
            memcpy(slot, backup->bytes[comp], size);
        else
            memset(slot, 0, size);
    }
}

/**
 * Gets the raw user-data DDT entry (status << 60 | pointer) of a sector.
 */
static int32_t twin_get_raw_ddt_entry(aaruformat_context *ctx, const uint64_t sector_address, const bool negative,
                                      uint64_t *entry)
{
    *entry = 0;

    if(ctx->ddt_version == 1)
    {
        uint64_t offset = 0, block_offset = 0;
        uint8_t  status = SectorStatusNotDumped;

        int32_t res = decode_ddt_entry_v1(ctx, sector_address, &offset, &block_offset, &status);
        if(res != AARUF_STATUS_OK) return res;

        // Only the status is meaningful for V1 images
        *entry = status == SectorStatusNotDumped ? 0 : (uint64_t)status << 60 | 1;
        return AARUF_STATUS_OK;
    }

    // Safe while writing too: the multi-level decoder never disturbs the table being written
    uint64_t offset = 0, block_offset = 0;
    uint8_t  status = SectorStatusNotDumped;

    const int32_t res = decode_ddt_entry_v2(ctx, sector_address, negative, &offset, &block_offset, &status);
    if(res != AARUF_STATUS_OK) return res;

    if(status == SectorStatusNotDumped && offset == 0 && block_offset == 0) return AARUF_STATUS_OK;

    *entry = (offset & (1ULL << ctx->user_data_ddt_header.dataShift) - 1 |
              block_offset >> ctx->user_data_ddt_header.blockAlignmentShift << ctx->user_data_ddt_header.dataShift) |
             (uint64_t)status << 60;
    return AARUF_STATUS_OK;
}

static int32_t twin_validate_context(void *context, const bool writing, aaruformat_context **out)
{
    if(context == NULL) return AARUF_ERROR_NOT_AARUFORMAT;

    aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC) return AARUF_ERROR_NOT_AARUFORMAT;

    if(writing && !ctx->is_writing) return AARUF_READ_ONLY;

    if(ctx->no_user_data_ddt) return AARUF_ERROR_USER_DATA_NOT_PRESENT;

    *out = ctx;
    return AARUF_STATUS_OK;
}

static int32_t twin_validate_address(const aaruformat_context *ctx, const uint64_t sector_address, const bool negative)
{
    if(negative && sector_address > ctx->user_data_ddt_header.negative) return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;

    if(!negative && sector_address > ctx->image_info.Sectors + ctx->user_data_ddt_header.overflow - 1)
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;

    return AARUF_STATUS_OK;
}

static void twin_free_group(TwinSectorGroupEntry *group)
{
    if(group == NULL) return;

    free(group->instances);
    free(group);
}

void free_twin_sectors(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    TwinSectorGroupEntry *group = NULL;
    TwinSectorGroupEntry *tmp   = NULL;

    HASH_ITER(hh, ctx->twin_sectors, group, tmp)
    {
        HASH_DEL(ctx->twin_sectors, group);
        twin_free_group(group);
    }

    ctx->twin_sectors = NULL;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Public API                                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------ */

/**
 * @brief Gets how many physical instances of a sector are stored in the image.
 *
 * @param context Image context.
 * @param sector_address Sector address.
 * @param negative True for a negative sector.
 * @param count Receives 0 when the sector was not dumped, 1 for a normal sector, or the number of twins (>= 2).
 *
 * @retval AARUF_STATUS_OK Count returned.
 * @retval AARUF_ERROR_NOT_AARUFORMAT Invalid context.
 * @retval AARUF_ERROR_SECTOR_OUT_OF_BOUNDS Address outside the image.
 * @retval AARUF_ERROR_USER_DATA_NOT_PRESENT Flux-only image.
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE count is NULL.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_sector_twin_count(const void *context, const uint64_t sector_address,
                                                          const bool negative, uint32_t *count)
{
    TRACE("Entering aaruf_get_sector_twin_count(%p, %" PRIu64 ", %d)", context, sector_address, negative);

    aaruformat_context *ctx = NULL;
    int32_t             res = twin_validate_context((void *)context, false, &ctx);

    if(res != AARUF_STATUS_OK) return res;
    if(count == NULL) return AARUF_ERROR_INCORRECT_DATA_SIZE;

    res = twin_validate_address(ctx, sector_address, negative);
    if(res != AARUF_STATUS_OK) return res;

    if(negative && ctx->ddt_version == 1) return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;

    const TwinSectorGroupEntry *group = twin_find_group(ctx, twin_internal_index(ctx, sector_address, negative));

    if(group != NULL)
    {
        *count = group->count;
        TRACE("Exiting aaruf_get_sector_twin_count() = AARUF_STATUS_OK (%u)", *count);
        return AARUF_STATUS_OK;
    }

    uint64_t entry = 0;
    res            = twin_get_raw_ddt_entry(ctx, sector_address, negative, &entry);
    if(res != AARUF_STATUS_OK) return res;

    *count = entry == 0 || entry >> 60 == SectorStatusNotDumped ? 0 : 1;

    TRACE("Exiting aaruf_get_sector_twin_count() = AARUF_STATUS_OK (%u)", *count);
    return AARUF_STATUS_OK;
}

typedef enum
{
    kTwinOpData = 0,
    kTwinOpLong = 1,
    kTwinOpTag  = 2
} TwinOperation;

static int32_t twin_read_common(void *context, const uint64_t sector_address, const bool negative,
                                const uint32_t twin_index, const TwinOperation operation, uint8_t *data,
                                uint32_t *length, uint8_t *sector_status, const int32_t tag)
{
    if(twin_index == 0)
    {
        switch(operation)
        {
            case kTwinOpData:
                return aaruf_read_sector(context, sector_address, negative, data, length, sector_status);
            case kTwinOpLong:
                return aaruf_read_sector_long(context, sector_address, negative, data, length, sector_status);
            default:
                return aaruf_read_sector_tag(context, sector_address, negative, data, length, tag);
        }
    }

    aaruformat_context *ctx = NULL;
    int32_t             res = twin_validate_context(context, false, &ctx);

    if(res != AARUF_STATUS_OK) return res;
    if(length == NULL) return AARUF_ERROR_INCORRECT_DATA_SIZE;
    if(operation != kTwinOpTag && sector_status == NULL) return AARUF_ERROR_INCORRECT_DATA_SIZE;

    res = twin_validate_address(ctx, sector_address, negative);
    if(res != AARUF_STATUS_OK) return res;

    const uint64_t        index = twin_internal_index(ctx, sector_address, negative);
    TwinSectorGroupEntry *group = twin_find_group(ctx, index);

    if(group == NULL || twin_index >= group->count) return AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE;

    TwinInstance  *instance = &group->instances[twin_index - 1];
    TwinSlotBackup backup;

    twin_swap_in(ctx, index, instance, &backup);

    ctx->twin_read_active   = true;
    ctx->twin_read_address  = sector_address;
    ctx->twin_read_negative = negative;
    ctx->twin_read_entry    = instance->present & 1U << kTwinCompData ? instance->data : 0;

    switch(operation)
    {
        case kTwinOpData:
            res = aaruf_read_sector(context, sector_address, negative, data, length, sector_status);
            break;
        case kTwinOpLong:
            res = aaruf_read_sector_long(context, sector_address, negative, data, length, sector_status);
            break;
        default:
            res = aaruf_read_sector_tag(context, sector_address, negative, data, length, tag);
            break;
    }

    ctx->twin_read_active = false;

    twin_restore(ctx, index, &backup);

    return res;
}

/**
 * @brief Reads the user data of one physical instance of a sector.
 *
 * Same contract as aaruf_read_sector(). twin_index 0 is exactly aaruf_read_sector(); for a non-twinned sector only
 * index 0 is valid.
 *
 * @retval AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE The instance does not exist.
 * @return Any value returned by aaruf_read_sector().
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_twin(void *context, const uint64_t sector_address, const bool negative,
                                                     const uint32_t twin_index, uint8_t *data, uint32_t *length,
                                                     uint8_t *sector_status)
{
    TRACE("Entering aaruf_read_sector_twin(%p, %" PRIu64 ", %d, %u)", context, sector_address, negative, twin_index);
    const int32_t res =
        twin_read_common(context, sector_address, negative, twin_index, kTwinOpData, data, length, sector_status, 0);
    TRACE("Exiting aaruf_read_sector_twin() = %d", res);
    return res;
}

/**
 * @brief Reads one physical instance of a sector in long (raw) form.
 *
 * Same contract as aaruf_read_sector_long(), the sector is rebuilt from the twin's own components (prefix, suffix,
 * subheader, DVD/BD fields, tags). twin_index 0 is exactly aaruf_read_sector_long().
 *
 * @retval AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE The instance does not exist.
 * @return Any value returned by aaruf_read_sector_long().
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_twin_long(void *context, const uint64_t sector_address,
                                                          const bool negative, const uint32_t twin_index, uint8_t *data,
                                                          uint32_t *length, uint8_t *sector_status)
{
    TRACE("Entering aaruf_read_sector_twin_long(%p, %" PRIu64 ", %d, %u)", context, sector_address, negative,
          twin_index);
    const int32_t res =
        twin_read_common(context, sector_address, negative, twin_index, kTwinOpLong, data, length, sector_status, 0);
    TRACE("Exiting aaruf_read_sector_twin_long() = %d", res);
    return res;
}

/**
 * @brief Reads a sector tag of one physical instance of a sector.
 *
 * Same contract as aaruf_read_sector_tag(). Track-level tags (flags, ISRC) are shared by all instances.
 * twin_index 0 is exactly aaruf_read_sector_tag().
 *
 * @retval AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE The instance does not exist.
 * @return Any value returned by aaruf_read_sector_tag().
 */
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_twin_tag(const void *context, const uint64_t sector_address,
                                                         const bool negative, const uint32_t twin_index,
                                                         uint8_t *buffer, uint32_t *length, const int32_t tag)
{
    TRACE("Entering aaruf_read_sector_twin_tag(%p, %" PRIu64 ", %d, %u, %d)", context, sector_address, negative,
          twin_index, tag);
    const int32_t res =
        twin_read_common((void *)context, sector_address, negative, twin_index, kTwinOpTag, buffer, length, NULL, tag);
    TRACE("Exiting aaruf_read_sector_twin_tag() = %d", res);
    return res;
}

/** Marks a sector as twinned in the user DDT, creating its group. */
static int32_t twin_create_group(aaruformat_context *ctx, const uint64_t sector_address, const bool negative,
                                 const uint64_t index, TwinSectorGroupEntry **out)
{
    uint64_t raw = 0;
    int32_t  res = twin_get_raw_ddt_entry(ctx, sector_address, negative, &raw);

    if(res != AARUF_STATUS_OK) return res;

    TwinSectorGroupEntry *group = calloc(1, sizeof(TwinSectorGroupEntry));
    if(group == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

    group->sector       = index;
    group->count        = 1;
    group->twin0_status = raw == 0 ? SectorStatusNotDumped : (uint8_t)(raw >> 60);

    if(group->twin0_status == SectorStatusTwin) group->twin0_status = SectorStatusDumped;

    uint64_t entry = raw & TWIN_ENTRY_MASK;
    if(!set_ddt_entry_v2(ctx, sector_address, negative, 0, 0, SectorStatusTwin, &entry))
    {
        free(group);
        return AARUF_ERROR_CANNOT_SET_DDT_ENTRY;
    }

    HASH_ADD(hh, ctx->twin_sectors, sector, sizeof(uint64_t), group);
    ctx->header.featureCompatibleRo |= AARU_FEATURE_ROCOMPAT_TWIN_SECTORS;

    *out = group;
    return AARUF_STATUS_OK;
}

static int32_t twin_write_common(void *context, const uint64_t sector_address, const bool negative,
                                 const uint32_t twin_index, const TwinOperation operation, const uint8_t *data,
                                 const uint8_t sector_status, const uint32_t length, const int32_t tag)
{
    if(twin_index == 0)
    {
        switch(operation)
        {
            case kTwinOpData:
                return aaruf_write_sector(context, sector_address, negative, data, sector_status, length);
            case kTwinOpLong:
                return aaruf_write_sector_long(context, sector_address, negative, data, sector_status, length);
            default:
                return aaruf_write_sector_tag(context, sector_address, negative, data, length, tag);
        }
    }

    aaruformat_context *ctx = NULL;
    int32_t             res = twin_validate_context(context, true, &ctx);

    if(res != AARUF_STATUS_OK) return res;

    if(ctx->is_tape) return AARUF_ERROR_INCORRECT_MEDIA_TYPE;

    res = twin_validate_address(ctx, sector_address, negative);
    if(res != AARUF_STATUS_OK) return res;

    const uint64_t        index = twin_internal_index(ctx, sector_address, negative);
    TwinSectorGroupEntry *group = twin_find_group(ctx, index);
    const uint32_t        count = group != NULL ? group->count : 1;

    if(twin_index > count || twin_index >= UINT16_MAX) return AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE;


    TwinInstance instance;
    memset(&instance, 0, sizeof(TwinInstance));
    if(twin_index < count) instance = group->instances[twin_index - 1];

    TwinSlotBackup backup;
    twin_swap_in(ctx, index, &instance, &backup);

    ctx->twin_write_active    = true;
    ctx->twin_write_entry_set = false;
    ctx->twin_write_entry     = 0;

    switch(operation)
    {
        case kTwinOpData:
            res = aaruf_write_sector(context, sector_address, negative, data, sector_status, length);
            break;
        case kTwinOpLong:
            res = aaruf_write_sector_long(context, sector_address, negative, data, sector_status, length);
            break;
        default:
            res = aaruf_write_sector_tag(context, sector_address, negative, data, length, tag);
            break;
    }

    ctx->twin_write_active = false;

    if(res == AARUF_STATUS_OK)
    {
        twin_capture(ctx, index, &instance);

        if(ctx->twin_write_entry_set)
        {
            instance.data = ctx->twin_write_entry;
            instance.present |= 1U << kTwinCompData;
        }
    }

    twin_restore(ctx, index, &backup);

    if(res != AARUF_STATUS_OK) return res;

    if(group == NULL)
    {
        res = twin_create_group(ctx, sector_address, negative, index, &group);
        if(res != AARUF_STATUS_OK) return res;
    }

    if(twin_index == group->count)
    {
        if(group->count - 1 >= group->capacity)
        {
            const uint16_t new_capacity = group->capacity == 0 ? 2 : (uint16_t)(group->capacity * 2);
            TwinInstance  *grown        = realloc(group->instances, new_capacity * sizeof(TwinInstance));

            if(grown == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

            group->instances = grown;
            group->capacity  = new_capacity;
        }

        group->count++;
    }

    group->instances[twin_index - 1] = instance;
    ctx->dirty_twin_table            = true;

    return AARUF_STATUS_OK;
}

/**
 * @brief Writes the user data of one physical instance of a sector.
 *
 * Same contract as aaruf_write_sector(). twin_index 0 is exactly aaruf_write_sector().
 * - twin_index <  count: replaces that instance.
 * - twin_index == count: appends a new instance.
 * - twin_index >  count: AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE.
 *
 * Twins do not contribute to the image checksums, which are computed over twin #0.
 *
 * @return Any value returned by aaruf_write_sector(), or AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_write_sector_twin(void *context, const uint64_t sector_address, const bool negative,
                                                      const uint32_t twin_index, const uint8_t *data,
                                                      const uint8_t sector_status, const uint32_t length)
{
    TRACE("Entering aaruf_write_sector_twin(%p, %" PRIu64 ", %d, %u, %u, %u)", context, sector_address, negative,
          twin_index, sector_status, length);
    const int32_t res =
        twin_write_common(context, sector_address, negative, twin_index, kTwinOpData, data, sector_status, length, 0);
    TRACE("Exiting aaruf_write_sector_twin() = %d", res);
    return res;
}

/**
 * @brief Writes one physical instance of a sector in long (raw) form.
 *
 * Same contract as aaruf_write_sector_long(): the raw sector is decomposed exactly as twin #0 would be (CD prefix
 * and suffix regenerated or stored, subheader, DVD/BD fields, Apple/Priam tags), but every component is kept for
 * this instance. Index rules as aaruf_write_sector_twin().
 *
 * @return Any value returned by aaruf_write_sector_long(), or AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_write_sector_twin_long(void *context, const uint64_t sector_address,
                                                           const bool negative, const uint32_t twin_index,
                                                           const uint8_t *data, const uint8_t sector_status,
                                                           const uint32_t length)
{
    TRACE("Entering aaruf_write_sector_twin_long(%p, %" PRIu64 ", %d, %u, %u, %u)", context, sector_address, negative,
          twin_index, sector_status, length);
    const int32_t res =
        twin_write_common(context, sector_address, negative, twin_index, kTwinOpLong, data, sector_status, length, 0);
    TRACE("Exiting aaruf_write_sector_twin_long() = %d", res);
    return res;
}

/**
 * @brief Writes a sector tag of one physical instance of a sector.
 *
 * Same contract as aaruf_write_sector_tag(). Track-level tags (flags, ISRC) are shared by all instances.
 * Index rules as aaruf_write_sector_twin().
 *
 * @return Any value returned by aaruf_write_sector_tag(), or AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_write_sector_twin_tag(void *context, const uint64_t sector_address,
                                                          const bool negative, const uint32_t twin_index,
                                                          const uint8_t *data, const size_t length, const int32_t tag)
{
    TRACE("Entering aaruf_write_sector_twin_tag(%p, %" PRIu64 ", %d, %u, %d)", context, sector_address, negative,
          twin_index, tag);

    if(length > UINT32_MAX) return AARUF_ERROR_INCORRECT_DATA_SIZE;

    const int32_t res =
        twin_write_common(context, sector_address, negative, twin_index, kTwinOpTag, data, 0, (uint32_t)length, tag);
    TRACE("Exiting aaruf_write_sector_twin_tag() = %d", res);
    return res;
}

/**
 * @brief Removes all twins of a sector, keeping twin #0 as a normal sector.
 *
 * @retval AARUF_STATUS_OK Twins removed, or the sector had none.
 * @retval AARUF_READ_ONLY Image not opened for writing.
 * @retval AARUF_ERROR_CANNOT_SET_DDT_ENTRY The DDT could not be updated.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_sector_twins(void *context, const uint64_t sector_address,
                                                       const bool negative)
{
    TRACE("Entering aaruf_clear_sector_twins(%p, %" PRIu64 ", %d)", context, sector_address, negative);

    aaruformat_context *ctx = NULL;
    int32_t             res = twin_validate_context(context, true, &ctx);

    if(res != AARUF_STATUS_OK) return res;

    res = twin_validate_address(ctx, sector_address, negative);
    if(res != AARUF_STATUS_OK) return res;

    TwinSectorGroupEntry *group = twin_find_group(ctx, twin_internal_index(ctx, sector_address, negative));
    if(group == NULL) return AARUF_STATUS_OK;


    uint64_t raw = 0;
    res          = twin_get_raw_ddt_entry(ctx, sector_address, negative, &raw);
    if(res != AARUF_STATUS_OK) return res;

    uint64_t entry = raw & TWIN_ENTRY_MASK;

    if(entry == 0 && group->twin0_status == SectorStatusNotDumped)
    {
        // Twin #0 never written: leave the entry empty
        if(!set_ddt_entry_v2(ctx, sector_address, negative, 0, 0, SectorStatusNotDumped, &entry))
            return AARUF_ERROR_CANNOT_SET_DDT_ENTRY;
    }
    else if(!set_ddt_entry_v2(ctx, sector_address, negative, 0, 0, group->twin0_status, &entry))
        return AARUF_ERROR_CANNOT_SET_DDT_ENTRY;

    HASH_DEL(ctx->twin_sectors, group);
    twin_free_group(group);
    ctx->dirty_twin_table = true;

    TRACE("Exiting aaruf_clear_sector_twins() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

static int twin_compare_groups(const void *a, const void *b)
{
    const TwinSectorGroupEntry *ga = *(TwinSectorGroupEntry *const *)a;
    const TwinSectorGroupEntry *gb = *(TwinSectorGroupEntry *const *)b;

    return ga->sector < gb->sector ? -1 : ga->sector > gb->sector ? 1 : 0;
}

/** Returns the groups sorted by sector (caller frees the array, not the groups). */
static TwinSectorGroupEntry **twin_sorted_groups(const aaruformat_context *ctx, uint32_t *count)
{
    *count = HASH_COUNT(ctx->twin_sectors);
    if(*count == 0) return NULL;

    TwinSectorGroupEntry **sorted = malloc(*count * sizeof(TwinSectorGroupEntry *));
    if(sorted == NULL) return NULL;

    uint32_t              i     = 0;
    TwinSectorGroupEntry *group = NULL;
    TwinSectorGroupEntry *tmp   = NULL;

    HASH_ITER(hh, ctx->twin_sectors, group, tmp) sorted[i++] = group;

    qsort(sorted, *count, sizeof(TwinSectorGroupEntry *), twin_compare_groups);
    return sorted;
}

/**
 * @brief Lists every twinned sector of the image, sorted by address.
 *
 * Call with buffer NULL to get the required size in *length.
 *
 * @param context Image context.
 * @param buffer Array of TwinSectorInfo, or NULL.
 * @param length In: size of buffer in bytes. Out: bytes needed / written.
 *
 * @retval AARUF_STATUS_OK List written (also when the image has no twins: *length = 0).
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL buffer is NULL or too small; *length holds the size needed.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_twin_sectors(const void *context, TwinSectorInfo *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_twin_sectors(%p, %p)", context, buffer);

    aaruformat_context *ctx = NULL;
    int32_t             res = twin_validate_context((void *)context, false, &ctx);

    if(res != AARUF_STATUS_OK) return res;
    if(length == NULL) return AARUF_ERROR_INCORRECT_DATA_SIZE;

    const size_t needed = (size_t)HASH_COUNT(ctx->twin_sectors) * sizeof(TwinSectorInfo);

    if(needed == 0)
    {
        *length = 0;
        return AARUF_STATUS_OK;
    }

    if(buffer == NULL || *length < needed)
    {
        *length = needed;
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    uint32_t               count  = 0;
    TwinSectorGroupEntry **sorted = twin_sorted_groups(ctx, &count);
    if(sorted == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

    const uint64_t negatives = ctx->user_data_ddt_header.negative;

    for(uint32_t i = 0; i < count; i++)
    {
        memset(&buffer[i], 0, sizeof(TwinSectorInfo));
        buffer[i].count = sorted[i]->count;

        if(sorted[i]->sector < negatives)
        {
            buffer[i].negative       = 1;
            buffer[i].sector_address = negatives - sorted[i]->sector;
        }
        else
            buffer[i].sector_address = sorted[i]->sector - negatives;
    }

    free(sorted);
    *length = needed;

    TRACE("Exiting aaruf_get_twin_sectors() = AARUF_STATUS_OK (%u)", count);
    return AARUF_STATUS_OK;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Serialization                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

static uint16_t twin_component_data_type(const aaruformat_context *ctx, const int comp)
{
    switch(comp)
    {
        case kTwinCompData:
            return kDataTypeUserData;
        case kTwinCompPrefix:
            return kDataTypeCdSectorPrefix;
        case kTwinCompSuffix:
            return kDataTypeCdSectorSuffix;
        case kTwinCompSubheader:
            return kDataTypeCdSubHeader;
        case kTwinCompSubchannel:
            return kDataTypeCdSubchannel;
        case kTwinCompDvdId:
            return kDataTypeDvdSectorId;
        case kTwinCompDvdIed:
            return kDataTypeDvdSectorIed;
        case kTwinCompDvdCprMai:
            return kDataTypeDvdSectorCprMai;
        case kTwinCompSectorEdc:
            return kDataTypeDvdSectorEdc;
        case kTwinCompTitleKey:
            return kDataTypeDvdTitleKeyDecrypted;
        case kTwinCompTag:
            return twin_tag_data_type(twin_tag_size(ctx));
        default:
            return kDataTypeNone;
    }
}

/** Maps a descriptor to an internal component, -1 if unknown or malformed. */
static int twin_component_from_descriptor(const TwinComponentDescriptor *descriptor)
{
    int      comp;
    uint8_t  kind = kTwinComponentInline;
    uint32_t size = 0;

    switch(descriptor->dataType)
    {
        case kDataTypeUserData:
            comp = kTwinCompData;
            kind = kTwinComponentPointer;
            size = 8;
            break;
        case kDataTypeCdSectorPrefix:
            comp = kTwinCompPrefix;
            kind = kTwinComponentPointer;
            size = 8;
            break;
        case kDataTypeCdSectorSuffix:
            comp = kTwinCompSuffix;
            kind = kTwinComponentPointer;
            size = 8;
            break;
        case kDataTypeCdSubHeader:
            comp = kTwinCompSubheader;
            size = 8;
            break;
        case kDataTypeCdSubchannel:
            comp = kTwinCompSubchannel;
            size = 96;
            break;
        case kDataTypeDvdSectorId:
            comp = kTwinCompDvdId;
            size = 4;
            break;
        case kDataTypeDvdSectorIed:
            comp = kTwinCompDvdIed;
            size = 2;
            break;
        case kDataTypeDvdSectorCprMai:
            comp = kTwinCompDvdCprMai;
            size = 6;
            break;
        case kDataTypeDvdSectorEdc:
        case kDataTypeBdSectorEdc:
            comp = kTwinCompSectorEdc;
            size = 4;
            break;
        case kDataTypeDvdTitleKeyDecrypted:
            comp = kTwinCompTitleKey;
            size = 5;
            break;
        case kDataTypeAppleSonyTag:
            comp = kTwinCompTag;
            size = 12;
            break;
        case kDataTypeAppleProfileTag:
            comp = kTwinCompTag;
            size = 20;
            break;
        case kDataTypePriamDataTowerTag:
            comp = kTwinCompTag;
            size = 24;
            break;
        default:
            return -1;
    }

    if(descriptor->kind != kind || descriptor->size != size) return -1;

    return comp;
}

/**
 * Parses a complete TWTB block held in memory (header + on-disk payload).
 *
 * @param out Receives a new hash of groups (caller frees with free_twin_group_hash()).
 */
static int32_t twin_parse_block(const aaruformat_context *ctx, const uint8_t *raw, const size_t raw_length,
                                TwinSectorGroupEntry **out)
{
    TwinSectorTableHeader header;

    *out = NULL;

    if(raw_length < sizeof(TwinSectorTableHeader)) return AARUF_ERROR_CANNOT_READ_BLOCK;

    memcpy(&header, raw, sizeof(TwinSectorTableHeader));

    if(header.identifier != TwinSectorTable) return AARUF_ERROR_CANNOT_READ_BLOCK;

    if(header.cmpLength > raw_length - sizeof(TwinSectorTableHeader)) return AARUF_ERROR_CANNOT_READ_BLOCK;

    if(header.components == 0 || header.recordSize < 8 || header.recordSize % 8 != 0 || header.length == 0 ||
       header.length > UINT32_MAX)
        return AARUF_ERROR_CANNOT_READ_BLOCK;

    const uint8_t *cmp        = raw + sizeof(TwinSectorTableHeader);
    size_t         cmp_length = (size_t)header.cmpLength;
    const uint8_t *props      = NULL;

    if(header.compression == kCompressionLzma)
    {
        if(cmp_length <= LZMA_PROPERTIES_LENGTH) return AARUF_ERROR_CANNOT_READ_BLOCK;

        props = cmp;
        cmp += LZMA_PROPERTIES_LENGTH;
        cmp_length -= LZMA_PROPERTIES_LENGTH;
    }

    if(aaruf_crc64_data(cmp, cmp_length) != header.cmpCrc64) return AARUF_ERROR_INVALID_BLOCK_CRC;

    uint8_t *payload = malloc((size_t)header.length);
    if(payload == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

    int32_t res = AARUF_STATUS_OK;

    switch(header.compression)
    {
        case kCompressionNone:
            if(cmp_length != header.length)
                res = AARUF_ERROR_CANNOT_READ_BLOCK;
            else
                memcpy(payload, cmp, cmp_length);
            break;
        case kCompressionLzma:
        {
            size_t dst_size = (size_t)header.length;
            size_t src_size = cmp_length;
            if(aaruf_lzma_decode_buffer(payload, &dst_size, cmp, &src_size, props, LZMA_PROPERTIES_LENGTH) != 0 ||
               dst_size != header.length)
                res = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            break;
        }
        case kCompressionZstd:
            if(aaruf_zstd_decode_buffer(payload, (size_t)header.length, cmp, cmp_length) != header.length)
                res = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            break;
        default:
            res = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
            break;
    }

    if(res == AARUF_STATUS_OK && aaruf_crc64_data(payload, (size_t)header.length) != header.crc64)
        res = AARUF_ERROR_INVALID_BLOCK_CRC;

    const uint64_t descriptors_size = (uint64_t)header.components * sizeof(TwinComponentDescriptor);
    const uint64_t groups_size      = (uint64_t)header.groups * sizeof(TwinSectorGroup);
    const uint64_t records_size     = header.instances * header.recordSize;

    if(res == AARUF_STATUS_OK &&
       (header.instances > UINT32_MAX || descriptors_size + groups_size + records_size != header.length))
        res = AARUF_ERROR_CANNOT_READ_BLOCK;

    if(res != AARUF_STATUS_OK)
    {
        free(payload);
        return res;
    }

    const TwinComponentDescriptor *descriptors = (const TwinComponentDescriptor *)payload;
    const uint8_t                 *groups_raw  = payload + descriptors_size;
    const uint8_t                 *records     = groups_raw + groups_size;

    // Map descriptors to internal components and record offsets
    int      map[256];
    uint32_t offsets[256];
    uint32_t position = 8;

    for(uint32_t i = 0; i < header.components; i++)
    {
        map[i]     = twin_component_from_descriptor(&descriptors[i]);
        offsets[i] = position;
        position += descriptors[i].size;

        if(map[i] == kTwinCompTag && descriptors[i].size != twin_tag_size(ctx)) map[i] = -1;
    }

    if(position > header.recordSize || map[0] != kTwinCompData)
    {
        free(payload);
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    const uint64_t        total  = twin_total_sectors(ctx);
    TwinSectorGroupEntry *result = NULL;

    for(uint32_t g = 0; g < header.groups && res == AARUF_STATUS_OK; g++)
    {
        TwinSectorGroup disk_group;
        memcpy(&disk_group, groups_raw + g * sizeof(TwinSectorGroup), sizeof(TwinSectorGroup));

        TwinSectorGroupEntry *existing = NULL;
        HASH_FIND(hh, result, &disk_group.sector, sizeof(uint64_t), existing);

        if(disk_group.count < 2 || disk_group.sector >= total || existing != NULL ||
           (uint64_t)disk_group.first + disk_group.count - 1 > header.instances)
        {
            res = AARUF_ERROR_CANNOT_READ_BLOCK;
            break;
        }

        TwinSectorGroupEntry *group = calloc(1, sizeof(TwinSectorGroupEntry));
        if(group != NULL) group->instances = calloc(disk_group.count - 1, sizeof(TwinInstance));

        if(group == NULL || group->instances == NULL)
        {
            twin_free_group(group);
            res = AARUF_ERROR_NOT_ENOUGH_MEMORY;
            break;
        }

        group->sector       = disk_group.sector;
        group->count        = disk_group.count;
        group->capacity     = disk_group.count - 1;
        group->twin0_status = disk_group.twin0Status;

        for(uint32_t t = 0; t < (uint32_t)disk_group.count - 1; t++)
        {
            const uint8_t *record = records + (uint64_t)(disk_group.first + t) * header.recordSize;
            TwinInstance  *inst   = &group->instances[t];
            uint32_t       present;

            memcpy(&present, record, sizeof(uint32_t));

            for(uint32_t i = 0; i < header.components && i < 32; i++)
            {
                if(map[i] < 0 || (present & 1U << i) == 0) continue;

                memcpy(twin_instance_field(inst, map[i]), record + offsets[i], descriptors[i].size);
                inst->present |= 1U << map[i];
            }
        }

        HASH_ADD(hh, result, sector, sizeof(uint64_t), group);
    }

    free(payload);

    if(res != AARUF_STATUS_OK)
    {
        TwinSectorGroupEntry *group = NULL;
        TwinSectorGroupEntry *tmp   = NULL;
        HASH_ITER(hh, result, group, tmp)
        {
            HASH_DEL(result, group);
            twin_free_group(group);
        }
        return res;
    }

    *out = result;
    return AARUF_STATUS_OK;
}

/** Reads a complete TWTB block (header + payload) from the file. */
static int32_t twin_read_raw_block(aaruformat_context *ctx, const uint64_t offset, uint8_t **raw, size_t *raw_length)
{
    TwinSectorTableHeader header;

    *raw        = NULL;
    *raw_length = 0;

    if(aaruf_fseek(ctx->imageStream, (aaru_off_t)offset, SEEK_SET) != 0 ||
       fread(&header, 1, sizeof(TwinSectorTableHeader), ctx->imageStream) != sizeof(TwinSectorTableHeader))
        return AARUF_ERROR_CANNOT_READ_BLOCK;

    if(header.identifier != TwinSectorTable || header.cmpLength > UINT32_MAX) return AARUF_ERROR_CANNOT_READ_BLOCK;

    const size_t length = sizeof(TwinSectorTableHeader) + (size_t)header.cmpLength;
    uint8_t     *buffer = malloc(length);
    if(buffer == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

    memcpy(buffer, &header, sizeof(TwinSectorTableHeader));

    if(fread(buffer + sizeof(TwinSectorTableHeader), 1, (size_t)header.cmpLength, ctx->imageStream) != header.cmpLength)
    {
        free(buffer);
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *raw        = buffer;
    *raw_length = length;
    return AARUF_STATUS_OK;
}

/** Loads and parses the TWTB at offset, trying erasure coding recovery when the block is damaged. */
static int32_t twin_load(aaruformat_context *ctx, const uint64_t offset, TwinSectorGroupEntry **out)
{
    uint8_t *raw        = NULL;
    size_t   raw_length = 0;
    int32_t  res        = twin_read_raw_block(ctx, offset, &raw, &raw_length);

    if(res == AARUF_STATUS_OK) res = twin_parse_block(ctx, raw, raw_length, out);

    free(raw);

    if(res == AARUF_STATUS_OK || !ctx->ec_recovery_available) return res;

    uint8_t *recovered      = NULL;
    uint32_t recovered_size = 0;

    if(ec_recover_meta_block(ctx, offset, &recovered, &recovered_size) != AARUF_STATUS_OK || recovered == NULL)
        return res;

    TRACE("EC recovery succeeded for twin sector table at offset %" PRIu64, offset);

    res = twin_parse_block(ctx, recovered, recovered_size, out);
    free(recovered);
    return res;
}

/**
 * @brief Loads the twin sector table indexed by entry into the context.
 *
 * A damaged table is not fatal: twin #0 of every sector stays readable, so the image is opened without twins.
 */
void process_twin_sector_table(aaruformat_context *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_twin_sector_table(%p, %" PRIu64 ")", ctx, entry->offset);

    TwinSectorGroupEntry *groups = NULL;
    const int32_t         res    = twin_load(ctx, entry->offset, &groups);

    if(res != AARUF_STATUS_OK)
    {
        FATAL("Could not load twin sector table at %" PRIu64 " (%d), twins will not be available", entry->offset, res);
        return;
    }

    free_twin_sectors(ctx);
    ctx->twin_sectors = groups;

    TRACE("Exiting process_twin_sector_table(): %u twinned sectors", HASH_COUNT(ctx->twin_sectors));
}

/**
 * @brief Verifies the twin sector table at offset: CRCs, structure, DDT consistency and arena bounds.
 */
int32_t verify_twin_sector_table(aaruformat_context *ctx, const uint64_t offset)
{
    TwinSectorGroupEntry *groups = NULL;
    int32_t               res    = twin_load(ctx, offset, &groups);

    if(res != AARUF_STATUS_OK) return res;

    TwinSectorGroupEntry *group = NULL;
    TwinSectorGroupEntry *tmp   = NULL;
    const uint64_t        negs  = ctx->user_data_ddt_header.negative;

    HASH_ITER(hh, groups, group, tmp)
    {
        if(res != AARUF_STATUS_OK) break;

        const bool     negative = group->sector < negs;
        const uint64_t address  = negative ? negs - group->sector : group->sector - negs;
        uint64_t       raw      = 0;

        if(twin_get_raw_ddt_entry(ctx, address, negative, &raw) != AARUF_STATUS_OK || raw >> 60 != SectorStatusTwin)
        {
            FATAL("Twinned sector %" PRIu64 " is not marked as twin in the DDT", group->sector);
            res = AARUF_ERROR_INVALID_BLOCK_CRC;
            break;
        }

        for(uint16_t t = 0; t + 1 < group->count; t++)
        {
            const TwinInstance *inst = &group->instances[t];

            if(inst->present & 1U << kTwinCompPrefix && inst->prefix >> 60 == SectorStatusErrored &&
               ((inst->prefix & TWIN_ENTRY_MASK) + 1) * 16 > ctx->sector_prefix_length)
            {
                FATAL("Twin of sector %" PRIu64 " references a prefix outside the arena", group->sector);
                res = AARUF_ERROR_INVALID_BLOCK_CRC;
            }

            if(inst->present & 1U << kTwinCompSuffix && inst->suffix >> 60 == SectorStatusErrored &&
               ((inst->suffix & TWIN_ENTRY_MASK) + 1) * 288 > ctx->sector_suffix_length)
            {
                FATAL("Twin of sector %" PRIu64 " references a suffix outside the arena", group->sector);
                res = AARUF_ERROR_INVALID_BLOCK_CRC;
            }
        }
    }

    HASH_ITER(hh, groups, group, tmp)
    {
        HASH_DEL(groups, group);
        twin_free_group(group);
    }

    if(res != AARUF_STATUS_OK || ctx->ddt_version != 2) return res;

    // Every sector marked as twin must have a table entry
    const uint64_t total = twin_total_sectors(ctx);
    for(uint64_t index = 0; index < total; index++)
    {
        if(ctx->user_data_ddt_header.tableShift == 0 && ctx->user_data_ddt2 != NULL &&
           ctx->user_data_ddt2[index] >> 60 != SectorStatusTwin)
            continue;

        const bool     negative = index < negs;
        const uint64_t address  = negative ? negs - index : index - negs;

        // Negative sector 0 and positive sector 0 share an index; check it once as positive
        if(negative && address == 0) continue;

        uint64_t raw = 0;
        if(twin_get_raw_ddt_entry(ctx, address, negative, &raw) != AARUF_STATUS_OK) continue;

        if(raw >> 60 == SectorStatusTwin && twin_find_group(ctx, index) == NULL)
        {
            FATAL("Sector %" PRIu64 " is marked as twin but is not in the twin sector table", index);
            return AARUF_ERROR_INVALID_BLOCK_CRC;
        }
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Serializes the twin sector table at the end of the image and indexes it.
 *
 * Removes any previous TWTB index entry; when no twins remain the table is dropped and the feature bit cleared.
 */
int32_t write_twin_sector_table(aaruformat_context *ctx)
{
    TRACE("Entering write_twin_sector_table(%p)", ctx);

    for(int k = (int)utarray_len(ctx->index_entries) - 1; k >= 0; k--)
    {
        const IndexEntry *entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, k);
        if(entry && entry->blockType == TwinSectorTable) utarray_erase(ctx->index_entries, k, 1);
    }

    ctx->dirty_index_block = true;

    uint32_t               group_count = 0;
    TwinSectorGroupEntry **sorted      = twin_sorted_groups(ctx, &group_count);

    if(group_count == 0)
    {
        ctx->header.featureCompatibleRo &= ~(uint64_t)AARU_FEATURE_ROCOMPAT_TWIN_SECTORS;
        TRACE("Exiting write_twin_sector_table() = AARUF_STATUS_OK (no twins)");
        return AARUF_STATUS_OK;
    }

    if(sorted == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

    ctx->header.featureCompatibleRo |= AARU_FEATURE_ROCOMPAT_TWIN_SECTORS;

    // Components used by any instance; user data always first
    uint32_t used      = 1U << kTwinCompData;
    uint64_t instances = 0;

    for(uint32_t g = 0; g < group_count; g++)
    {
        instances += sorted[g]->count - 1;
        for(uint16_t t = 0; t + 1 < sorted[g]->count; t++) used |= sorted[g]->instances[t].present;
    }

    TwinComponentDescriptor descriptors[kTwinCompCount];
    int                     components[kTwinCompCount];
    uint8_t                 component_count = 0;
    uint32_t                record_size     = 8;

    for(int comp = 0; comp < kTwinCompCount; comp++)
    {
        const uint32_t size      = twin_component_size(ctx, comp);
        const uint16_t data_type = twin_component_data_type(ctx, comp);

        if((used & 1U << comp) == 0 || size == 0 || data_type == kDataTypeNone) continue;

        memset(&descriptors[component_count], 0, sizeof(TwinComponentDescriptor));
        descriptors[component_count].dataType = data_type;
        descriptors[component_count].kind     = comp <= kTwinCompSuffix ? kTwinComponentPointer : kTwinComponentInline;
        descriptors[component_count].size     = size;
        components[component_count]           = comp;
        component_count++;
        record_size += size;
    }

    record_size = record_size + 7 & ~7U;

    const size_t descriptors_size = component_count * sizeof(TwinComponentDescriptor);
    const size_t groups_size      = group_count * sizeof(TwinSectorGroup);
    const size_t payload_size     = descriptors_size + groups_size + (size_t)instances * record_size;

    uint8_t *payload = calloc(1, payload_size);
    if(payload == NULL)
    {
        free(sorted);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    memcpy(payload, descriptors, descriptors_size);

    uint32_t first = 0;
    for(uint32_t g = 0; g < group_count; g++)
    {
        TwinSectorGroup disk_group;
        memset(&disk_group, 0, sizeof(TwinSectorGroup));
        disk_group.sector      = sorted[g]->sector;
        disk_group.first       = first;
        disk_group.count       = sorted[g]->count;
        disk_group.twin0Status = sorted[g]->twin0_status;
        memcpy(payload + descriptors_size + g * sizeof(TwinSectorGroup), &disk_group, sizeof(TwinSectorGroup));

        for(uint16_t t = 0; t + 1 < sorted[g]->count; t++)
        {
            TwinInstance *inst    = &sorted[g]->instances[t];
            uint8_t      *record  = payload + descriptors_size + groups_size + (size_t)(first + t) * record_size;
            uint32_t      present = 0;
            uint32_t      offset  = 8;

            for(uint8_t i = 0; i < component_count; i++)
            {
                const uint32_t size = descriptors[i].size;

                if(inst->present & 1U << components[i])
                {
                    present |= 1U << i;
                    memcpy(record + offset, twin_instance_field(inst, components[i]), size);
                }

                offset += size;
            }

            memcpy(record, &present, sizeof(uint32_t));
        }

        first += sorted[g]->count - 1;
    }

    free(sorted);

    TwinSectorTableHeader header;
    memset(&header, 0, sizeof(TwinSectorTableHeader));
    header.identifier          = TwinSectorTable;
    header.blockAlignmentShift = ctx->user_data_ddt_header.blockAlignmentShift;
    header.dataShift           = ctx->user_data_ddt_header.dataShift;
    header.components          = component_count;
    header.recordSize          = record_size;
    header.groups              = group_count;
    header.instances           = instances;
    header.length              = payload_size;
    header.crc64               = aaruf_crc64_data(payload, payload_size);
    header.compression =
        ctx->compression_enabled ? (ctx->use_zstd ? kCompressionZstd : kCompressionLzma) : kCompressionNone;

    uint8_t *buffer                                  = payload;
    size_t   cmp_size                                = payload_size;
    uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

    if(header.compression != kCompressionNone)
    {
        buffer = calloc(1, payload_size * 2 + 1024);

        if(buffer == NULL)
            header.compression = kCompressionNone;
        else if(header.compression == kCompressionZstd)
        {
            cmp_size = aaruf_zstd_encode_buffer(buffer, payload_size * 2 + 1024, payload, payload_size, ctx->zstd_level,
                                                ctx->num_threads);
            if(cmp_size == 0) cmp_size = payload_size;
        }
        else
        {
            cmp_size          = payload_size * 2 + 1024;
            size_t props_size = LZMA_PROPERTIES_LENGTH;
            if(aaruf_lzma_encode_buffer(buffer, &cmp_size, payload, payload_size, lzma_properties, &props_size, 9,
                                        ctx->lzma_dict_size, 4, 0, 2, 273, LZMA_THREADS(ctx)) != 0)
                cmp_size = payload_size;
        }

        if(header.compression != kCompressionNone && cmp_size >= payload_size)
        {
            free(buffer);
            buffer             = payload;
            cmp_size           = payload_size;
            header.compression = kCompressionNone;
        }
    }

    header.cmpCrc64  = header.compression == kCompressionNone ? header.crc64 : aaruf_crc64_data(buffer, cmp_size);
    header.cmpLength = cmp_size + (header.compression == kCompressionLzma ? LZMA_PROPERTIES_LENGTH : 0);

    if(header.compression == kCompressionZstd) ctx->has_zstd_blocks = true;

    aaruf_fseek(ctx->imageStream, 0, SEEK_END);
    aaru_off_t     position       = aaruf_ftell(ctx->imageStream);
    const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;

    if(position & alignment_mask)
    {
        position = (aaru_off_t)((uint64_t)position + alignment_mask & ~alignment_mask);
        aaruf_fseek(ctx->imageStream, position, SEEK_SET);
    }

    bool ok = fwrite(&header, sizeof(TwinSectorTableHeader), 1, ctx->imageStream) == 1;
    if(ok && header.compression == kCompressionLzma)
        ok = fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream) == 1;
    if(ok) ok = fwrite(buffer, cmp_size, 1, ctx->imageStream) == 1;

    if(buffer != payload) free(buffer);
    free(payload);

    if(!ok)
    {
        FATAL("Could not write twin sector table");
        return AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA;
    }

    IndexEntry index_entry;
    index_entry.blockType = TwinSectorTable;
    index_entry.dataType  = kDataTypeUserData;
    index_entry.offset    = (uint64_t)position;
    utarray_push_back(ctx->index_entries, &index_entry);

    ctx->dirty_twin_table = false;

    TRACE("Exiting write_twin_sector_table() = AARUF_STATUS_OK (%u groups, %" PRIu64 " instances at %" PRIu64 ")",
          group_count, instances, (uint64_t)position);
    return AARUF_STATUS_OK;
}
