/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <errno.h>
#include <locale.h>
#include <time.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

#include <aaruformat.h>
#include <sys/types.h>

#include "aaruformattool.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

// Converts Windows FILETIME (100ns intervals since 1601-01-01) to time_t (seconds since 1970-01-01)
static const char *format_filetime(uint64_t filetime)
{
    static char    buf[64];
    const uint64_t EPOCH_DIFF = 116444736000000000ULL;
#if defined(_WIN32) || defined(_WIN64)
    FILETIME   ft;
    SYSTEMTIME st;
    ft.dwLowDateTime  = (DWORD)(filetime & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(filetime >> 32);
    if(FileTimeToSystemTime(&ft, &st))
    {
        // Format: YYYY-MM-DD HH:MM:SS
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                 st.wSecond);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%llu", filetime);
    }
    return buf;
#else
    time_t    t;
    struct tm tm_info;
    if(filetime < EPOCH_DIFF)
    {
        snprintf(buf, sizeof(buf), "%llu", filetime);
        return buf;
    }
    t = (time_t)((filetime - EPOCH_DIFF) / 10000000ULL);
    if(localtime_r(&t, &tm_info))
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    else
        snprintf(buf, sizeof(buf), "%llu", filetime);
    return buf;
#endif
}

int info(const char *path)
{
    aaruformat_context  *ctx          = NULL;
    char                *strBuffer    = NULL;
    UErrorCode           u_error_code = U_ZERO_ERROR;
    uint32_t             i            = 0;
    mediaTagEntry const *mediaTag     = NULL;
    mediaTagEntry const *tmpMediaTag  = NULL;

    ctx = aaruf_open(path, false, NULL);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.", errno);
        return errno;
    }

    printf("AaruFormat context information:\n");
    printf("Magic number: %8.8s\n", (char *)&ctx->magic);
    printf("Library version: %d.%d\n", ctx->library_major_version, ctx->library_minor_version);
    printf("AaruFormat header:\n");
    printf("\tIdentifier: %8.8s\n", (char *)&ctx->header.identifier);
    printf("\tApplication: %s\n", ctx->header.application);
    printf("\tApplication version: %d.%d\n", ctx->header.applicationMajorVersion, ctx->header.applicationMinorVersion);
    printf("\tImage format version: %d.%d\n", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    printf("\tMedia type: %u (%s)\n", ctx->header.mediaType, media_type_to_string(ctx->header.mediaType));
    printf("\tIndex offset: %llu\n", ctx->header.indexOffset);
    printf("\tCreation time: %s\n", format_filetime(ctx->header.creationTime));
    printf("\tLast written time: %s\n", format_filetime(ctx->header.lastWrittenTime));

    // TODO: Traverse media tags

    if(ctx->sector_prefix != NULL) printf("Sector prefix array has been read.\n");

    if(ctx->sector_prefix_corrected != NULL) printf("Sector prefix corrected array has been read.\n");

    if(ctx->sector_suffix != NULL) printf("Sector suffix array has been read.\n");

    if(ctx->sector_suffix_corrected != NULL) printf("Sector suffix corrected array has been read.\n");

    if(ctx->sector_subchannel != NULL) printf("Sector subchannel array has been read.\n");

    if(ctx->mode2_subheaders != NULL) printf("Sector mode 2 subheaders array has been read.\n");

    printf("Shift is %d (%d bytes).\n", ctx->shift, 1 << ctx->shift);

    if(ctx->in_memory_ddt) printf("User-data DDT resides in memory.\n");

    if(ctx->user_data_ddt != NULL) printf("User-data DDT has been read to memory.\n");

    if(ctx->mapped_memory_ddt_size > 0) printf("Mapped memory DDT has %zu bytes", ctx->mapped_memory_ddt_size);

    if(ctx->sector_prefix_ddt != NULL) printf("Sector prefix DDT has been read to memory.\n");

    if(ctx->sector_prefix_ddt != NULL) printf("Sector suffix DDT has been read to memory.\n");

    uint32_t cylinders       = 0;
    uint32_t heads           = 0;
    uint32_t sectorsPerTrack = 0;
    if(aaruf_get_geometry(ctx, &cylinders, &heads, &sectorsPerTrack) == AARUF_STATUS_OK)
        printf("Media has %d cylinders, %d heads and %d sectors per track.\n", cylinders, heads, sectorsPerTrack);

    int32_t sequence     = 0;
    int32_t lastSequence = 0;

    printf("Metadata block:\n");
    if(aaruf_get_media_sequence(ctx, &sequence, &lastSequence) == AARUF_STATUS_OK && sequence > 0)
    {
        printf("\tMedia is no. %d in a set of %d media\n", sequence, lastSequence);
    }
    int32_t length = 0;
    if(aaruf_get_creator(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_creator(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tCreator: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_comments(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_comments(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tComments: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_media_title(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_media_title(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tMedia title: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_media_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_media_manufacturer(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tMedia manufacturer: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_media_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_media_model(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tMedia model: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_media_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_media_serial_number(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tMedia serial number: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_media_barcode(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_media_barcode(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tMedia barcode: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_media_part_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_media_part_number(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tMedia part number: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_drive_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_drive_manufacturer(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tDrive manufacturer: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_drive_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_drive_model(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tDrive model: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_drive_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_drive_serial_number(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tDrive serial number: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    length = 0;
    if(aaruf_get_drive_firmware_revision(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
    {
        uint8_t *utf16Buffer = malloc(length);
        if(utf16Buffer != NULL)
        {
            if(aaruf_get_drive_firmware_revision(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
            {
                strBuffer = malloc(length + 1);
                if(strBuffer != NULL)
                {
                    memset(strBuffer, 0, length + 1);
                    u_error_code = U_ZERO_ERROR;
                    ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length, &u_error_code);
                    if(u_error_code == U_ZERO_ERROR) printf("\tDrive firmware revision: %s\n", strBuffer);
                    free(strBuffer);
                }
            }
            free(utf16Buffer);
        }
    }

    // TODO: Table format?
    if(ctx->tracks_header.identifier == TracksBlock)
    {
        printf("Tracks block:\n");
        for(i = 0; i < ctx->tracks_header.entries; i++)
        {
            printf("\tTrack entry %d:\n", i);
            printf("\t\tSequence: %d\n", ctx->track_entries[i].sequence);
            printf("\t\tType: %d\n", ctx->track_entries[i].type);
            printf("\t\tStart: %lld\n", ctx->track_entries[i].start);
            printf("\t\tEnd: %lld\n", ctx->track_entries[i].end);
            printf("\t\tPregap: %lld\n", ctx->track_entries[i].pregap);
            printf("\t\tSession: %d\n", ctx->track_entries[i].session);
            printf("\t\tISRC: %.13s\n", ctx->track_entries[i].isrc);
            printf("\t\tFlags: %d\n", ctx->track_entries[i].flags);
        }
    }

    if(ctx->cicm_block_header.identifier == CicmBlock)
    {
        printf("CICM block:\n");
        printf("%s", ctx->cicm_block);
    }

    // TODO: Table format?
    if(ctx->dump_hardware_header.identifier == DumpHardwareBlock)
    {
        printf("Dump hardware block:\n");

        for(i = 0; i < ctx->dump_hardware_header.entries; i++)
        {
            printf("\tDump hardware entry %d\n", i);

            if(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].manufacturer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength, &u_error_code);
                printf("\t\tManufacturer: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.modelLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.modelLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.modelLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer, (int)ctx->dump_hardware_entries_with_data[i].entry.modelLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].model,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.modelLength, &u_error_code);
                printf("\t\tModel: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.revisionLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.revisionLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.revisionLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.revisionLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].revision,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.revisionLength, &u_error_code);
                printf("\t\tRevision: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.firmwareLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.firmwareLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.firmwareLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.firmwareLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].firmware,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.firmwareLength, &u_error_code);
                printf("\t\tFirmware version: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.serialLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.serialLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.serialLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer, (int)ctx->dump_hardware_entries_with_data[i].entry.serialLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].serial,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.serialLength, &u_error_code);
                printf("\t\tSerial number: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].softwareName,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength, &u_error_code);
                printf("\t\tSoftware name: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].softwareVersion,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength, &u_error_code);
                printf("\t\tSoftware version: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength,
                             &u_error_code);
                printf("\t\tSoftware operating system: %s\n", strBuffer);
                free(strBuffer);
            }

            for(uint32_t j = 0; j < ctx->dump_hardware_entries_with_data[i].entry.extents; j++)
            {
                printf("\t\tExtent %d:\n", j);
                printf("\t\t\tStart: %llu\n", ctx->dump_hardware_entries_with_data[i].extents[j].start);
                printf("\t\t\tEnd: %llu\n", ctx->dump_hardware_entries_with_data[i].extents[j].end);
            }
        }
    }

    if(ctx->ecc_cd_context != NULL) printf("CD ECC has been initialized.\n");

    printf("There are %d data tracks.\n", ctx->number_of_data_tracks);

    // TODO: ctx->readableSectorTags;

    if(ctx->block_header_cache.max_items > 0)
    {
        if(ctx->block_header_cache.cache != NULL) printf("Block header cache has been initialized.\n");
        printf("Block header cache can contain a maximum of %llu items.\n", ctx->block_header_cache.max_items);
    }

    if(ctx->block_cache.max_items > 0)
    {
        if(ctx->block_cache.cache != NULL) printf("Block cache has been initialized.\n");
        printf("Block cache can contain a maximum of %llu items.\n", ctx->block_cache.max_items);
    }

    printf("Aaru's ImageInfo:\n");
    printf("\tHas partitions?: %s\n", ctx->image_info.HasPartitions ? "yes" : "no");
    printf("\tHas sessions?: %s\n", ctx->image_info.HasSessions ? "yes" : "no");
    printf("\tImage size without headers: %llu bytes\n", ctx->image_info.ImageSize);
    printf("\tImage contains %llu sectors\n", ctx->image_info.Sectors);
    printf("\tBiggest sector is %d bytes\n", ctx->image_info.SectorSize);
    printf("\tImage version: %s\n", ctx->image_info.Version);
    if(ctx->image_info.Application != NULL) printf("\tApplication: %s\n", ctx->image_info.Application);
    if(ctx->image_info.ApplicationVersion != NULL)
        printf("\tApplication version: %s\n", ctx->image_info.ApplicationVersion);
    printf("\tCreation time: %s\n", format_filetime(ctx->image_info.CreationTime));
    printf("\tLast written time: %s\n", format_filetime(ctx->image_info.LastModificationTime));
    printf("\tMedia type: %u (%s)\n", ctx->image_info.MediaType, media_type_to_string(ctx->image_info.MediaType));
    printf("\tXML media type: %d\n", ctx->image_info.MetadataMediaType);

    if(ctx->checksums.hasMd5)
    {
        strBuffer = byte_array_to_hex_string(ctx->checksums.md5, MD5_DIGEST_LENGTH);
        printf("MD5: %s\n", strBuffer);
        free(strBuffer);
    }

    if(ctx->checksums.hasSha1)
    {
        strBuffer = byte_array_to_hex_string(ctx->checksums.sha1, SHA1_DIGEST_LENGTH);
        printf("SHA1: %s\n", strBuffer);
        free(strBuffer);
    }

    if(ctx->checksums.hasSha256)
    {
        strBuffer = byte_array_to_hex_string(ctx->checksums.sha256, SHA256_DIGEST_LENGTH);
        printf("SHA256: %s\n", strBuffer);
        free(strBuffer);
    }

    if(ctx->checksums.hasSpamSum) printf("SpamSum: %s\n", ctx->checksums.spamsum);

    if(ctx->mediaTags != NULL)
    {
        printf("Media tags:\n");
        HASH_ITER(hh, ctx->mediaTags, mediaTag, tmpMediaTag)
        { printf("\tType %d is %d bytes long.\n", mediaTag->type, mediaTag->length); }
    }

    aaruf_close(ctx);

    return 0;
}
