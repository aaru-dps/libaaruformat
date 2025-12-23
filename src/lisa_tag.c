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
 * @file lisa_tag.c
 * @brief Parsing, conversion and serialization helpers for Lisa (Apple Lisa / Profile / Priam / Sony)
 *        disk block tags.
 *
 * The Lisa operating system and related peripheral disk formats (Sony 3.5" diskettes, ProFile hard disks,
 * Priam drives) store per-block (or per-page) metadata in compact on-disk "tag" records. These records use
 * mixed-size big-endian integer fields and bit fields to describe allocation chains, relative/absolute page
 * numbering, volume numbers, versions, and (for some variants) checksums and disk sizing.
 *
 * This module provides:
 *  - Parsing raw big-endian on-disk tag byte sequences into the in-memory structures: ::sony_tag,
 *    ::profile_tag, ::priam_tag.
 *  - Lossless (within each format's limits) conversion between the different tag structure variants,
 *    mapping sentinel values (e.g. 0x7FF <-> 0xFFFFFF for end-of-chain markers) appropriately.
 *  - Serialization of the in-memory structures back into their canonical on-disk packed byte layouts.
 *
 * Endianness: All multi-byte numeric fields in the on-disk tag formats are stored big-endian. The helper
 * routines perform the necessary byte shifting and masking; the resulting structure fields are in host
 * endianness.
 *
 * Memory management: The serialization functions ( *_tag_to_bytes ) allocate new byte buffers using
 * calloc(). The caller owns the returned pointer and must free() it when no longer needed. Parsing
 * functions do not allocate dynamic memory; they return structures by value.
 *
 * Validation: For performance and low-level usage reasons, the parsing routines do not perform NULL
 * pointer checks nor length validation on the input byte arrays. Supplying a NULL pointer or insufficient
 * bytes is undefined behavior. If such safety is required, perform validation before calling these
 * functions.
 *
 * Sentinel mapping: Sony tag next/prev block fields are 11-bit values (masked with 0x7FF) with the value
 * 0x7FF meaning end-of-chain (or no previous). In the wider Profile/Priam formats these become 24-bit
 * fields, with 0xFFFFFF used for the same semantic meaning. Conversion helpers transparently translate
 * between these sentinel values.
 *
 * Bit fields: The structures define C bit fields for compact representation (e.g., kind, reserved,
 * valid_chk, used_bytes). The parsing and serialization routines reconstruct / pack these manually through
 * masking and shifting because on-disk layout may not match compiler bit-field packing. This provides
 * portability irrespective of platform-specific bit-field ordering.
 */

#include <stdint.h>

#include "aaruformat/structs/lisa_tag.h"

#include <stdlib.h>

#define SONY_BLOCK_NUMBER_MASK  0x07FFU
#define SONY_BLOCK_VALID_FLAG   0x8000U
#define SONY_BLOCK_ALLOWED_MASK (SONY_BLOCK_VALID_FLAG | SONY_BLOCK_NUMBER_MASK)  ///< Preserve valid bit + 11-bit index

/**
 * @brief Parse a 12-byte Sony tag record into a ::sony_tag structure.
 *
 * Field extraction follows the documented on-disk layout (big-endian). The next/prev block numbers are
 * masked to 11 bits (0x7FF) since only those bits are defined in the Sony tag format. The reserved bits
 * are preserved in the structure's reserved member for round-tripping if needed.
 *
 * @param bytes Pointer to at least 12 consecutive bytes containing a raw Sony tag as read from disk.
 * @return A populated ::sony_tag structure with host-endian integer fields.
 * @warning Behavior is undefined if bytes is NULL or does not point to at least 12 readable bytes.
 * @note No validation of version, kind, or chain linkage consistency is performed.
 */
sony_tag bytes_to_sony_tag(const uint8_t *bytes)
{
    sony_tag tag = {0};

    tag.version             = (uint16_t)(bytes[0] << 8 | bytes[1]);
    tag.kind                = (bytes[2] & 0xC0) >> 6;
    tag.reserved            = bytes[2] & 0x3F;
    tag.volume              = bytes[3];
    tag.file_id             = (int16_t)(bytes[4] << 8 | bytes[5]);
    tag.rel_page            = (uint16_t)(bytes[6] << 8 | bytes[7]);
    const uint16_t next_raw = (uint16_t)((uint16_t)bytes[8] << 8 | bytes[9]);
    const uint16_t prev_raw = (uint16_t)((uint16_t)bytes[10] << 8 | bytes[11]);

    tag.next_block = next_raw & SONY_BLOCK_ALLOWED_MASK;
    tag.prev_block = prev_raw & SONY_BLOCK_ALLOWED_MASK;

    return tag;
}

/**
 * @brief Parse a 24-byte Priam tag record into a ::priam_tag structure.
 *
 * Priam tags extend the Profile tag with an additional 4-byte disk_size field. This routine decodes all bit fields,
 * separating the valid checksum flag (bit 7 of byte 6) from the 15-bit used_bytes value.
 *
 * @param bytes Pointer to at least 24 consecutive bytes containing a raw Priam tag.
 * @return A populated ::priam_tag structure.
 * @warning Undefined behavior if bytes is NULL or shorter than 24 bytes.
 * @note The abs_page, next_block and prev_block 24-bit values are expanded into 32-bit integers in host memory.
 */
priam_tag bytes_to_priam_tag(const uint8_t *bytes)
{
    priam_tag tag = {0};

    tag.version    = (uint16_t)(bytes[0] << 8 | bytes[1]);
    tag.kind       = (bytes[2] & 0xC0) >> 6;
    tag.reserved   = bytes[2] & 0x3F;
    tag.volume     = bytes[3];
    tag.file_id    = (int16_t)(bytes[4] << 8 | bytes[5]);
    tag.used_bytes = (uint16_t)((bytes[6] & 0x7F) << 8 | bytes[7]);
    tag.valid_chk  = (bytes[6] & 0x80) != 0;
    tag.abs_page   = (uint32_t)(bytes[8] << 16 | bytes[9] << 8 | bytes[10]);
    tag.checksum   = bytes[11];
    tag.rel_page   = (uint16_t)(bytes[12] << 8 | bytes[13]);
    tag.next_block = (uint32_t)(bytes[14] << 16 | bytes[15] << 8 | bytes[16]);
    tag.prev_block = (uint32_t)(bytes[17] << 16 | bytes[18] << 8 | bytes[19]);
    tag.disk_size  = (uint32_t)(bytes[20] << 24 | bytes[21] << 16 | bytes[22] << 8 | bytes[23]);

    return tag;
}

/**
 * @brief Parse a 20-byte Profile tag record into a ::profile_tag structure.
 *
 * A Profile tag is similar to Priam but without the trailing disk_size field. Multi-byte numeric values are big-endian.
 *
 * @param bytes Pointer to at least 20 consecutive bytes containing a raw Profile tag.
 * @return A populated ::profile_tag structure.
 * @warning Undefined behavior if bytes is NULL or shorter than 20 bytes.
 */
profile_tag bytes_to_profile_tag(const uint8_t *bytes)
{
    profile_tag tag = {0};

    tag.version    = (uint16_t)(bytes[0] << 8 | bytes[1]);
    tag.kind       = (bytes[2] & 0xC0) >> 6;
    tag.reserved   = bytes[2] & 0x3F;
    tag.volume     = bytes[3];
    tag.file_id    = (int16_t)(bytes[4] << 8 | bytes[5]);
    tag.used_bytes = (uint16_t)((bytes[6] & 0x7F) << 8 | bytes[7]);
    tag.valid_chk  = (bytes[6] & 0x80) != 0;
    tag.abs_page   = (uint32_t)(bytes[8] << 16 | bytes[9] << 8 | bytes[10]);
    tag.checksum   = bytes[11];
    tag.rel_page   = (uint16_t)(bytes[12] << 8 | bytes[13]);
    tag.next_block = (uint32_t)(bytes[14] << 16 | bytes[15] << 8 | bytes[16]);
    tag.prev_block = (uint32_t)(bytes[17] << 16 | bytes[18] << 8 | bytes[19]);

    return tag;
}

/**
 * @brief Convert a ::sony_tag to a ::profile_tag representation.
 *
 * Sony tags contain a reduced-width (11-bit) next/prev block. To preserve chain termination semantics, values of
 * 0x7FF are translated to 0xFFFFFF (24-bit "no next/prev" sentinel). Other values are copied verbatim into the wider
 * fields. Only fields common to both formats are populated; Profile-specific fields (abs_page, checksum, used_bytes,
 * valid_chk, reserved) remain zero-initialized.
 *
 * @param tag Source Sony tag.
 * @return Profile tag with mapped values.
 */
profile_tag sony_tag_to_profile(const sony_tag tag)
{
    profile_tag profile = {0};

    const uint16_t next_number = tag.next_block & SONY_BLOCK_NUMBER_MASK;
    const uint16_t prev_number = tag.prev_block & SONY_BLOCK_NUMBER_MASK;

    profile.file_id    = tag.file_id;
    profile.kind       = tag.kind;
    profile.reserved   = tag.reserved;
    profile.next_block = next_number == SONY_BLOCK_NUMBER_MASK ? 0xFFFFFFU : next_number;
    profile.prev_block = prev_number == SONY_BLOCK_NUMBER_MASK ? 0xFFFFFFU : prev_number;
    profile.rel_page   = tag.rel_page;
    profile.version    = tag.version;
    profile.volume     = tag.volume;

    return profile;
}

/**
 * @brief Convert a ::sony_tag to a ::priam_tag representation.
 *
 * Similar to sony_tag_to_profile() but returns a Priam tag. Priam-specific fields (abs_page, checksum, used_bytes,
 * valid_chk, disk_size, reserved) are zero-initialized. 0x7FF next/prev sentinels expand to 0xFFFFFF.
 *
 * @param tag Source Sony tag.
 * @return Priam tag with mapped values.
 */
priam_tag sony_tag_to_priam(const sony_tag tag)
{
    priam_tag priam = {0};

    const uint16_t next_number = tag.next_block & SONY_BLOCK_NUMBER_MASK;
    const uint16_t prev_number = tag.prev_block & SONY_BLOCK_NUMBER_MASK;

    priam.file_id    = tag.file_id;
    priam.kind       = tag.kind;
    priam.reserved   = tag.reserved;
    priam.next_block = next_number == SONY_BLOCK_NUMBER_MASK ? 0xFFFFFFU : next_number;
    priam.prev_block = prev_number == SONY_BLOCK_NUMBER_MASK ? 0xFFFFFFU : prev_number;
    priam.rel_page   = tag.rel_page;
    priam.version    = tag.version;
    priam.volume     = tag.volume;

    return priam;
}

/**
 * @brief Convert a ::profile_tag to a ::priam_tag.
 *
 * Copies all overlapping fields directly. The Priam-only disk_size field remains zero unless modified later.
 *
 * @param tag Source Profile tag.
 * @return Priam tag with copied values.
 */
priam_tag profile_tag_to_priam(const profile_tag tag)
{
    priam_tag priam = {0};

    priam.abs_page   = tag.abs_page;
    priam.checksum   = tag.checksum;
    priam.file_id    = tag.file_id;
    priam.kind       = tag.kind;
    priam.reserved   = tag.reserved;
    priam.next_block = tag.next_block;
    priam.prev_block = tag.prev_block;
    priam.rel_page   = tag.rel_page;
    priam.used_bytes = tag.used_bytes;
    priam.valid_chk  = tag.valid_chk;
    priam.version    = tag.version;
    priam.volume     = tag.volume;

    return priam;
}

/**
 * @brief Convert a ::profile_tag to a ::sony_tag.
 *
 * Narrows 24-bit next/prev block numbers to 11 bits. A value of 0xFFFFFF (end-of-chain sentinel) is mapped back to
 * 0x7FF. Any non-sentinel value is truncated to its lower 11 bits. Fields without Sony equivalents are dropped.
 *
 * @param tag Source Profile tag.
 * @return Sony tag with mapped/narrowed values.
 */
sony_tag profile_tag_to_sony(const profile_tag tag)
{
    sony_tag sony = {0};

    const uint32_t next_number = tag.next_block & 0xFFFFFFU;
    const uint32_t prev_number = tag.prev_block & 0xFFFFFFU;

    uint16_t sony_next = (uint16_t)(next_number & SONY_BLOCK_NUMBER_MASK);
    uint16_t sony_prev = (uint16_t)(prev_number & SONY_BLOCK_NUMBER_MASK);

    if(next_number == 0xFFFFFFU) sony_next = SONY_BLOCK_NUMBER_MASK;
    if(prev_number == 0xFFFFFFU) sony_prev = SONY_BLOCK_NUMBER_MASK;

    sony.file_id    = tag.file_id;
    sony.kind       = tag.kind;
    sony.reserved   = tag.reserved;
    sony.next_block = sony_next;
    sony.prev_block = sony_prev;
    sony.rel_page   = tag.rel_page;
    sony.version    = tag.version;
    sony.volume     = tag.volume;

    return sony;
}

/**
 * @brief Convert a ::priam_tag to a ::sony_tag.
 *
 * See profile_tag_to_sony(); Priam-only fields (abs_page, checksum, used_bytes, valid_chk, disk_size) are discarded.
 * 0xFFFFFF chain terminators become 0x7FF; other values are truncated to 11 bits.
 *
 * @param tag Source Priam tag.
 * @return Sony tag with mapped values.
 */
sony_tag priam_tag_to_sony(const priam_tag tag)
{
    sony_tag sony = {0};

    const uint32_t next_number = tag.next_block & 0xFFFFFFU;
    const uint32_t prev_number = tag.prev_block & 0xFFFFFFU;

    uint16_t sony_next = (uint16_t)(next_number & SONY_BLOCK_NUMBER_MASK);
    uint16_t sony_prev = (uint16_t)(prev_number & SONY_BLOCK_NUMBER_MASK);

    if(next_number == 0xFFFFFFU) sony_next = SONY_BLOCK_NUMBER_MASK;
    if(prev_number == 0xFFFFFFU) sony_prev = SONY_BLOCK_NUMBER_MASK;

    sony.file_id    = tag.file_id;
    sony.kind       = tag.kind;
    sony.reserved   = tag.reserved;
    sony.next_block = sony_next;
    sony.prev_block = sony_prev;
    sony.rel_page   = tag.rel_page;
    sony.version    = tag.version;
    sony.volume     = tag.volume;

    return sony;
}

/**
 * @brief Convert a ::priam_tag to a ::profile_tag.
 *
 * Copies overlapping fields, omitting the Priam-specific disk_size. Useful when reusing Priam metadata in a context
 * that expects Profile tag semantics.
 *
 * @param tag Source Priam tag.
 * @return Profile tag with copied values.
 */
profile_tag priam_tag_to_profile(const priam_tag tag)
{
    profile_tag profile = {0};

    profile.abs_page   = tag.abs_page;
    profile.checksum   = tag.checksum;
    profile.file_id    = tag.file_id;
    profile.kind       = tag.kind;
    profile.reserved   = tag.reserved;
    profile.next_block = tag.next_block;
    profile.prev_block = tag.prev_block;
    profile.rel_page   = tag.rel_page;
    profile.used_bytes = tag.used_bytes;
    profile.valid_chk  = tag.valid_chk;
    profile.version    = tag.version;
    profile.volume     = tag.volume;

    return profile;
}

/**
 * @brief Serialize a ::profile_tag into a newly allocated 20-byte big-endian on-disk representation.
 *
 * Bit/byte packing mirrors bytes_to_profile_tag(). The 24-bit next_block, prev_block and abs_page values are written
 * most-significant byte first. The used_bytes high 7 bits and valid_chk flag are packed into the first byte of the
 * 16-bit used_bytes field as per on-disk layout.
 *
 * @param tag Profile tag to serialize.
 * @return Pointer to a 20-byte buffer containing the serialized tag, or NULL on allocation failure.
 * @retval NULL Allocation failure (caller should check before use).
 * @note Caller must free() the returned buffer.
 */
uint8_t *profile_tag_to_bytes(const profile_tag tag)
{
    uint8_t *bytes = calloc(1, 20);
    if(!bytes) return NULL;

    const uint16_t version    = tag.version;
    const uint8_t  volume     = tag.volume;
    const int16_t  file_id    = tag.file_id;
    const uint16_t rel_page   = tag.rel_page;
    const uint32_t abs_page   = tag.abs_page & 0xFFFFFFU;
    const uint32_t next_block = tag.next_block & 0xFFFFFFU;
    const uint32_t prev_block = tag.prev_block & 0xFFFFFFU;
    const uint16_t used_bytes = (uint16_t)(tag.used_bytes & 0x7FFFU);
    const uint8_t  kind_bits  = (uint8_t)(tag.kind & 0x3U);
    const uint8_t  reserved   = (uint8_t)(tag.reserved & 0x3FU);

    bytes[0] = (uint8_t)((version >> 8) & 0xFF);
    bytes[1] = (uint8_t)(version & 0xFF);
    bytes[2] = (uint8_t)((kind_bits << 6) | reserved);
    bytes[3] = volume;
    bytes[4] = (uint8_t)((file_id >> 8) & 0xFF);
    bytes[5] = (uint8_t)(file_id & 0xFF);
    bytes[6] = (uint8_t)((used_bytes >> 8) & 0x7F);
    if(tag.valid_chk) bytes[6] |= 0x80U;
    bytes[7]  = (uint8_t)(used_bytes & 0xFF);
    bytes[8]  = (uint8_t)((abs_page >> 16) & 0xFF);
    bytes[9]  = (uint8_t)((abs_page >> 8) & 0xFF);
    bytes[10] = (uint8_t)(abs_page & 0xFF);
    bytes[11] = tag.checksum;
    bytes[12] = (uint8_t)((rel_page >> 8) & 0xFF);
    bytes[13] = (uint8_t)(rel_page & 0xFF);
    bytes[14] = (uint8_t)((next_block >> 16) & 0xFF);
    bytes[15] = (uint8_t)((next_block >> 8) & 0xFF);
    bytes[16] = (uint8_t)(next_block & 0xFF);
    bytes[17] = (uint8_t)((prev_block >> 16) & 0xFF);
    bytes[18] = (uint8_t)((prev_block >> 8) & 0xFF);
    bytes[19] = (uint8_t)(prev_block & 0xFF);

    return bytes;
}

/**
 * @brief Serialize a ::priam_tag into a newly allocated 24-byte big-endian on-disk representation.
 *
 * Layout matches bytes_to_priam_tag(). Includes the final 4-byte disk_size field. Packing rules for used_bytes and
 * valid_chk mirror those used in profile_tag_to_bytes().
 *
 * @param tag Priam tag to serialize.
 * @return Pointer to a 24-byte buffer, or NULL on allocation failure.
 * @retval NULL Allocation failure.
 * @note Caller must free() the returned buffer.
 */
uint8_t *priam_tag_to_bytes(const priam_tag tag)
{
    uint8_t *bytes = calloc(1, 24);
    if(!bytes) return NULL;

    const uint16_t version    = tag.version;
    const uint8_t  volume     = tag.volume;
    const int16_t  file_id    = tag.file_id;
    const uint16_t rel_page   = tag.rel_page;
    const uint32_t abs_page   = tag.abs_page & 0xFFFFFFU;
    const uint32_t next_block = tag.next_block & 0xFFFFFFU;
    const uint32_t prev_block = tag.prev_block & 0xFFFFFFU;
    const uint32_t disk_size  = tag.disk_size;
    const uint16_t used_bytes = (uint16_t)(tag.used_bytes & 0x7FFFU);
    const uint8_t  kind_bits  = (uint8_t)(tag.kind & 0x3U);
    const uint8_t  reserved   = (uint8_t)(tag.reserved & 0x3FU);

    bytes[0] = (uint8_t)((version >> 8) & 0xFF);
    bytes[1] = (uint8_t)(version & 0xFF);
    bytes[2] = (uint8_t)((kind_bits << 6) | reserved);
    bytes[3] = volume;
    bytes[4] = (uint8_t)((file_id >> 8) & 0xFF);
    bytes[5] = (uint8_t)(file_id & 0xFF);
    bytes[6] = (uint8_t)((used_bytes >> 8) & 0x7F);
    if(tag.valid_chk) bytes[6] |= 0x80U;
    bytes[7]  = (uint8_t)(used_bytes & 0xFF);
    bytes[8]  = (uint8_t)((abs_page >> 16) & 0xFF);
    bytes[9]  = (uint8_t)((abs_page >> 8) & 0xFF);
    bytes[10] = (uint8_t)(abs_page & 0xFF);
    bytes[11] = tag.checksum;
    bytes[12] = (uint8_t)((rel_page >> 8) & 0xFF);
    bytes[13] = (uint8_t)(rel_page & 0xFF);
    bytes[14] = (uint8_t)((next_block >> 16) & 0xFF);
    bytes[15] = (uint8_t)((next_block >> 8) & 0xFF);
    bytes[16] = (uint8_t)(next_block & 0xFF);
    bytes[17] = (uint8_t)((prev_block >> 16) & 0xFF);
    bytes[18] = (uint8_t)((prev_block >> 8) & 0xFF);
    bytes[19] = (uint8_t)(prev_block & 0xFF);
    bytes[20] = (uint8_t)((disk_size >> 24) & 0xFF);
    bytes[21] = (uint8_t)((disk_size >> 16) & 0xFF);
    bytes[22] = (uint8_t)((disk_size >> 8) & 0xFF);
    bytes[23] = (uint8_t)(disk_size & 0xFF);

    return bytes;
}

/**
 * @brief Serialize a ::sony_tag into a newly allocated 12-byte big-endian on-disk representation.
 *
 * The next_block and prev_block fields are masked to 11 bits (0x7FF) during packing. Caller should ensure sentinel
 * semantics are already encoded (i.e., use 0x7FF for end-of-chain) prior to calling.
 *
 * @param tag Sony tag to serialize.
 * @return Pointer to a 12-byte buffer, or NULL on allocation failure.
 * @retval NULL Allocation failure.
 * @note Caller must free() the returned buffer.
 */
uint8_t *sony_tag_to_bytes(sony_tag tag)
{
    uint8_t *bytes = calloc(1, 12);
    if(!bytes) return NULL;

    const uint16_t version    = tag.version;
    const uint8_t  volume     = tag.volume;
    const int16_t  file_id    = tag.file_id;
    const uint16_t rel_page   = tag.rel_page;
    const uint16_t next_block = tag.next_block & SONY_BLOCK_ALLOWED_MASK;
    const uint16_t prev_block = tag.prev_block & SONY_BLOCK_ALLOWED_MASK;
    const uint8_t  kind_bits  = (uint8_t)(tag.kind & 0x3U);
    const uint8_t  reserved   = (uint8_t)(tag.reserved & 0x3FU);

    bytes[0]  = (uint8_t)((version >> 8) & 0xFF);
    bytes[1]  = (uint8_t)(version & 0xFF);
    bytes[2]  = (uint8_t)((kind_bits << 6) | reserved);
    bytes[3]  = volume;
    bytes[4]  = (uint8_t)((file_id >> 8) & 0xFF);
    bytes[5]  = (uint8_t)(file_id & 0xFF);
    bytes[6]  = (uint8_t)((rel_page >> 8) & 0xFF);
    bytes[7]  = (uint8_t)(rel_page & 0xFF);
    bytes[8]  = (uint8_t)((next_block >> 8) & 0xFF);
    bytes[9]  = (uint8_t)(next_block & 0xFF);
    bytes[10] = (uint8_t)((prev_block >> 8) & 0xFF);
    bytes[11] = (uint8_t)(prev_block & 0xFF);

    return bytes;
}