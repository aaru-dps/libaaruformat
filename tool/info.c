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
        printf("\n❌ Error: Cannot open AaruFormat image (error code: %d)\n\n", errno);
        return errno;
    }

    printf("\n");
    printf("================================================================================\n");
    printf("                        AARUFORMAT IMAGE INFORMATION\n");
    printf("================================================================================\n\n");

    // Image Format Section
    printf("┌─ IMAGE FORMAT ─────────────────────────────────────────────────────────────┐\n");
    printf("│ Magic:               %8.8s                                              │\n", (char *)&ctx->magic);
    printf("│ Format Version:      %d.%d                                                   │\n",
           ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    printf("│ Library Version:     %d.%d                                                   │\n",
           ctx->library_major_version, ctx->library_minor_version);
    printf("│ Identifier:          %8.8s                                              │\n",
           (char *)&ctx->header.identifier);
    printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");

    // Creator Application Section
    printf("┌─ CREATOR APPLICATION ──────────────────────────────────────────────────────┐\n");
    printf("│ Application:         %-53s │\n", (char *)ctx->header.application);
    printf("│ Version:             %d.%d                                                   │\n",
           ctx->header.applicationMajorVersion, ctx->header.applicationMinorVersion);
    printf("│ Created:             %-53s │\n", format_filetime(ctx->header.creationTime));
    printf("│ Last Modified:       %-53s │\n", format_filetime(ctx->header.lastWrittenTime));
    printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");

    // Media Information Section
    printf("┌─ MEDIA INFORMATION ────────────────────────────────────────────────────────┐\n");
    printf("│ Media Type:          %-53s │\n", media_type_to_string(ctx->header.mediaType));

    uint32_t cylinders       = 0;
    uint32_t heads           = 0;
    uint32_t sectorsPerTrack = 0;
    if(aaruf_get_geometry(ctx, &cylinders, &heads, &sectorsPerTrack) == AARUF_STATUS_OK)
    {
        printf("│ Geometry:            C:%u / H:%u / S:%u                                     │\n", cylinders, heads,
               sectorsPerTrack);
    }

    char image_size[78];
    snprintf(image_size, sizeof(image_size), "%llu bytes", ctx->image_info.ImageSize);
    printf("│ Image Size:          %-53s │\n", image_size);
    printf("│ Total Sectors:       %-53llu │\n", ctx->image_info.Sectors);
    printf("│ Sector Size:         %d bytes                                            │\n",
           ctx->image_info.SectorSize);
    printf("│ Has Partitions:      %-53s │\n", ctx->image_info.HasPartitions ? "Yes" : "No");
    printf("│ Has Sessions:        %-53s │\n", ctx->image_info.HasSessions ? "Yes" : "No");
    printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");

    // Metadata Section
    int32_t sequence     = 0;
    int32_t lastSequence = 0;
    bool    hasMetadata  = false;
    int32_t length       = 0;

    // Check if we have any metadata
    if((aaruf_get_media_sequence(ctx, &sequence, &lastSequence) == AARUF_STATUS_OK && sequence > 0) ||
       aaruf_get_creator(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_comments(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_title(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_barcode(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_part_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_firmware_revision(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        hasMetadata = true;
    }

    if(hasMetadata)
    {
        printf("┌─ METADATA ─────────────────────────────────────────────────────────────────┐\n");

        if(aaruf_get_media_sequence(ctx, &sequence, &lastSequence) == AARUF_STATUS_OK && sequence > 0)
        {
            printf("│ Media Sequence:      %d of %d                                               │\n", sequence,
                   lastSequence);
        }

        length = 0;
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Creator:             %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Comments:            %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Media Title:         %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Media Manufacturer:  %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Media Model:         %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Media Serial:        %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Media Barcode:       %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Media Part Number:   %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Drive Manufacturer:  %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Drive Model:         %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Drive Serial:        %-52s │\n", strBuffer);
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
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) printf("│ Drive Firmware:      %-52s │\n", strBuffer);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");
    }

    // Tracks Section
    if(ctx->tracks_header.identifier == TracksBlock && ctx->tracks_header.entries > 0)
    {
        printf("┌─ TRACKS (%u total) ─────────────────────────────────────────────────────────┐\n",
               ctx->tracks_header.entries);
        for(i = 0; i < ctx->tracks_header.entries && i < 20; i++)  // Limit to first 20 tracks
        {
            char track_info[78];
            snprintf(track_info, sizeof(track_info), "Track #%-2d: Seq=%d Type=%d Start=%lld End=%lld Session=%d",
                     i + 1, ctx->track_entries[i].sequence, ctx->track_entries[i].type, ctx->track_entries[i].start,
                     ctx->track_entries[i].end, ctx->track_entries[i].session);
            printf("│ %-74s │\n", track_info);
        }
        if(ctx->tracks_header.entries > 20)
        {
            char more_info[78];
            snprintf(more_info, sizeof(more_info), "... and %u more track(s)", ctx->tracks_header.entries - 20);
            printf("│ %-74s │\n", more_info);
        }
        printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");
    }

    // Dump Hardware Section
    if(ctx->dump_hardware_header.identifier == DumpHardwareBlock && ctx->dump_hardware_header.entries > 0)
    {
        printf("┌─ DUMP HARDWARE (%u device(s)) ──────────────────────────────────────────────┐\n",
               ctx->dump_hardware_header.entries);

        for(i = 0; i < ctx->dump_hardware_header.entries; i++)
        {
            printf("│ Device #%d:                                                                 │\n", i + 1);

            if(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].manufacturer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength, &u_error_code);
                printf("│   Manufacturer: %-58s │\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.modelLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.modelLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.modelLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer, (int)ctx->dump_hardware_entries_with_data[i].entry.modelLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].model,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.modelLength, &u_error_code);
                printf("│   Model: %-65s │\n", strBuffer);
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
                printf("│   Software: %-62s │\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.extents > 0 &&
               ctx->dump_hardware_entries_with_data[i].entry.extents <= 3)
            {
                for(uint32_t j = 0; j < ctx->dump_hardware_entries_with_data[i].entry.extents; j++)
                {
                    char extent_str[80];
                    snprintf(extent_str, sizeof(extent_str), "%llu - %llu",
                             ctx->dump_hardware_entries_with_data[i].extents[j].start,
                             ctx->dump_hardware_entries_with_data[i].extents[j].end);
                    printf("│   Extent: %-64s │\n", extent_str);
                }
            }
            else if(ctx->dump_hardware_entries_with_data[i].entry.extents > 3)
            {
                char extent_str[80];
                snprintf(extent_str, sizeof(extent_str), "%u extent(s)",
                         ctx->dump_hardware_entries_with_data[i].entry.extents);
                printf("│   Extents: %-65s │\n", extent_str);
            }
        }
        printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");
    }

    // Checksums Section
    bool hasChecksums =
        ctx->checksums.hasMd5 || ctx->checksums.hasSha1 || ctx->checksums.hasSha256 || ctx->checksums.hasSpamSum;

    if(hasChecksums)
    {
        printf("┌─ CHECKSUMS ────────────────────────────────────────────────────────────────┐\n");

        if(ctx->checksums.hasMd5)
        {
            strBuffer = byte_array_to_hex_string(ctx->checksums.md5, MD5_DIGEST_LENGTH);
            printf("│ MD5:     %s                                         │\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->checksums.hasSha1)
        {
            strBuffer = byte_array_to_hex_string(ctx->checksums.sha1, SHA1_DIGEST_LENGTH);
            printf("│ SHA-1:   %s                                 │\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->checksums.hasSha256)
        {
            strBuffer = byte_array_to_hex_string(ctx->checksums.sha256, SHA256_DIGEST_LENGTH);
            printf("│ SHA-256: %-64s │\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->checksums.hasSpamSum) { printf("│ SpamSum: %-64s │\n", ctx->checksums.spamsum); }

        printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");
    }

    // Media Tags Section
    if(ctx->mediaTags != NULL)
    {
        uint32_t tag_count = HASH_COUNT(ctx->mediaTags);
        printf("┌─ MEDIA TAGS (%u total) ─────────────────────────────────────────────────────┐\n", tag_count);

        uint32_t displayed = 0;
        HASH_ITER(hh, ctx->mediaTags, mediaTag, tmpMediaTag)
        {
            if(displayed < 20)  // Limit display to first 20 tags
            {
                char tag_info[78];
                snprintf(tag_info, sizeof(tag_info), "%s (%d bytes)", data_type_to_string((uint16_t)mediaTag->type),
                         mediaTag->length);
                printf("│ %-74s │\n", tag_info);
                displayed++;
            }
        }

        if(tag_count > 20)
        {
            char more_info[78];
            snprintf(more_info, sizeof(more_info), "... and %u more tag(s)", tag_count - 20);
            printf("│ %-76s │\n", more_info);
        }

        printf("└────────────────────────────────────────────────────────────────────────────┘\n\n");
    }

    printf("================================================================================\n\n");

    aaruf_close(ctx);

    return 0;
}
