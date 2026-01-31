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

#include "benchmark.h"
#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <aaruformat/structs/data.h>
#include <aaruformat/structs/ddt.h>
#include <aaruformat/structs/flux.h>
#include <aaruformat/structs/index.h>
#include "compression.h"

#define PROGRESS_BAR_WIDTH 40

// ANSI color codes
#define ANSI_RESET       "\033[0m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"
#define ANSI_RED         "\033[31m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_WHITE       "\033[37m"
#define ANSI_BG_BLUE     "\033[44m"
#define ANSI_BG_GREEN    "\033[42m"
#define ANSI_CLEAR_LINE  "\033[2K"

// Check if terminal supports colors
static int use_colors = 0;

static void init_colors(void)
{
    // Check if stdout is a terminal and TERM is set
    use_colors = isatty(STDOUT_FILENO) && getenv("TERM") != NULL;
}

// Color helper function
static const char* clr(const char* code)
{
    return use_colors ? code : "";
}

// External library functions
extern uint64_t aaruf_crc64_data(const uint8_t *data, size_t length);
extern int32_t  aaruf_lzma_decode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                         size_t *src_len, const uint8_t *props, const size_t props_size);

#define LZMA_PROPERTIES_LENGTH 5

// Print progress bar with colors
static void print_progress(const progress_state *state)
{
    if(state->total == 0) return;

    const double percentage = (double)state->current / (double)state->total * 100.0;
    const int    filled     = (int)(percentage / 100.0 * PROGRESS_BAR_WIDTH);

    printf("\r%s%s%s ", clr(ANSI_CLEAR_LINE), clr(ANSI_CYAN), state->label);
    printf("%s[", clr(ANSI_RESET));

    // Draw progress bar with gradient effect
    for(int i = 0; i < PROGRESS_BAR_WIDTH; i++)
    {
        if(i < filled)
            printf("%s█", clr(ANSI_GREEN));
        else if(i == filled && state->current < state->total)
            printf("%s▓", clr(ANSI_YELLOW));
        else
            printf("%s░", clr(ANSI_DIM));
    }

    printf("%s] %s%6.1f%%%s", clr(ANSI_RESET), clr(ANSI_BOLD), percentage, clr(ANSI_RESET));
    fflush(stdout);

    if(state->current >= state->total) printf("\n");
}

// Get current time in nanoseconds
static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Convert nanoseconds to seconds
static double ns_to_seconds(const uint64_t ns) { return (double)ns / 1000000000.0; }

// Format bytes as human-readable string with optional color
static void format_bytes(const uint64_t bytes, char *buffer, const size_t buffer_size)
{
    if(bytes < 1024)
        snprintf(buffer, buffer_size, "%" PRIu64 " B", bytes);
    else if(bytes < 1024 * 1024)
        snprintf(buffer, buffer_size, "%.2f KiB", (double)bytes / 1024.0);
    else if(bytes < 1024 * 1024 * 1024)
        snprintf(buffer, buffer_size, "%.2f MiB", (double)bytes / 1024.0 / 1024.0);
    else
        snprintf(buffer, buffer_size, "%.2f GiB", (double)bytes / 1024.0 / 1024.0 / 1024.0);
}

// Print a section header
static void print_section_header(const char *title)
{
    printf("\n%s%s══════════════════════════════════════════════════════════════%s\n",
           clr(ANSI_BOLD), clr(ANSI_CYAN), clr(ANSI_RESET));
    printf("%s%s  %s%s\n", clr(ANSI_BOLD), clr(ANSI_WHITE), title, clr(ANSI_RESET));
    printf("%s%s══════════════════════════════════════════════════════════════%s\n\n",
           clr(ANSI_BOLD), clr(ANSI_CYAN), clr(ANSI_RESET));
}

// Print a subsection header
static void print_subsection_header(const char *title)
{
    printf("%s%s▶ %s%s\n", clr(ANSI_BOLD), clr(ANSI_YELLOW), title, clr(ANSI_RESET));
    printf("%s──────────────────────────────────────────────────────────────%s\n",
           clr(ANSI_DIM), clr(ANSI_RESET));
}

// Print a key-value pair
static void print_info(const char *key, const char *value)
{
    printf("  %s%-24s%s %s%s%s\n", clr(ANSI_DIM), key, clr(ANSI_RESET), clr(ANSI_WHITE), value, clr(ANSI_RESET));
}

// Print a key-value pair with numeric value
static void print_info_num(const char *key, uint64_t value)
{
    printf("  %s%-24s%s %s%'" PRIu64 "%s\n", clr(ANSI_DIM), key, clr(ANSI_RESET), clr(ANSI_WHITE), value, clr(ANSI_RESET));
}

// Get color for compression ratio
static const char* get_ratio_color(double ratio)
{
    if(ratio < 50.0) return ANSI_GREEN;
    if(ratio < 70.0) return ANSI_YELLOW;
    return ANSI_RED;
}

int main(int argc, char *argv[])
{
    // Initialize color support
    init_colors();

    // Set locale for thousands separator in printf
    setlocale(LC_NUMERIC, "");

    // Print banner
    printf("\n%s%s", clr(ANSI_BOLD), clr(ANSI_CYAN));
    printf("    ╔═══════════════════════════════════════════════════════╗\n");
    printf("    ║       %sAaru Format Compression Benchmark Tool%s          ║\n", clr(ANSI_WHITE), clr(ANSI_CYAN));
    printf("    ╚═══════════════════════════════════════════════════════╝%s\n\n", clr(ANSI_RESET));

    if(argc < 2)
    {
        printf("%sUsage:%s %s <input.aaruformat>\n\n", clr(ANSI_BOLD), clr(ANSI_RESET), argv[0]);
        printf("%sDescription:%s\n", clr(ANSI_BOLD), clr(ANSI_RESET));
        printf("  Benchmark compression algorithms on Aaru format images.\n\n");
        printf("%sAlgorithms tested:%s\n", clr(ANSI_BOLD), clr(ANSI_RESET));
        printf("  %s•%s LZMA      %s(high compression, slow)%s\n", clr(ANSI_GREEN), clr(ANSI_RESET), clr(ANSI_DIM), clr(ANSI_RESET));
        printf("  %s•%s Bzip3     %s(high compression, medium speed)%s\n", clr(ANSI_GREEN), clr(ANSI_RESET), clr(ANSI_DIM), clr(ANSI_RESET));
        printf("  %s•%s Brotli    %s(good compression, medium speed)%s\n", clr(ANSI_GREEN), clr(ANSI_RESET), clr(ANSI_DIM), clr(ANSI_RESET));
        printf("  %s•%s Zstd      %s(good compression, fast)%s\n", clr(ANSI_GREEN), clr(ANSI_RESET), clr(ANSI_DIM), clr(ANSI_RESET));
        printf("  %s•%s Zstd+Dict %s(better compression with trained dictionary)%s\n\n", clr(ANSI_GREEN), clr(ANSI_RESET), clr(ANSI_DIM), clr(ANSI_RESET));
        return 1;
    }

    const char *input_path = argv[1];

    // Open and analyze input image
    print_section_header("Image Analysis");

    printf("  %sOpening:%s %s%s%s\n", clr(ANSI_DIM), clr(ANSI_RESET), clr(ANSI_WHITE), input_path, clr(ANSI_RESET));

    image_info info;
    if(open_image(input_path, &info) != 0)
    {
        printf("\n  %s✗ Failed to open image%s\n", clr(ANSI_RED), clr(ANSI_RESET));
        return 1;
    }

    printf("  %s✓ Image opened successfully%s\n\n", clr(ANSI_GREEN), clr(ANSI_RESET));

    char size_str[64];
    format_bytes(info.total_uncompressed_size, size_str, sizeof(size_str));

    char version_str[32];
    snprintf(version_str, sizeof(version_str), "%u.%u", info.major_version, info.minor_version);
    print_info("Format Version:", version_str);
    print_info_num("Block Count:", info.block_count);
    print_info("Total Size:", size_str);

    // ===== ZSTD DICTIONARY TRAINING PHASE (runs once for all algorithms) =====
    zstd_dict_context *dict_ctx = NULL;

    print_section_header("Zstd Dictionary Training");

    uint64_t sample_target_size = info.total_uncompressed_size / 10;                    // 10%
    if(sample_target_size > 100 * 1024 * 1024) sample_target_size = 100 * 1024 * 1024;  // 100MB max

    uint8_t *sample_buffer = malloc(sample_target_size);
    if(sample_buffer == NULL)
    {
        printf("  %s⚠ Cannot allocate sample buffer for dictionary training%s\n", clr(ANSI_YELLOW), clr(ANSI_RESET));
    }
    else
    {
        uint64_t sample_collected = 0;
        char target_str[64];
        format_bytes(sample_target_size, target_str, sizeof(target_str));
        print_info("Target Sample Size:", target_str);

        // Collect samples from first blocks
        for(uint64_t i = 0; i < info.block_count && sample_collected < sample_target_size; i++)
        {
            IndexEntry *entry       = (IndexEntry *)info.index_entries + i;
            long        block_start = entry->offset;

            if(fseek(info.file, block_start, SEEK_SET) != 0) continue;

            uint32_t identifier;
            if(fread(&identifier, 1, sizeof(uint32_t), info.file) != sizeof(uint32_t)) continue;

            // Collect from benchmarkable blocks
            if(identifier == 0x4B4C4244 ||  // DataBlock
               identifier == 0x2A544444 ||  // DeDuplicationTable (v1)
               identifier == 0x32544444 ||  // DeDuplicationTable2 (v2)
               identifier == 0x4C505344)    // DataStreamPayloadBlock
            {
                // Read the full header based on type to get cmpLength and length
                uint16_t compression;
                uint64_t cmpLength, length;

                fseek(info.file, block_start, SEEK_SET);

                if(identifier == 0x4B4C4244)  // DataBlock
                {
                    BlockHeader block_header;
                    if(fread(&block_header, 1, sizeof(BlockHeader), info.file) != sizeof(BlockHeader)) continue;
                    compression = block_header.compression;
                    cmpLength   = block_header.cmpLength;
                    length      = block_header.length;
                }
                else if(identifier == 0x2A544444)  // DDT v1
                {
                    DdtHeader ddt_header;
                    if(fread(&ddt_header, 1, sizeof(DdtHeader), info.file) != sizeof(DdtHeader)) continue;
                    compression = ddt_header.compression;
                    cmpLength   = ddt_header.cmpLength;
                    length      = ddt_header.length;
                }
                else if(identifier == 0x32544444)  // DDT v2
                {
                    DdtHeader2 ddt_header2;
                    if(fread(&ddt_header2, 1, sizeof(DdtHeader2), info.file) != sizeof(DdtHeader2)) continue;
                    compression = ddt_header2.compression;
                    cmpLength   = ddt_header2.cmpLength;
                    length      = ddt_header2.length;
                }
                else  // DataStreamPayloadBlock (0x4C505344)
                {
                    DataStreamPayloadHeader payload_header;
                    if(fread(&payload_header, 1, sizeof(DataStreamPayloadHeader), info.file) !=
                       sizeof(DataStreamPayloadHeader))
                        continue;
                    compression = payload_header.compression;
                    cmpLength   = payload_header.cmpLength;
                    length      = payload_header.length;
                }

                // Skip empty blocks
                if(length == 0) continue;

                // Allocate buffer for uncompressed data
                uint8_t *uncompressed = malloc(length);
                if(uncompressed == NULL) continue;

                // Decompress data if needed
                if(compression == 1)  // LZMA
                {
                    // Read LZMA properties
                    uint8_t lzma_props[LZMA_PROPERTIES_LENGTH];
                    if(fread(lzma_props, 1, LZMA_PROPERTIES_LENGTH, info.file) != LZMA_PROPERTIES_LENGTH)
                    {
                        free(uncompressed);
                        continue;
                    }

                    // Read compressed data
                    const size_t compressed_size = cmpLength - LZMA_PROPERTIES_LENGTH;
                    uint8_t     *compressed      = malloc(compressed_size);
                    if(compressed == NULL)
                    {
                        free(uncompressed);
                        continue;
                    }

                    if(fread(compressed, 1, compressed_size, info.file) != compressed_size)
                    {
                        free(compressed);
                        free(uncompressed);
                        continue;
                    }

                    // Decompress
                    size_t decompressed_size = length;
                    size_t cmp_size          = compressed_size;
                    if(aaruf_lzma_decode_buffer(uncompressed, &decompressed_size, compressed, &cmp_size, lzma_props,
                                                LZMA_PROPERTIES_LENGTH) != 0)
                    {
                        free(compressed);
                        free(uncompressed);
                        continue;
                    }

                    free(compressed);
                }
                else if(compression == 0)  // Uncompressed
                {
                    if(fread(uncompressed, 1, length, info.file) != length)
                    {
                        free(uncompressed);
                        continue;
                    }
                }
                else  // Skip other compression types
                {
                    free(uncompressed);
                    continue;
                }

                // Add to sample buffer
                uint64_t to_copy =
                    (sample_collected + length > sample_target_size) ? (sample_target_size - sample_collected) : length;
                if(to_copy > 0)
                {
                    memcpy(sample_buffer + sample_collected, uncompressed, to_copy);
                    sample_collected += to_copy;
                }

                free(uncompressed);
            }
        }

        char collected_str[64], dict_size_str[64];
        format_bytes(sample_collected, collected_str, sizeof(collected_str));
        print_info("Collected:", collected_str);

        if(sample_collected > 0)
        {
            printf("  %sTraining...%s", clr(ANSI_DIM), clr(ANSI_RESET));
            fflush(stdout);

            dict_ctx = train_zstd_dictionary(sample_buffer, sample_collected, 512 * 1024);  // 512KB dict

            printf("\r%s", clr(ANSI_CLEAR_LINE));  // Clear the "Training..." line

            if(dict_ctx != NULL)
            {
                format_bytes(dict_ctx->dict_size, dict_size_str, sizeof(dict_size_str));
                print_info("Dictionary Size:", dict_size_str);
                printf("  %s✓ Dictionary trained successfully%s\n", clr(ANSI_GREEN), clr(ANSI_RESET));
            }
            else
            {
                printf("  %s⚠ Dictionary training failed%s\n", clr(ANSI_YELLOW), clr(ANSI_RESET));
            }
        }
        else
        {
            printf("  %s⚠ No samples collected for dictionary training%s\n", clr(ANSI_YELLOW), clr(ANSI_RESET));
        }

        free(sample_buffer);
    }
    // ===== END DICTIONARY TRAINING =====

    // Test each compression algorithm
    const compression_algorithm algorithms[]      = {COMP_LZMA, COMP_BZIP3, COMP_BROTLI, COMP_ZSTD, COMP_ZSTD};
    const char                 *algorithm_names[] = {"LZMA", "Bzip3", "Brotli", "Zstd (no dict)", "Zstd (with dict)"};
    const size_t                algorithm_count   = sizeof(algorithms) / sizeof(algorithms[0]);

    benchmark_result results[5];

    print_section_header("Compression Benchmarks");

    for(size_t i = 0; i < algorithm_count; i++)
    {
        print_subsection_header(algorithm_names[i]);

        char output_path[512];
        snprintf(output_path, sizeof(output_path), "%s.%s.aaruformat", input_path, algorithm_names[i]);

        const uint64_t start_time = get_time_ns();

        progress_state progress = {0, info.block_count, {0}};
        snprintf(progress.label, sizeof(progress.label), "Compressing");

        // Pass dictionary only for fifth run (Zstd with dict)
        const zstd_dict_context *use_dict = (i == 4) ? dict_ctx : NULL;

        if(benchmark_compression(input_path, output_path, algorithms[i], &info, &results[i], &progress, print_progress,
                                 use_dict) != 0)
        {
            printf("  %s✗ Failed to benchmark%s\n", clr(ANSI_RED), clr(ANSI_RESET));
            if(dict_ctx) free_zstd_dictionary(dict_ctx);
            close_image(&info);
            return 1;
        }

        results[i].elapsed_ns = get_time_ns() - start_time;

        // Print results for this algorithm
        char compressed_str[64];
        format_bytes(results[i].compressed_size, compressed_str, sizeof(compressed_str));
        double ratio = (double)results[i].compressed_size / (double)info.total_uncompressed_size * 100.0;

        printf("  %sTime:%s         %s%.2f%s seconds\n", clr(ANSI_DIM), clr(ANSI_RESET),
               clr(ANSI_WHITE), ns_to_seconds(results[i].elapsed_ns), clr(ANSI_RESET));
        printf("  %sCompressed:%s   %s%s%s\n", clr(ANSI_DIM), clr(ANSI_RESET),
               clr(ANSI_WHITE), compressed_str, clr(ANSI_RESET));
        printf("  %sRatio:%s        %s%s%.2f%%%s\n\n", clr(ANSI_DIM), clr(ANSI_RESET),
               clr(ANSI_BOLD), clr(get_ratio_color(ratio)), ratio, clr(ANSI_RESET));
    }

    // Cleanup dictionary
    if(dict_ctx != NULL) free_zstd_dictionary(dict_ctx);

    // Print summary table
    print_section_header("Results Summary");

    // Table header
    printf("  %s%-16s %20s %20s %12s %10s%s\n", clr(ANSI_BOLD),
           "Algorithm", "Uncompressed (B)", "Compressed (B)", "Ratio", "Time (s)", clr(ANSI_RESET));
    printf("  %s────────────────────────────────────────────────────────────────────────────────%s\n",
           clr(ANSI_DIM), clr(ANSI_RESET));

    for(size_t i = 0; i < algorithm_count; i++)
    {
        const double ratio  = (double)results[i].compressed_size / (double)info.total_uncompressed_size * 100.0;
        const double time_s = ns_to_seconds(results[i].elapsed_ns);

        printf("  %s%-16s%s %'20" PRIu64 " %'20" PRIu64 " %s%s%11.2f%%%s %10.2f\n",
               clr(ANSI_WHITE), algorithm_names[i], clr(ANSI_RESET),
               info.total_uncompressed_size, results[i].compressed_size,
               clr(ANSI_BOLD), clr(get_ratio_color(ratio)), ratio, clr(ANSI_RESET), time_s);
    }

    // Find best compression
    size_t   best_compression_idx = 0;
    uint64_t best_compressed_size = results[0].compressed_size;
    for(size_t i = 1; i < algorithm_count; i++)
    {
        if(results[i].compressed_size < best_compressed_size)
        {
            best_compressed_size = results[i].compressed_size;
            best_compression_idx = i;
        }
    }

    // Find fastest
    size_t   fastest_idx  = 0;
    uint64_t fastest_time = results[0].elapsed_ns;
    for(size_t i = 1; i < algorithm_count; i++)
    {
        if(results[i].elapsed_ns < fastest_time)
        {
            fastest_time = results[i].elapsed_ns;
            fastest_idx  = i;
        }
    }

    printf("\n  %s────────────────────────────────────────────────────────────────────────────────%s\n",
           clr(ANSI_DIM), clr(ANSI_RESET));
    printf("  %s🏆 Best Compression:%s %s%s%s\n", clr(ANSI_BOLD), clr(ANSI_RESET),
           clr(ANSI_GREEN), algorithm_names[best_compression_idx], clr(ANSI_RESET));
    printf("  %s⚡ Fastest:%s         %s%s%s\n\n", clr(ANSI_BOLD), clr(ANSI_RESET),
           clr(ANSI_CYAN), algorithm_names[fastest_idx], clr(ANSI_RESET));

    close_image(&info);
    return 0;
}
