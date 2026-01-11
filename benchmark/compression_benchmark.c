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

#include <aaruformat/consts.h>
#include <aaruformat/structs/data.h>
#include <aaruformat/structs/ddt.h>
#include <aaruformat/structs/flux.h>
#include <aaruformat/structs/header.h>
#include <aaruformat/structs/index.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "benchmark.h"
#include "compression.h"

// CRC64 implementation (from library)
extern uint64_t aaruf_crc64_data(const uint8_t *data, size_t length);

// LZMA decompression (from library)
extern int32_t aaruf_lzma_decode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                        size_t *src_len, const uint8_t *props, const size_t props_size);

// FLAC decompression (from library)
extern size_t aaruf_flac_decode_redbook_buffer(uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer,
                                               size_t src_size);

// CST (Claunia Subchannel Transform) functions (from library)
extern int32_t aaruf_cst_transform(const uint8_t *interleaved, uint8_t *sequential, size_t length);
extern int32_t aaruf_cst_untransform(const uint8_t *sequential, uint8_t *interleaved, size_t length);

// Benchmark compression algorithm on an image
int benchmark_compression(const char *input_path, const char *output_path, const compression_algorithm algorithm,
                          image_info *info, benchmark_result *result, progress_state *progress,
                          progress_callback progress_cb, const zstd_dict_context *dict_ctx)
{
    memset(result, 0, sizeof(benchmark_result));

    // Open output file
    FILE *output = fopen(output_path, "wb");
    if(output == NULL)
    {
        fprintf(stderr, "Error: Cannot create output file %s\n", output_path);
        return -1;
    }

    // Read original header from input
    if(fseek(info->file, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot seek to header\n");
        fclose(output);
        return -1;
    }

    AaruHeaderV2 header;
    if(fread(&header, 1, sizeof(AaruHeaderV2), info->file) != sizeof(AaruHeaderV2))
    {
        fprintf(stderr, "Error: Cannot read header\n");
        fclose(output);
        return -1;
    }

    // Write placeholder header (will update later)
    if(fwrite(&header, 1, sizeof(AaruHeaderV2), output) != sizeof(AaruHeaderV2))
    {
        fprintf(stderr, "Error: Cannot write header\n");
        fclose(output);
        return -1;
    }

    // Prepare new index entries
    IndexEntry *old_entries = (IndexEntry *)info->index_entries;
    IndexEntry *new_entries = malloc(info->block_count * sizeof(IndexEntry));
    if(new_entries == NULL)
    {
        fprintf(stderr, "Error: Cannot allocate memory for new index entries\n");
        fclose(output);
        return -1;
    }

    memcpy(new_entries, old_entries, info->block_count * sizeof(IndexEntry));

    // Process each block
    uint64_t current_position = ftell(output);
    progress->current         = 0;

    for(uint64_t i = 0; i < info->block_count; i++)
    {
        IndexEntry *entry = &old_entries[i];

        // Seek to block
        if(fseek(info->file, entry->offset, SEEK_SET) != 0)
        {
            fprintf(stderr, "Error: Cannot seek to block %" PRIu64 "\n", i);
            free(new_entries);
            fclose(output);
            return -1;
        }

        // Read block identifier to determine type
        uint32_t identifier;
        long     block_start = ftell(info->file);
        if(fread(&identifier, 1, sizeof(uint32_t), info->file) != sizeof(uint32_t))
        {
            fprintf(stderr, "Error: Cannot read block identifier %" PRIu64 "\n", i);
            free(new_entries);
            fclose(output);
            return -1;
        }
        fseek(info->file, block_start, SEEK_SET);  // Rewind

        // Calculate block size by looking at next entry or EOF
        size_t block_size;
        if(i + 1 < info->block_count) { block_size = old_entries[i + 1].offset - entry->offset; }
        else
        {
            // Last block - read to EOF (excluding index)
            block_size = info->index_offset - entry->offset;
        }

        // Process blocks with compression: DataBlock, DDT (v1/v2), DataStreamPayload
        if(identifier == 0x4B4C4244 ||  // DataBlock
           identifier == 0x2A544444 ||  // DeDuplicationTable (v1)
           identifier == 0x32544444 ||  // DeDuplicationTable2 (v2)
           identifier == 0x4C505344)    // DataStreamPayloadBlock
        {
            // These blocks all share: identifier(4), type(2), compression(2), then data
            // Read the common header fields
            uint16_t type, compression;
            fseek(info->file, block_start + 4, SEEK_SET);
            if(fread(&type, 1, sizeof(uint16_t), info->file) != sizeof(uint16_t) ||
               fread(&compression, 1, sizeof(uint16_t), info->file) != sizeof(uint16_t))
            {
                fprintf(stderr, "Error: Cannot read block header fields\n");
                free(new_entries);
                fclose(output);
                return -1;
            }

            // Read the full header based on type to get cmpLength and length
            uint64_t cmpLength, length, cmpCrc64, crc64;
            size_t   header_size;

            fseek(info->file, block_start, SEEK_SET);

            if(identifier == 0x4B4C4244)  // DataBlock
            {
                BlockHeader block_header;
                if(fread(&block_header, 1, sizeof(BlockHeader), info->file) != sizeof(BlockHeader))
                {
                    fprintf(stderr, "Error: Cannot read BlockHeader\n");
                    free(new_entries);
                    fclose(output);
                    return -1;
                }
                header_size = sizeof(BlockHeader);
                cmpLength   = block_header.cmpLength;
                length      = block_header.length;
                cmpCrc64    = block_header.cmpCrc64;
                crc64       = block_header.crc64;
            }
            else if(identifier == 0x2A544444)  // DDT v1
            {
                DdtHeader ddt_header;
                if(fread(&ddt_header, 1, sizeof(DdtHeader), info->file) != sizeof(DdtHeader))
                {
                    fprintf(stderr, "Error: Cannot read DdtHeader\n");
                    free(new_entries);
                    fclose(output);
                    return -1;
                }
                header_size = sizeof(DdtHeader);
                compression = ddt_header.compression;
                cmpLength   = ddt_header.cmpLength;
                length      = ddt_header.length;
                cmpCrc64    = ddt_header.cmpCrc64;
                crc64       = ddt_header.crc64;
            }
            else  // DDT v2 (0x32544444) or DataStreamPayload (0x4C505344)
            {
                if(identifier == 0x32544444)  // DDT v2
                {
                    DdtHeader2 ddt_header2;
                    if(fread(&ddt_header2, 1, sizeof(DdtHeader2), info->file) != sizeof(DdtHeader2))
                    {
                        fprintf(stderr, "Error: Cannot read DdtHeader2\n");
                        free(new_entries);
                        fclose(output);
                        return -1;
                    }
                    header_size = sizeof(DdtHeader2);
                    compression = ddt_header2.compression;
                    cmpLength   = ddt_header2.cmpLength;
                    length      = ddt_header2.length;
                    cmpCrc64    = ddt_header2.cmpCrc64;
                    crc64       = ddt_header2.crc64;
                }
                else  // DataStreamPayloadBlock (0x4C505344)
                {
                    DataStreamPayloadHeader payload_header;
                    if(fread(&payload_header, 1, sizeof(DataStreamPayloadHeader), info->file) !=
                       sizeof(DataStreamPayloadHeader))
                    {
                        fprintf(stderr, "Error: Cannot read DataStreamPayloadHeader\n");
                        free(new_entries);
                        fclose(output);
                        return -1;
                    }
                    header_size = sizeof(DataStreamPayloadHeader);
                    compression = payload_header.compression;
                    cmpLength   = payload_header.cmpLength;
                    length      = payload_header.length;
                    cmpCrc64    = payload_header.cmpCrc64;
                    crc64       = payload_header.crc64;
                }
            }

            // Allocate buffer for uncompressed data
            uint8_t *uncompressed = malloc(length);
            if(uncompressed == NULL)
            {
                fprintf(stderr, "Error: Cannot allocate uncompressed buffer\n");
                free(new_entries);
                fclose(output);
                return -1;
            }

            // Decompress data if needed
            if(compression == 1)  // LZMA
            {
                // Read LZMA properties
                uint8_t lzma_props[LZMA_PROPERTIES_LENGTH];
                if(fread(lzma_props, 1, LZMA_PROPERTIES_LENGTH, info->file) != LZMA_PROPERTIES_LENGTH)
                {
                    fprintf(stderr, "Error: Cannot read LZMA properties\n");
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                // Read compressed data
                const size_t compressed_size = cmpLength - LZMA_PROPERTIES_LENGTH;
                uint8_t     *compressed      = malloc(compressed_size);
                if(compressed == NULL)
                {
                    fprintf(stderr, "Error: Cannot allocate compressed buffer\n");
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                if(fread(compressed, 1, compressed_size, info->file) != compressed_size)
                {
                    fprintf(stderr, "Error: Cannot read compressed data\n");
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                // Decompress
                size_t decompressed_size = length;
                size_t cmp_size          = compressed_size;
                if(aaruf_lzma_decode_buffer(uncompressed, &decompressed_size, compressed, &cmp_size, lzma_props,
                                            LZMA_PROPERTIES_LENGTH) != 0)
                {
                    fprintf(stderr, "Error: LZMA decompression failed for block %" PRIu64 "\n", i);
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                free(compressed);
            }
            else if(compression == 2)  // FLAC
            {
                // Read FLAC compressed data
                uint8_t *compressed = malloc(cmpLength);
                if(compressed == NULL)
                {
                    fprintf(stderr, "Error: Cannot allocate compressed buffer for FLAC\n");
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                if(fread(compressed, 1, cmpLength, info->file) != cmpLength)
                {
                    fprintf(stderr, "Error: Cannot read FLAC compressed data\n");
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                // Decompress FLAC (returns bytes written)
                const size_t decompressed_size =
                    aaruf_flac_decode_redbook_buffer(uncompressed, length, compressed, cmpLength);
                if(decompressed_size != length)
                {
                    fprintf(stderr, "Error: FLAC decompression failed for block %" PRIu64 " (expected %zu, got %zu)\n",
                            i, (size_t)length, decompressed_size);
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                free(compressed);
            }
            else if(compression == 3)  // LZMA with Claunia Subchannel Transform
            {
                // Read LZMA properties
                uint8_t lzma_props[LZMA_PROPERTIES_LENGTH];
                if(fread(lzma_props, 1, LZMA_PROPERTIES_LENGTH, info->file) != LZMA_PROPERTIES_LENGTH)
                {
                    fprintf(stderr, "Error: Cannot read LZMA properties for LZMA+CST\n");
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                // Read compressed data
                const size_t compressed_size = cmpLength - LZMA_PROPERTIES_LENGTH;
                uint8_t     *compressed      = malloc(compressed_size);
                if(compressed == NULL)
                {
                    fprintf(stderr, "Error: Cannot allocate compressed buffer for LZMA+CST\n");
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                if(fread(compressed, 1, compressed_size, info->file) != compressed_size)
                {
                    fprintf(stderr, "Error: Cannot read LZMA+CST compressed data\n");
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                // Decompress LZMA first (into a temporary buffer for CST sequential data)
                uint8_t *cst_sequential = malloc(length);
                if(cst_sequential == NULL)
                {
                    fprintf(stderr, "Error: Cannot allocate CST sequential buffer\n");
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                size_t decompressed_size = length;
                size_t cmp_size          = compressed_size;
                if(aaruf_lzma_decode_buffer(cst_sequential, &decompressed_size, compressed, &cmp_size, lzma_props,
                                            LZMA_PROPERTIES_LENGTH) != 0)
                {
                    fprintf(stderr, "Error: LZMA decompression failed for LZMA+CST block %" PRIu64 "\n", i);
                    free(cst_sequential);
                    free(compressed);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                free(compressed);

                // Untransform CST (sequential -> interleaved)
                if(aaruf_cst_untransform(cst_sequential, uncompressed, length) != 0)
                {
                    fprintf(stderr, "Error: CST untransform failed for block %" PRIu64 "\n", i);
                    free(cst_sequential);
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }

                free(cst_sequential);
            }
            else if(compression == 0)  // None
            {
                // Read uncompressed data directly
                if(fread(uncompressed, 1, length, info->file) != length)
                {
                    fprintf(stderr, "Error: Cannot read uncompressed data\n");
                    free(uncompressed);
                    free(new_entries);
                    fclose(output);
                    return -1;
                }
            }
            else
            {
                fprintf(stderr,
                        "Warning: Unsupported compression type %u for block %" PRIu64
                        " (supported: 0=None, 1=LZMA, 2=FLAC, 3=LZMA+CST), copying as-is\n",
                        compression, i);
                free(uncompressed);
                // Fall through to copy block as-is
                goto copy_block_asis;
            }

            // Now recompress with the test algorithm (only for DataBlock and DataStreamPayload)
            uint8_t *recompressed      = NULL;
            size_t   recompressed_size = 0;
            int      new_compression   = compression;         // Default: keep original
            bool     had_cst           = (compression == 3);  // Track if original had CST

            // Benchmark DataBlocks, DataStreamPayload, and DDT blocks for compression
            if((identifier == 0x4B4C4244 || identifier == 0x4C505344 || identifier == 0x2A544444 ||
                identifier == 0x32544444))  // Include DDT v1 and v2
            {
                uint8_t *data_to_compress      = uncompressed;
                size_t   data_to_compress_size = length;
                uint8_t *cst_transformed       = NULL;

                // If original had CST, apply CST transform before compressing
                if(had_cst)
                {
                    cst_transformed = malloc(length);
                    if(cst_transformed == NULL)
                    {
                        fprintf(stderr, "Error: Cannot allocate CST transform buffer\n");
                        free(uncompressed);
                        free(new_entries);
                        fclose(output);
                        return -1;
                    }

                    if(aaruf_cst_transform(uncompressed, cst_transformed, length) != 0)
                    {
                        fprintf(stderr, "Error: CST transform failed for block %" PRIu64 "\n", i);
                        free(cst_transformed);
                        free(uncompressed);
                        free(new_entries);
                        fclose(output);
                        return -1;
                    }

                    data_to_compress = cst_transformed;
                }

                // Try test compression algorithm on the (possibly CST-transformed) data
                // Use Zstd with dictionary if available, otherwise use standard compression
                if(algorithm == COMP_ZSTD && dict_ctx != NULL)
                {
                    if(compress_data_zstd_dict(data_to_compress, data_to_compress_size, &recompressed,
                                               &recompressed_size, dict_ctx) == 0)
                    {
                        // Check if compression is beneficial
                        if(recompressed_size < data_to_compress_size)
                        {
                            new_compression = had_cst ? 3 : 101;  // 101 is zstd identifier
                        }
                        else
                        {
                            // Compression not beneficial, use uncompressed
                            free(recompressed);
                            recompressed      = uncompressed;
                            recompressed_size = length;
                            new_compression   = 0;
                            uncompressed      = NULL;
                        }
                    }
                    else
                    {
                        // Compression failed, use original compression type
                        recompressed      = data_to_compress;
                        recompressed_size = data_to_compress_size;
                        new_compression   = compression;

                        if(had_cst) { cst_transformed = NULL; }
                        else
                        {
                            uncompressed = NULL;
                        }
                    }
                }
                else
                {
                    // Use standard compression algorithm (LZMA, Bzip3, or Zstd without dictionary)
                    if(compress_data(algorithm, data_to_compress, data_to_compress_size, &recompressed,
                                     &recompressed_size) == 0)
                    {
                        // Check if compression is beneficial
                        if(recompressed_size < data_to_compress_size)
                        {
                            int base_compression = get_compression_type(algorithm);
                            new_compression      = had_cst ? 3 : base_compression;
                        }
                        else
                        {
                            // Compression not beneficial, use uncompressed
                            free(recompressed);
                            recompressed      = uncompressed;
                            recompressed_size = length;
                            new_compression   = 0;
                            uncompressed      = NULL;
                        }
                    }
                    else
                    {
                        // Compression failed, use original compression type
                        recompressed      = data_to_compress;
                        recompressed_size = data_to_compress_size;
                        new_compression   = compression;

                        if(had_cst) { cst_transformed = NULL; }
                        else
                        {
                            uncompressed = NULL;
                        }
                    }
                }

                // Clean up CST buffer if not used
                if(cst_transformed && cst_transformed != recompressed) free(cst_transformed);
                // Clean up uncompressed if CST was used and not recompressed
                if(had_cst && uncompressed && uncompressed != recompressed) free(uncompressed);
            }
            else
            {
                // Keep DDT blocks unchanged
                recompressed      = uncompressed;
                recompressed_size = length;
                new_compression   = compression;
                uncompressed      = NULL;
            }

            // Write the block back with original header structure
            new_entries[i].offset = current_position;

            fseek(info->file, block_start, SEEK_SET);
            uint8_t *header_buffer = malloc(header_size);
            if(fread(header_buffer, 1, header_size, info->file) != header_size)
            {
                fprintf(stderr, "Error: Cannot re-read header\n");
                free(header_buffer);
                free(recompressed);
                if(uncompressed) free(uncompressed);
                free(new_entries);
                fclose(output);
                return -1;
            }

            // Update compression fields in the header buffer
            if(identifier == 0x4B4C4244)  // DataBlock
            {
                BlockHeader *bhdr = (BlockHeader *)header_buffer;
                bhdr->compression = new_compression;
                bhdr->cmpLength   = recompressed_size;
                bhdr->cmpCrc64    = aaruf_crc64_data(recompressed, recompressed_size);
            }
            else if(identifier == 0x2A544444)  // DDT v1
            {
                DdtHeader *dhdr   = (DdtHeader *)header_buffer;
                dhdr->compression = new_compression;
                dhdr->cmpLength   = recompressed_size;
                dhdr->cmpCrc64    = aaruf_crc64_data(recompressed, recompressed_size);
            }
            else if(identifier == 0x32544444)  // DDT v2
            {
                DdtHeader2 *dhdr2  = (DdtHeader2 *)header_buffer;
                dhdr2->compression = new_compression;
                dhdr2->cmpLength   = recompressed_size;
                dhdr2->cmpCrc64    = aaruf_crc64_data(recompressed, recompressed_size);
            }
            else  // DataStreamPayloadBlock (0x4C505344)
            {
                DataStreamPayloadHeader *phdr = (DataStreamPayloadHeader *)header_buffer;
                phdr->compression             = new_compression;
                phdr->cmpLength               = recompressed_size;
                phdr->cmpCrc64                = aaruf_crc64_data(recompressed, recompressed_size);
            }

            // Write header
            if(fwrite(header_buffer, 1, header_size, output) != header_size)
            {
                fprintf(stderr, "Error: Cannot write block header\n");
                free(header_buffer);
                free(recompressed);
                if(uncompressed) free(uncompressed);
                free(new_entries);
                fclose(output);
                return -1;
            }
            free(header_buffer);

            // Write data
            if(fwrite(recompressed, 1, recompressed_size, output) != recompressed_size)
            {
                fprintf(stderr, "Error: Cannot write block data\n");
                free(recompressed);
                if(uncompressed) free(uncompressed);
                free(new_entries);
                fclose(output);
                return -1;
            }

            current_position += header_size + recompressed_size;
            // Count all benchmarked blocks in compressed size (DataBlock, DataStreamPayload, DDT v1/v2)
            if(identifier == 0x4B4C4244 || identifier == 0x4C505344 || identifier == 0x2A544444 ||
               identifier == 0x32544444)
                result->compressed_size += recompressed_size;

            free(recompressed);
            if(uncompressed) free(uncompressed);
        }
        else
        {
        copy_block_asis:
            // Copy all other blocks as-is
            fseek(info->file, block_start, SEEK_SET);

            uint8_t *block_buffer = malloc(block_size);
            if(block_buffer == NULL)
            {
                fprintf(stderr, "Error: Cannot allocate block buffer\n");
                free(new_entries);
                fclose(output);
                return -1;
            }

            if(fread(block_buffer, 1, block_size, info->file) != block_size)
            {
                fprintf(stderr, "Error: Cannot read block %" PRIu64 "\n", i);
                free(block_buffer);
                free(new_entries);
                fclose(output);
                return -1;
            }

            new_entries[i].offset = current_position;
            if(fwrite(block_buffer, 1, block_size, output) != block_size)
            {
                fprintf(stderr, "Error: Cannot write block %" PRIu64 "\n", i);
                free(block_buffer);
                free(new_entries);
                fclose(output);
                return -1;
            }

            free(block_buffer);
            current_position += block_size;
        }

        // Update progress
        progress->current++;
        if(progress_cb) progress_cb(progress);
    }

    // ===== WRITE DICTIONARY BLOCK (Zstd only) =====
    if(algorithm == COMP_ZSTD && dict_ctx != NULL && dict_ctx->dict_data != NULL && dict_ctx->dict_size > 0)
    {
        printf("\nWriting Zstd dictionary block: %zu bytes\n", dict_ctx->dict_size);

        // Create a DataBlock with new datatype for dictionary
        BlockHeader dict_block_header;
        dict_block_header.identifier  = 0x4B4C4244;  // DataBlock
        dict_block_header.type        = 99;          // Custom datatype for dictionary
        dict_block_header.compression = 101;         // Zstd
        dict_block_header.sectorSize  = 512;
        dict_block_header.length      = dict_ctx->dict_size;
        dict_block_header.cmpLength   = dict_ctx->dict_size;  // Not further compressed
        dict_block_header.crc64       = aaruf_crc64_data(dict_ctx->dict_data, dict_ctx->dict_size);
        dict_block_header.cmpCrc64    = dict_block_header.crc64;

        // Write dictionary block
        fseek(output, current_position, SEEK_SET);
        if(fwrite(&dict_block_header, sizeof(BlockHeader), 1, output) != 1)
        {
            fprintf(stderr, "Error: Cannot write dictionary block header\n");
            free_zstd_dictionary(dict_ctx);
            free(new_entries);
            fclose(output);
            return -1;
        }

        if(fwrite(dict_ctx->dict_data, dict_ctx->dict_size, 1, output) != 1)
        {
            fprintf(stderr, "Error: Cannot write dictionary block data\n");
            free_zstd_dictionary(dict_ctx);
            free(new_entries);
            fclose(output);
            return -1;
        }

        // Include dictionary in result size
        result->compressed_size += dict_ctx->dict_size;
        current_position += sizeof(BlockHeader) + dict_ctx->dict_size;
        printf("Dictionary block written at offset %" PRIu64 "\n",
               current_position - sizeof(BlockHeader) - dict_ctx->dict_size);
    }
    // ===== END DICTIONARY BLOCK =====

    // Write new index (use IndexBlock3 to match modern images)
    const uint64_t new_index_offset = current_position;

    IndexHeader3 index_header;
    index_header.identifier = 0x33584449;  // IndexBlock3
    index_header.entries    = info->block_count;
    index_header.crc64      = aaruf_crc64_data((uint8_t *)new_entries, info->block_count * sizeof(IndexEntry));
    index_header.previous   = 0;  // No chaining for now

    if(fwrite(&index_header, 1, sizeof(IndexHeader3), output) != sizeof(IndexHeader3))
    {
        fprintf(stderr, "Error: Cannot write index header\n");
        free(new_entries);
        fclose(output);
        return -1;
    }

    if(fwrite(new_entries, sizeof(IndexEntry), info->block_count, output) != info->block_count)
    {
        fprintf(stderr, "Error: Cannot write index entries\n");
        free(new_entries);
        fclose(output);
        return -1;
    }

    // Update header with new index offset
    header.indexOffset = new_index_offset;
    if(fseek(output, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Cannot seek to header\n");
        free(new_entries);
        fclose(output);
        return -1;
    }

    if(fwrite(&header, 1, sizeof(AaruHeaderV2), output) != sizeof(AaruHeaderV2))
    {
        fprintf(stderr, "Error: Cannot update header\n");
        free(new_entries);
        fclose(output);
        return -1;
    }

    free(new_entries);
    fclose(output);

    // Get final file size
    struct stat st;
    if(stat(output_path, &st) == 0) { result->compressed_size = st.st_size; }

    return 0;
}
