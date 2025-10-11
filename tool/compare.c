/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

#include <aaruformat.h>
#include <stdbool.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

#define TB_IMPL

#include "termbox2.h"

void draw_progress_bar(int row, int percent)
{
    const int width     = tb_width() / 2;
    const int bar_width = width - 4;  // leave space for borders
    const int filled    = bar_width * percent / 100;

    // Draw progress bar outline
    tb_printf(2, row, TB_YELLOW | TB_BOLD, TB_BLUE, "[");
    tb_printf(bar_width + 3, row, TB_YELLOW | TB_BOLD, TB_BLUE, "]");

    // Fill progress bar
    for(int i = 0; i < filled; ++i) { tb_set_cell(3 + i, row, '=', TB_YELLOW | TB_BOLD, TB_BLUE); }

    tb_present();
}

int compare(const char *path1, const char *path2)
{
    int                 ret                  = AARUF_STATUS_OK;
    aaruformat_context *ctx1                 = NULL;
    aaruformat_context *ctx2                 = NULL;
    bool                imagesAreDifferent   = false;
    char               *strBuffer            = NULL;
    UErrorCode          u_error_code         = U_ZERO_ERROR;
    int                 lr                   = 0;
    int                 rr                   = 0;
    uintattr_t          appVerColor          = TB_WHITE;
    uintattr_t          imageVerColor        = TB_WHITE;
    uintattr_t          mediaTypeColor       = TB_WHITE;
    uintattr_t          creationTimeColor    = TB_WHITE;
    uintattr_t          lastWrittenTimeColor = TB_WHITE;
    uintattr_t          partitionsColor      = TB_WHITE;
    uintattr_t          sessionsColor        = TB_WHITE;
    uintattr_t          sectorsColor         = TB_WHITE;
    uintattr_t          sectorSizeColor      = TB_WHITE;
    uintattr_t          versionColor         = TB_WHITE;
    uint8_t             sector_status1       = 0;
    uint8_t             sector_status2       = 0;

    // Initialize termbox2
    if(tb_init() != 0) return 1;

    // Get terminal dimensions
    int width  = tb_width();   // Total number of columns
    int height = tb_height();  // Total number of rows
    int mid_x  = width / 2;    // Midpoint for horizontal split

    tb_clear();  // Clear the screen buffer

    // Draw left panel (blue background)
    for(int y = 0; y < height; ++y)
    {
        for(int x = 0; x < mid_x; ++x)
        {
            tb_set_cell(x, y, ' ', TB_WHITE, TB_BLUE);  // Set each cell to blank with blue background
        }
    }

    // Draw right panel (green background)
    for(int y = 0; y < height; ++y)
    {
        for(int x = mid_x + 1; x < width; ++x)
        {
            tb_set_cell(x, y, ' ', TB_WHITE, TB_BLUE);  // Set each cell to blank with green background
        }
    }

    // Draw vertical divider line (gray background)
    for(int y = 0; y < height; ++y)
    {
        tb_set_cell(mid_x, y, '|', TB_BLACK | TB_BOLD, TB_BLUE);  // Draw a vertical bar at the split
    }

    // Print labels in each panel
    tb_printf(2, 1, TB_WHITE | TB_BOLD, TB_BLUE, path1);          // Label in left panel
    tb_printf(mid_x + 2, 1, TB_WHITE | TB_BOLD, TB_BLUE, path2);  // Label in right panel

    tb_present();  // Render the buffer to the terminal

    // Open the first AaruFormat image
    tb_printf(2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Opening image...");
    tb_present();  // Render the buffer to the terminal
    ctx1 = aaruf_open(path1);
    if(ctx1 == NULL)
    {
        tb_printf(2, 3, TB_RED | TB_BOLD, TB_BLUE, "Error opening: %s", errno);
        tb_present();
        goto finished;
    }

    tb_printf(2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Image opened successfully...");

    // Open the second AaruFormat image
    tb_printf(mid_x + 2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Opening image...");
    tb_present();  // Render the buffer to the terminal
    ctx2 = aaruf_open(path2);
    if(ctx2 == NULL)
    {
        tb_printf(mid_x + 2, 3, TB_RED | TB_BOLD, TB_BLUE, "Error opening: %s", errno);
        tb_present();
        goto finished;
    }

    tb_printf(mid_x + 2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Image opened successfully...");

    u_error_code =
        u_strCompare((const UChar *)ctx1->header.application, -1, (const UChar *)ctx2->header.application, -1, false);
    if(u_error_code != U_ZERO_ERROR) imagesAreDifferent = true;

    strBuffer = malloc(65);
    memset(strBuffer, 0, 65);
    u_error_code = U_ZERO_ERROR;  // Reset error code before conversion
    ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, ctx1->header.application, 64, &u_error_code);
    if(u_error_code == U_ZERO_ERROR)
    {
        tb_printf(2, 3, TB_WHITE | TB_BOLD, TB_BLUE, "Application: ");
        tb_printf(15, 3, TB_WHITE, TB_BLUE, "%s", strBuffer);
    }
    tb_present();  // Render the buffer to the terminal

    memset(strBuffer, 0, 65);
    u_error_code = U_ZERO_ERROR;  // Reset error code before conversion
    ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, ctx2->header.application, 64, &u_error_code);
    if(u_error_code == U_ZERO_ERROR)
    {
        tb_printf(mid_x + 2, 3, TB_WHITE | TB_BOLD, TB_BLUE, "Application: ");
        tb_printf(mid_x + 15, 3, TB_WHITE, TB_BLUE, "%s", strBuffer);
    }
    tb_present();  // Render the buffer to the terminal
    free(strBuffer);

    // Compare header information
    if(ctx1->header.applicationMajorVersion != ctx2->header.applicationMajorVersion)
    {
        imagesAreDifferent = true;
        appVerColor        = TB_RED;
    }

    if(ctx1->header.applicationMinorVersion != ctx2->header.applicationMinorVersion)
    {
        imagesAreDifferent = true;
        appVerColor        = TB_RED;
    }

    if(ctx1->header.imageMajorVersion != ctx2->header.imageMajorVersion)
    {
        imagesAreDifferent = true;
        imageVerColor      = TB_RED;
    }

    if(ctx1->header.imageMinorVersion != ctx2->header.imageMinorVersion)
    {
        imagesAreDifferent = true;
        imageVerColor      = TB_RED;
    }

    if(ctx1->header.mediaType != ctx2->header.mediaType)
    {
        imagesAreDifferent = true;
        mediaTypeColor     = TB_RED;
    }

    if(ctx1->header.creationTime != ctx2->header.creationTime)
    {
        imagesAreDifferent = true;
        creationTimeColor  = TB_RED;
    }

    if(ctx1->header.lastWrittenTime != ctx2->header.lastWrittenTime)
    {
        imagesAreDifferent   = true;
        lastWrittenTimeColor = TB_RED;
    }

    // Print header information for the first image
    tb_printf(2, 4, TB_WHITE | TB_BOLD, TB_BLUE, "Application version: ");
    tb_printf(23, 4, appVerColor, TB_BLUE, "%d.%d", ctx1->header.applicationMajorVersion,
              ctx1->header.applicationMinorVersion);
    tb_printf(2, 5, TB_WHITE | TB_BOLD, TB_BLUE, "Image format version: ");
    tb_printf(24, 5, imageVerColor, TB_BLUE, "%d.%d", ctx1->header.imageMajorVersion, ctx1->header.imageMinorVersion);
    tb_printf(2, 6, TB_WHITE | TB_BOLD, TB_BLUE, "Media type: ");
    tb_printf(14, 6, mediaTypeColor, TB_BLUE, "%u", ctx1->header.mediaType);
    tb_printf(2, 7, TB_WHITE | TB_BOLD, TB_BLUE, "Creation time: ");
    tb_printf(17, 7, creationTimeColor, TB_BLUE, "%lld", ctx1->header.creationTime);
    tb_printf(2, 8, TB_WHITE | TB_BOLD, TB_BLUE, "Last written time: ");
    tb_printf(21, 8, lastWrittenTimeColor, TB_BLUE, "%lld", ctx1->header.lastWrittenTime);
    tb_present();  // Render the buffer to the terminal

    // Print header information for the second image
    tb_printf(mid_x + 2, 4, TB_WHITE | TB_BOLD, TB_BLUE, "Application version: ");
    tb_printf(mid_x + 23, 4, appVerColor, TB_BLUE, "%d.%d", ctx2->header.applicationMajorVersion,
              ctx2->header.applicationMinorVersion);
    tb_printf(mid_x + 2, 5, TB_WHITE | TB_BOLD, TB_BLUE, "Image format version: ");
    tb_printf(mid_x + 24, 5, imageVerColor, TB_BLUE, "%d.%d", ctx2->header.imageMajorVersion,
              ctx2->header.imageMinorVersion);
    tb_printf(mid_x + 2, 6, TB_WHITE | TB_BOLD, TB_BLUE, "Media type: ");
    tb_printf(mid_x + 14, 6, mediaTypeColor, TB_BLUE, "%u", ctx2->header.mediaType);
    tb_printf(mid_x + 2, 7, TB_WHITE | TB_BOLD, TB_BLUE, "Creation time: ");
    tb_printf(mid_x + 17, 7, creationTimeColor, TB_BLUE, "%lld", ctx2->header.creationTime);
    tb_printf(mid_x + 2, 8, TB_WHITE | TB_BOLD, TB_BLUE, "Last written time: ");
    tb_printf(mid_x + 21, 8, lastWrittenTimeColor, TB_BLUE, "%lld", ctx2->header.lastWrittenTime);
    tb_present();  // Render the buffer to the terminal

    // Compare ImageInfo
    u_error_code = U_ZERO_ERROR;
    u_error_code = u_strCompare((const UChar *)ctx1->image_info.Application, -1,
                                (const UChar *)ctx2->image_info.Application, -1, false);
    if(u_error_code != U_ZERO_ERROR) imagesAreDifferent = true;

    // Current left row
    lr = 9;
    // Current right row
    rr = 9;

    if(ctx1->image_info.HasPartitions != ctx2->image_info.HasPartitions)
    {
        imagesAreDifferent = true;
        partitionsColor    = TB_RED;
    }
    if(ctx1->image_info.HasSessions != ctx2->image_info.HasSessions)
    {
        imagesAreDifferent = true;
        sessionsColor      = TB_RED;
    }
    if(ctx1->image_info.Sectors != ctx2->image_info.Sectors)
    {
        imagesAreDifferent = true;
        sectorsColor       = TB_RED;
    }
    if(ctx1->image_info.SectorSize != ctx2->image_info.SectorSize)
    {
        imagesAreDifferent = true;
        sectorSizeColor    = TB_RED;
    }
    if(ctx1->image_info.Version != ctx2->image_info.Version)
    {
        imagesAreDifferent = true;
        versionColor       = TB_RED;
    }

    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Has partitions?: ");
    tb_printf(19, lr++, partitionsColor, TB_BLUE, "%s", ctx1->image_info.HasPartitions ? "yes" : "no");
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Has sessions?: ");
    tb_printf(17, lr++, sessionsColor, TB_BLUE, "%s", ctx1->image_info.HasSessions ? "yes" : "no");
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Image size without headers: ");
    tb_printf(30, lr++, TB_WHITE, TB_BLUE, "%llu bytes", ctx1->image_info.ImageSize);
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Image contains: ");
    tb_printf(18, lr++, sectorsColor, TB_BLUE, "%llu sectors", ctx1->image_info.Sectors);
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Biggest sector is: ");
    tb_printf(21, lr++, sectorSizeColor, TB_BLUE, "%d bytes", ctx1->image_info.SectorSize);
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Image version: ");
    tb_printf(17, lr++, versionColor, TB_BLUE, "%s", ctx1->image_info.Version);
    tb_present();

    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Has partitions?: ");
    tb_printf(mid_x + 19, rr++, partitionsColor, TB_BLUE, "%s", ctx2->image_info.HasPartitions ? "yes" : "no");
    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Has sessions?: ");
    tb_printf(mid_x + 17, rr++, sessionsColor, TB_BLUE, "%s", ctx2->image_info.HasSessions ? "yes" : "no");
    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Image size without headers: ");
    tb_printf(mid_x + 30, rr++, TB_WHITE, TB_BLUE, "%llu bytes", ctx2->image_info.ImageSize);
    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Image contains: ");
    tb_printf(mid_x + 18, rr++, sectorsColor, TB_BLUE, "%llu sectors", ctx2->image_info.Sectors);
    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Biggest sector is: ");
    tb_printf(mid_x + 21, rr++, sectorSizeColor, TB_BLUE, "%d bytes", ctx2->image_info.SectorSize);
    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Image version: ");
    tb_printf(mid_x + 17, rr++, versionColor, TB_BLUE, "%s", ctx2->image_info.Version);
    tb_present();

    if(ctx1->image_info.Application != NULL || ctx2->image_info.Application != NULL)
    {
        strBuffer = malloc(65);
        memset(strBuffer, 0, 65);
        u_error_code = U_ZERO_ERROR;  // Reset error code before conversion
        ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, (const char *)ctx1->image_info.Application, 64, &u_error_code);
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Application: ");
        if(u_error_code == U_ZERO_ERROR) tb_printf(15, lr, TB_WHITE, TB_BLUE, "%s", strBuffer);
        lr++;

        memset(strBuffer, 0, 65);
        u_error_code = U_ZERO_ERROR;  // Reset error code before conversion
        ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, (const char *)ctx2->image_info.Application, 64, &u_error_code);
        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Application: ");
        if(u_error_code == U_ZERO_ERROR) tb_printf(mid_x + 15, rr, TB_WHITE, TB_BLUE, "%s", strBuffer);
        rr++;

        free(strBuffer);
        tb_present();
    }

    if(ctx1->image_info.ApplicationVersion != NULL || ctx2->image_info.ApplicationVersion != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Application version: ");
        tb_printf(23, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->image_info.ApplicationVersion);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Application version: ");
        tb_printf(mid_x + 23, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->image_info.ApplicationVersion);
    }
    if(ctx1->creator != NULL || ctx2->creator != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Creator: ");
        tb_printf(11, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->creator);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Creator: ");
        tb_printf(mid_x + 11, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->creator);
    }

    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Creation time: ");
    tb_printf(17, lr++, TB_WHITE, TB_BLUE, "%lld", ctx1->image_info.CreationTime);

    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Creation time: ");
    tb_printf(mid_x + 17, rr++, TB_WHITE, TB_BLUE, "%lld", ctx2->image_info.CreationTime);

    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Last written time: ");
    tb_printf(21, lr++, TB_WHITE, TB_BLUE, "%lld", ctx1->image_info.LastModificationTime);

    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Last written time: ");
    tb_printf(mid_x + 21, rr++, TB_WHITE, TB_BLUE, "%lld", ctx2->image_info.LastModificationTime);

    if(ctx1->comments != NULL || ctx2->comments != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Comments: ");
        tb_printf(12, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->comments);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Comments: ");
        tb_printf(mid_x + 12, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->comments);
    }
    if(ctx1->media_title != NULL || ctx2->media_title != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Media title: ");
        tb_printf(15, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->media_title);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Media title: ");
        tb_printf(mid_x + 15, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->media_title);
    }
    if(ctx1->media_manufacturer != NULL || ctx2->media_manufacturer != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Media manufacturer: ");
        tb_printf(22, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->media_manufacturer);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Media manufacturer: ");
        tb_printf(mid_x + 22, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->media_manufacturer);
    }
    if(ctx1->media_serial_number != NULL || ctx2->media_serial_number != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Media serial number: ");
        tb_printf(23, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->media_serial_number);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Media serial number: ");
        tb_printf(mid_x + 23, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->media_serial_number);
    }
    if(ctx1->media_barcode != NULL || ctx2->media_barcode != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Media barcode: ");
        tb_printf(17, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->media_barcode);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Media barcode: ");
        tb_printf(mid_x + 17, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->media_barcode);
    }
    if(ctx1->media_part_number != NULL || ctx2->media_part_number != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Media part number: ");
        tb_printf(21, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->media_part_number);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Media part number: ");
        tb_printf(mid_x + 21, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->media_part_number);
    }
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Media type: ");
    tb_printf(14, lr++, TB_WHITE, TB_BLUE, "%u", ctx1->image_info.MediaType);

    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Media type: ");
    tb_printf(mid_x + 14, rr++, TB_WHITE, TB_BLUE, "%u", ctx2->image_info.MediaType);

    if(ctx1->media_sequence > 0 || ctx1->last_media_sequence > 0 || ctx2->media_sequence > 0 ||
       ctx2->last_media_sequence > 0)
    {
        tb_printf(2, lr++, TB_WHITE | TB_BOLD, TB_BLUE, "Media is number %d in a set of %d media", ctx1->media_sequence,
                  ctx1->last_media_sequence);

        tb_printf(mid_x + 2, rr++, TB_WHITE | TB_BOLD, TB_BLUE, "Media is number %d in a set of %d media",
                  ctx2->media_sequence, ctx2->last_media_sequence);
    }

    if(ctx1->drive_manufacturer != NULL || ctx2->drive_manufacturer != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive manufacturer: ");
        tb_printf(22, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->drive_manufacturer);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive manufacturer: ");
        tb_printf(mid_x + 22, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->drive_manufacturer);
    }
    if(ctx1->drive_model != NULL || ctx2->drive_model != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive model: ");
        tb_printf(15, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->drive_model);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive model: ");
        tb_printf(mid_x + 15, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->drive_model);
    }
    if(ctx1->drive_serial_number != NULL || ctx2->drive_serial_number != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive serial number: ");
        tb_printf(23, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->drive_serial_number);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive serial number: ");
        tb_printf(mid_x + 23, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->drive_serial_number);
    }
    if(ctx1->drive_firmware_revision != NULL || ctx2->drive_firmware_revision != NULL)
    {
        tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive firmware revision: ");
        tb_printf(27, lr++, TB_WHITE, TB_BLUE, "%s", ctx1->drive_firmware_revision);

        tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "Drive firmware revision: ");
        tb_printf(mid_x + 27, rr++, TB_WHITE, TB_BLUE, "%s", ctx2->drive_firmware_revision);
    }
    tb_printf(2, lr, TB_WHITE | TB_BOLD, TB_BLUE, "XML media type: ");
    tb_printf(18, lr++, TB_WHITE, TB_BLUE, "%d", ctx1->image_info.MetadataMediaType);

    tb_printf(mid_x + 2, rr, TB_WHITE | TB_BOLD, TB_BLUE, "XML media type: ");
    tb_printf(mid_x + 18, rr++, TB_WHITE, TB_BLUE, "%d", ctx2->image_info.MetadataMediaType);

    if(ctx1->cylinders > 0 || ctx1->heads > 0 || ctx1->sectors_per_track > 0 || ctx2->cylinders > 0 ||
       ctx2->heads > 0 || ctx2->sectors_per_track > 0)
    {
        tb_printf(2, lr++, TB_WHITE | TB_BOLD, TB_BLUE, "Media has %d cylinders, %d heads and %d sectors per track",
                  ctx1->cylinders, ctx1->heads, ctx1->sectors_per_track);

        tb_printf(mid_x + 2, rr++, TB_WHITE | TB_BOLD, TB_BLUE,
                  "Media has %d cylinders, %d heads and %d sectors per track", ctx2->cylinders, ctx2->heads,
                  ctx2->sectors_per_track);
    }

    tb_present();

    lr++;
    uint64_t sectors = ctx1->image_info.Sectors;
    if(ctx2->image_info.Sectors < sectors) sectors = ctx2->image_info.Sectors;
    bool     imageContentsAreDifferent = false;
    uint32_t sectorSize                = ctx1->image_info.SectorSize;
    if(ctx2->image_info.SectorSize > sectorSize) sectorSize = ctx2->image_info.SectorSize;
    uint8_t *buffer1 = malloc(sectorSize);

    if(buffer1 == NULL)
    {
        tb_printf(2, lr, TB_RED | TB_BOLD, TB_BLUE, "Error allocating memory for buffer: %s", errno);
        tb_present();
        ret = AARUF_ERROR_NOT_ENOUGH_MEMORY;
        goto finished;
    }

    uint8_t *buffer2 = malloc(sectorSize);

    if(buffer2 == NULL)
    {
        free(buffer1);
        tb_printf(2, lr, TB_RED | TB_BOLD, TB_BLUE, "Error allocating memory for buffer: %s", errno);
        tb_present();
        ret = AARUF_ERROR_NOT_ENOUGH_MEMORY;
        goto finished;
    }

    for(uint64_t i = 0; i < sectors; i++)
    {
        tb_printf(2, height - 5, TB_WHITE | TB_BOLD, TB_BLUE, "Comparing sector %llu of %llu", i + 1, sectors);
        draw_progress_bar(height - 4, i * 100 / sectors);

        errno = aaruf_read_sector(ctx1, i, false, buffer1, &sectorSize, &sector_status1);
        if(errno != AARUF_STATUS_OK && errno != AARUF_STATUS_SECTOR_NOT_DUMPED)
        {
            tb_printf(2, lr++, TB_RED | TB_BOLD, TB_BLUE, "Error reading sector %llu: %s", i, errno);
            tb_present();
            continue;
        }

        errno = aaruf_read_sector(ctx2, i, false, buffer2, &sectorSize, &sector_status2);
        if(errno != AARUF_STATUS_OK && errno != AARUF_STATUS_SECTOR_NOT_DUMPED)
        {
            tb_printf(2, rr++, TB_RED | TB_BOLD, TB_BLUE, "Error reading sector %llu: %s", i, errno);
            tb_present();
            continue;
        }

        for(uint32_t j = 0; j < sectorSize; j++)
            if(buffer1[j] != buffer2[j])
            {
                imageContentsAreDifferent = true;
                tb_printf(2, lr++, TB_RED | TB_BOLD, TB_BLUE, "Sector %llu differs at byte %u", i, j);
                tb_present();
                break;  // Exit the loop on first difference
            }
    }
    draw_progress_bar(height - 4, 100);  // Fill the progress bar to 100%

    free(buffer1);
    free(buffer2);

    if(imagesAreDifferent)
        tb_printf(2, height - 3, TB_RED | TB_BOLD, TB_BLUE, "Images are different!");
    else
        tb_printf(2, height - 3, TB_GREEN | TB_BOLD, TB_BLUE, "Images are identical!");
    if(imageContentsAreDifferent)
        tb_printf(2, height - 2, TB_RED | TB_BOLD, TB_BLUE, "Images contents are different!");
    else
        tb_printf(2, height - 2, TB_GREEN | TB_BOLD, TB_BLUE, "Images contents are identical!");

finished:
    tb_printf(2, height - 1, TB_WHITE | TB_BOLD, TB_BLUE, "Press any key to exit...");
    tb_present();  // Render the buffer to the terminal
    // Wait for a key press before exiting
    struct tb_event ev;
    tb_poll_event(&ev);  // Block until user presses a key

    // Restore terminal and exit
    tb_shutdown();
    return ret;
}