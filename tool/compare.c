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

// Helper structure to track comparison state
typedef struct {
    int mid_x;
    int height;
    int left_row;
    int right_row;
    bool has_differences;
} compare_state_t;

void draw_progress_bar(int row, int percent)
{
    const int width     = tb_width() / 2;
    const int bar_width = width - 4;
    const int filled    = bar_width * percent / 100;

    tb_printf(2, row, TB_YELLOW | TB_BOLD, TB_BLUE, "[");
    tb_printf(bar_width + 3, row, TB_YELLOW | TB_BOLD, TB_BLUE, "]");

    for(int i = 0; i < filled; ++i) {
        tb_set_cell(3 + i, row, '=', TB_YELLOW | TB_BOLD, TB_BLUE);
    }

    tb_present();
}

static void setup_screen(compare_state_t *state)
{
    int width = tb_width();
    int height = tb_height();
    state->mid_x = width / 2;
    state->height = height;
    state->left_row = 9;
    state->right_row = 9;
    state->has_differences = false;

    tb_clear();

    // Draw panels and divider
    for(int y = 0; y < height; ++y)
    {
        for(int x = 0; x < state->mid_x; ++x)
            tb_set_cell(x, y, ' ', TB_WHITE, TB_BLUE);

        tb_set_cell(state->mid_x, y, '|', TB_BLACK | TB_BOLD, TB_BLUE);

        for(int x = state->mid_x + 1; x < width; ++x)
            tb_set_cell(x, y, ' ', TB_WHITE, TB_BLUE);
    }
}

// Helper to print a field on both sides with comparison
static void print_field_pair(compare_state_t *state, const char *label,
                             const char *fmt, uintattr_t color,
                             void *val1, void *val2, int label_len)
{
    tb_printf(2, state->left_row, TB_WHITE | TB_BOLD, TB_BLUE, "%s", label);
    tb_printf(label_len, state->left_row, color, TB_BLUE, fmt, val1);

    tb_printf(state->mid_x + 2, state->right_row, TB_WHITE | TB_BOLD, TB_BLUE, "%s", label);
    tb_printf(state->mid_x + label_len, state->right_row, color, TB_BLUE, fmt, val2);

    state->left_row++;
    state->right_row++;
}

// Helper to compare and print integer fields
static void compare_int_field(compare_state_t *state, const char *label, int label_len,
                              uint64_t val1, uint64_t val2, const char *fmt)
{
    uintattr_t color = (val1 != val2) ? TB_RED : TB_WHITE;
    if(val1 != val2) state->has_differences = true;

    print_field_pair(state, label, fmt, color, &val1, &val2, label_len);
}

// Helper to compare and print string fields (if at least one is non-NULL)
static void compare_string_field(compare_state_t *state, const char *label, int label_len,
                                 const char *val1, const char *val2)
{
    if(val1 == NULL && val2 == NULL) return;

    tb_printf(2, state->left_row, TB_WHITE | TB_BOLD, TB_BLUE, "%s", label);
    if(val1) tb_printf(label_len, state->left_row, TB_WHITE, TB_BLUE, "%s", val1);

    tb_printf(state->mid_x + 2, state->right_row, TB_WHITE | TB_BOLD, TB_BLUE, "%s", label);
    if(val2) tb_printf(state->mid_x + label_len, state->right_row, TB_WHITE, TB_BLUE, "%s", val2);

    state->left_row++;
    state->right_row++;
}

// Helper to convert and print UTF-16 application name
static void print_application_name(compare_state_t *state,
                                   const char *app1, const char *app2)
{
    char strBuffer[65];
    UErrorCode u_error_code = U_ZERO_ERROR;

    // Left side
    memset(strBuffer, 0, 65);
    ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, app1, 64, &u_error_code);
    tb_printf(2, 3, TB_WHITE | TB_BOLD, TB_BLUE, "Application: ");
    if(u_error_code == U_ZERO_ERROR)
        tb_printf(15, 3, TB_WHITE, TB_BLUE, "%s", strBuffer);

    // Right side
    memset(strBuffer, 0, 65);
    u_error_code = U_ZERO_ERROR;
    ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, app2, 64, &u_error_code);
    tb_printf(state->mid_x + 2, 3, TB_WHITE | TB_BOLD, TB_BLUE, "Application: ");
    if(u_error_code == U_ZERO_ERROR)
        tb_printf(state->mid_x + 15, 3, TB_WHITE, TB_BLUE, "%s", strBuffer);
}

// Compare header information
static void compare_headers(compare_state_t *state,
                            aaruformat_context *ctx1, aaruformat_context *ctx2)
{
    // Check application name difference
    UErrorCode u_err = u_strCompare((const UChar *)ctx1->header.application, -1,
                                    (const UChar *)ctx2->header.application, -1, false);
    if(u_err != U_ZERO_ERROR) state->has_differences = true;

    print_application_name(state, ctx1->header.application, ctx2->header.application);

    // Compare version fields
    uintattr_t app_ver_color = (ctx1->header.applicationMajorVersion != ctx2->header.applicationMajorVersion ||
                                ctx1->header.applicationMinorVersion != ctx2->header.applicationMinorVersion)
                                ? TB_RED : TB_WHITE;
    uintattr_t img_ver_color = (ctx1->header.imageMajorVersion != ctx2->header.imageMajorVersion ||
                                ctx1->header.imageMinorVersion != ctx2->header.imageMinorVersion)
                                ? TB_RED : TB_WHITE;
    uintattr_t media_color = (ctx1->header.mediaType != ctx2->header.mediaType) ? TB_RED : TB_WHITE;
    uintattr_t create_color = (ctx1->header.creationTime != ctx2->header.creationTime) ? TB_RED : TB_WHITE;
    uintattr_t modified_color = (ctx1->header.lastWrittenTime != ctx2->header.lastWrittenTime) ? TB_RED : TB_WHITE;

    if(app_ver_color == TB_RED || img_ver_color == TB_RED || media_color == TB_RED ||
       create_color == TB_RED || modified_color == TB_RED)
        state->has_differences = true;

    // Print header fields
    tb_printf(2, 4, TB_WHITE | TB_BOLD, TB_BLUE, "Application version: ");
    tb_printf(23, 4, app_ver_color, TB_BLUE, "%d.%d",
              ctx1->header.applicationMajorVersion, ctx1->header.applicationMinorVersion);
    tb_printf(state->mid_x + 2, 4, TB_WHITE | TB_BOLD, TB_BLUE, "Application version: ");
    tb_printf(state->mid_x + 23, 4, app_ver_color, TB_BLUE, "%d.%d",
              ctx2->header.applicationMajorVersion, ctx2->header.applicationMinorVersion);

    tb_printf(2, 5, TB_WHITE | TB_BOLD, TB_BLUE, "Image format version: ");
    tb_printf(24, 5, img_ver_color, TB_BLUE, "%d.%d",
              ctx1->header.imageMajorVersion, ctx1->header.imageMinorVersion);
    tb_printf(state->mid_x + 2, 5, TB_WHITE | TB_BOLD, TB_BLUE, "Image format version: ");
    tb_printf(state->mid_x + 24, 5, img_ver_color, TB_BLUE, "%d.%d",
              ctx2->header.imageMajorVersion, ctx2->header.imageMinorVersion);

    tb_printf(2, 6, TB_WHITE | TB_BOLD, TB_BLUE, "Media type: ");
    tb_printf(14, 6, media_color, TB_BLUE, "%u", ctx1->header.mediaType);
    tb_printf(state->mid_x + 2, 6, TB_WHITE | TB_BOLD, TB_BLUE, "Media type: ");
    tb_printf(state->mid_x + 14, 6, media_color, TB_BLUE, "%u", ctx2->header.mediaType);

    tb_printf(2, 7, TB_WHITE | TB_BOLD, TB_BLUE, "Creation time: ");
    tb_printf(17, 7, create_color, TB_BLUE, "%lld", ctx1->header.creationTime);
    tb_printf(state->mid_x + 2, 7, TB_WHITE | TB_BOLD, TB_BLUE, "Creation time: ");
    tb_printf(state->mid_x + 17, 7, create_color, TB_BLUE, "%lld", ctx2->header.creationTime);

    tb_printf(2, 8, TB_WHITE | TB_BOLD, TB_BLUE, "Last written time: ");
    tb_printf(21, 8, modified_color, TB_BLUE, "%lld", ctx1->header.lastWrittenTime);
    tb_printf(state->mid_x + 2, 8, TB_WHITE | TB_BOLD, TB_BLUE, "Last written time: ");
    tb_printf(state->mid_x + 21, 8, modified_color, TB_BLUE, "%lld", ctx2->header.lastWrittenTime);

    tb_present();
}

// Compare image info
static void compare_image_info(compare_state_t *state,
                              aaruformat_context *ctx1, aaruformat_context *ctx2)
{
    compare_int_field(state, "Has partitions?: ", 19,
                     ctx1->image_info.HasPartitions, ctx2->image_info.HasPartitions, "%s");
    state->left_row--;
    state->right_row--;
    tb_printf(19, state->left_row, (ctx1->image_info.HasPartitions != ctx2->image_info.HasPartitions) ? TB_RED : TB_WHITE,
              TB_BLUE, "%s", ctx1->image_info.HasPartitions ? "yes" : "no");
    tb_printf(state->mid_x + 19, state->right_row,
              (ctx1->image_info.HasPartitions != ctx2->image_info.HasPartitions) ? TB_RED : TB_WHITE,
              TB_BLUE, "%s", ctx2->image_info.HasPartitions ? "yes" : "no");
    state->left_row++;
    state->right_row++;

    compare_int_field(state, "Has sessions?: ", 17,
                     ctx1->image_info.HasSessions, ctx2->image_info.HasSessions, "%s");
    state->left_row--;
    state->right_row--;
    tb_printf(17, state->left_row, (ctx1->image_info.HasSessions != ctx2->image_info.HasSessions) ? TB_RED : TB_WHITE,
              TB_BLUE, "%s", ctx1->image_info.HasSessions ? "yes" : "no");
    tb_printf(state->mid_x + 17, state->right_row,
              (ctx1->image_info.HasSessions != ctx2->image_info.HasSessions) ? TB_RED : TB_WHITE,
              TB_BLUE, "%s", ctx2->image_info.HasSessions ? "yes" : "no");
    state->left_row++;
    state->right_row++;

    tb_printf(2, state->left_row, TB_WHITE | TB_BOLD, TB_BLUE, "Image size without headers: ");
    tb_printf(30, state->left_row, TB_WHITE, TB_BLUE, "%llu bytes", ctx1->image_info.ImageSize);
    tb_printf(state->mid_x + 2, state->right_row, TB_WHITE | TB_BOLD, TB_BLUE, "Image size without headers: ");
    tb_printf(state->mid_x + 30, state->right_row, TB_WHITE, TB_BLUE, "%llu bytes", ctx2->image_info.ImageSize);
    state->left_row++;
    state->right_row++;

    compare_int_field(state, "Image contains: ", 18,
                     ctx1->image_info.Sectors, ctx2->image_info.Sectors, "%llu sectors");
    compare_int_field(state, "Biggest sector is: ", 21,
                     ctx1->image_info.SectorSize, ctx2->image_info.SectorSize, "%d bytes");

    uintattr_t ver_color = TB_WHITE;
    if(ctx1->image_info.Version && ctx2->image_info.Version &&
       strcmp(ctx1->image_info.Version, ctx2->image_info.Version) != 0) {
        ver_color = TB_RED;
        state->has_differences = true;
    }

    tb_printf(2, state->left_row, TB_WHITE | TB_BOLD, TB_BLUE, "Image version: ");
    if(ctx1->image_info.Version) tb_printf(17, state->left_row, ver_color, TB_BLUE, "%s", ctx1->image_info.Version);
    tb_printf(state->mid_x + 2, state->right_row, TB_WHITE | TB_BOLD, TB_BLUE, "Image version: ");
    if(ctx2->image_info.Version) tb_printf(state->mid_x + 17, state->right_row, ver_color, TB_BLUE, "%s", ctx2->image_info.Version);
    state->left_row++;
    state->right_row++;

    tb_present();
}

// Compare metadata fields
static void compare_metadata(compare_state_t *state,
                            aaruformat_context *ctx1, aaruformat_context *ctx2)
{
    compare_string_field(state, "Application version: ", 23,
                        ctx1->image_info.ApplicationVersion, ctx2->image_info.ApplicationVersion);
    compare_string_field(state, "Creator: ", 11, ctx1->creator, ctx2->creator);

    compare_int_field(state, "Creation time: ", 17,
                     ctx1->image_info.CreationTime, ctx2->image_info.CreationTime, "%lld");
    compare_int_field(state, "Last written time: ", 21,
                     ctx1->image_info.LastModificationTime, ctx2->image_info.LastModificationTime, "%lld");

    compare_string_field(state, "Comments: ", 12, ctx1->comments, ctx2->comments);
    compare_string_field(state, "Media title: ", 15, ctx1->media_title, ctx2->media_title);
    compare_string_field(state, "Media manufacturer: ", 22,
                        ctx1->media_manufacturer, ctx2->media_manufacturer);
    compare_string_field(state, "Media serial number: ", 23,
                        ctx1->media_serial_number, ctx2->media_serial_number);
    compare_string_field(state, "Media barcode: ", 17, ctx1->media_barcode, ctx2->media_barcode);
    compare_string_field(state, "Media part number: ", 21,
                        ctx1->media_part_number, ctx2->media_part_number);

    compare_int_field(state, "Media type: ", 14,
                     ctx1->image_info.MediaType, ctx2->image_info.MediaType, "%u");

    if(ctx1->media_sequence > 0 || ctx1->last_media_sequence > 0 ||
       ctx2->media_sequence > 0 || ctx2->last_media_sequence > 0)
    {
        tb_printf(2, state->left_row, TB_WHITE | TB_BOLD, TB_BLUE,
                 "Media is number %d in a set of %d media",
                 ctx1->media_sequence, ctx1->last_media_sequence);
        tb_printf(state->mid_x + 2, state->right_row, TB_WHITE | TB_BOLD, TB_BLUE,
                 "Media is number %d in a set of %d media",
                 ctx2->media_sequence, ctx2->last_media_sequence);
        state->left_row++;
        state->right_row++;
    }

    compare_string_field(state, "Drive manufacturer: ", 22,
                        ctx1->drive_manufacturer, ctx2->drive_manufacturer);
    compare_string_field(state, "Drive model: ", 15, ctx1->drive_model, ctx2->drive_model);
    compare_string_field(state, "Drive serial number: ", 23,
                        ctx1->drive_serial_number, ctx2->drive_serial_number);
    compare_string_field(state, "Drive firmware revision: ", 27,
                        ctx1->drive_firmware_revision, ctx2->drive_firmware_revision);

    compare_int_field(state, "XML media type: ", 18,
                     ctx1->image_info.MetadataMediaType, ctx2->image_info.MetadataMediaType, "%d");

    if(ctx1->cylinders > 0 || ctx1->heads > 0 || ctx1->sectors_per_track > 0 ||
       ctx2->cylinders > 0 || ctx2->heads > 0 || ctx2->sectors_per_track > 0)
    {
        tb_printf(2, state->left_row, TB_WHITE | TB_BOLD, TB_BLUE,
                 "Media has %d cylinders, %d heads and %d sectors per track",
                 ctx1->cylinders, ctx1->heads, ctx1->sectors_per_track);
        tb_printf(state->mid_x + 2, state->right_row, TB_WHITE | TB_BOLD, TB_BLUE,
                 "Media has %d cylinders, %d heads and %d sectors per track",
                 ctx2->cylinders, ctx2->heads, ctx2->sectors_per_track);
        state->left_row++;
        state->right_row++;
    }

    tb_present();
}

// Compare sector contents
static int compare_sectors(compare_state_t *state,
                          aaruformat_context *ctx1, aaruformat_context *ctx2)
{
    uint64_t sectors = ctx1->image_info.Sectors;
    if(ctx2->image_info.Sectors < sectors) sectors = ctx2->image_info.Sectors;

    uint32_t sectorSize = ctx1->image_info.SectorSize;
    if(ctx2->image_info.SectorSize > sectorSize) sectorSize = ctx2->image_info.SectorSize;

    uint8_t *buffer1 = malloc(sectorSize);
    if(buffer1 == NULL)
    {
        tb_printf(2, state->left_row, TB_RED | TB_BOLD, TB_BLUE,
                 "Error allocating memory for buffer");
        tb_present();
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    uint8_t *buffer2 = malloc(sectorSize);
    if(buffer2 == NULL)
    {
        free(buffer1);
        tb_printf(2, state->left_row, TB_RED | TB_BOLD, TB_BLUE,
                 "Error allocating memory for buffer");
        tb_present();
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    bool contents_differ = false;
    uint8_t sector_status1 = 0;
    uint8_t sector_status2 = 0;

    for(uint64_t i = 0; i < sectors; i++)
    {
        tb_printf(2, state->height - 5, TB_WHITE | TB_BOLD, TB_BLUE,
                 "Comparing sector %llu of %llu", i + 1, sectors);
        draw_progress_bar(state->height - 4, i * 100 / sectors);

        int err1 = aaruf_read_sector(ctx1, i, false, buffer1, &sectorSize, &sector_status1);
        int err2 = aaruf_read_sector(ctx2, i, false, buffer2, &sectorSize, &sector_status2);

        if(err1 != AARUF_STATUS_OK && err1 != AARUF_STATUS_SECTOR_NOT_DUMPED) continue;
        if(err2 != AARUF_STATUS_OK && err2 != AARUF_STATUS_SECTOR_NOT_DUMPED) continue;

        if(memcmp(buffer1, buffer2, sectorSize) != 0)
        {
            contents_differ = true;
            if(state->left_row < state->height - 6)
            {
                tb_printf(2, state->left_row++, TB_RED | TB_BOLD, TB_BLUE,
                         "Sector %llu differs", i);
                tb_present();
            }
        }
    }

    draw_progress_bar(state->height - 4, 100);

    free(buffer1);
    free(buffer2);

    // Print final comparison results
    if(state->has_differences)
        tb_printf(2, state->height - 3, TB_RED | TB_BOLD, TB_BLUE, "Images are different!");
    else
        tb_printf(2, state->height - 3, TB_GREEN | TB_BOLD, TB_BLUE, "Images are identical!");

    if(contents_differ)
        tb_printf(2, state->height - 2, TB_RED | TB_BOLD, TB_BLUE, "Images contents are different!");
    else
        tb_printf(2, state->height - 2, TB_GREEN | TB_BOLD, TB_BLUE, "Images contents are identical!");

    return AARUF_STATUS_OK;
}

int compare(const char *path1, const char *path2)
{
    int ret = AARUF_STATUS_OK;
    aaruformat_context *ctx1 = NULL;
    aaruformat_context *ctx2 = NULL;
    compare_state_t state = {0};

    if(tb_init() != 0) return 1;

    setup_screen(&state);

    // Print file paths
    tb_printf(2, 1, TB_WHITE | TB_BOLD, TB_BLUE, "%s", path1);
    tb_printf(state.mid_x + 2, 1, TB_WHITE | TB_BOLD, TB_BLUE, "%s", path2);
    tb_present();

    // Open first image
    tb_printf(2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Opening image...");
    tb_present();
    ctx1 = aaruf_open(path1, false, NULL);
    if(ctx1 == NULL)
    {
        tb_printf(2, 3, TB_RED | TB_BOLD, TB_BLUE, "Error opening image");
        tb_present();
        ret = -1;
        goto finished;
    }
    tb_printf(2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Image opened successfully...");

    // Open second image
    tb_printf(state.mid_x + 2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Opening image...");
    tb_present();
    ctx2 = aaruf_open(path2, false, NULL);
    if(ctx2 == NULL)
    {
        tb_printf(state.mid_x + 2, 3, TB_RED | TB_BOLD, TB_BLUE, "Error opening image");
        tb_present();
        ret = -1;
        goto finished;
    }
    tb_printf(state.mid_x + 2, 2, TB_WHITE | TB_BOLD, TB_BLUE, "Image opened successfully...");

    // Perform comparisons
    compare_headers(&state, ctx1, ctx2);
    compare_image_info(&state, ctx1, ctx2);
    compare_metadata(&state, ctx1, ctx2);

    state.left_row++;
    ret = compare_sectors(&state, ctx1, ctx2);

finished:
    tb_printf(2, state.height - 1, TB_WHITE | TB_BOLD, TB_BLUE, "Press any key to exit...");
    tb_present();

    struct tb_event ev;
    tb_poll_event(&ev);

    if(ctx1) aaruf_close(ctx1);
    if(ctx2) aaruf_close(ctx2);

    tb_shutdown();
    return ret;
}