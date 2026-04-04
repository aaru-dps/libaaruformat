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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <aaru.h>
#include <aaruformat.h>

#include "aaruformattool.h"
#include "uthash.h"

// Hash table structure for index entries (keyed by offset)
typedef struct
{
    uint64_t       offset;     // key - file offset of the block
    uint32_t       blockType;  // block type identifier
    uint16_t       dataType;   // data type identifier
    UT_hash_handle hh;         // makes this structure hashable
} IndexEntryHash;

// Structure to map MediaTagType enum names to values
typedef struct
{
    const char  *name;
    MediaTagType value;
} MediaTagTypeMapping;

// clang-format off
static const MediaTagTypeMapping media_tag_mappings[] = {
    {"kMediaTagCdToc",                        kMediaTagCdToc},
    {"kMediaTagSessionInfo",                kMediaTagSessionInfo},
    {"kMediaTagFullToc",                    kMediaTagFullToc},
    {"kMediaTagCdPma",                        kMediaTagCdPma},
    {"kMediaTagCdAtip",                       kMediaTagCdAtip},
    {"kMediaTagCdText",                       kMediaTagCdText},
    {"kMediaTagCdMcn",                        kMediaTagCdMcn},
    {"kMediaTagDvdPfi",                       kMediaTagDvdPfi},
    {"kMediaTagDvdCmi",                       kMediaTagDvdCmi},
    {"kMediaTagDvdDiscKey",                   kMediaTagDvdDiscKey},
    {"kMediaTagDvdBca",                       kMediaTagDvdBca},
    {"kMediaTagDvdDmi",                       kMediaTagDvdDmi},
    {"kMediaTagDvdMediaIdentifier",           kMediaTagDvdMediaIdentifier},
    {"kMediaTagDvdMkb",                       kMediaTagDvdMkb},
    {"kMediaTagDvdRamDds",                    kMediaTagDvdRamDds},
    {"kMediaTagDvdRamMediumStatus",           kMediaTagDvdRamMediumStatus},
    {"kMediaTagDvdRamSpareArea",              kMediaTagDvdRamSpareArea},
    {"kMediaTagDvdrRmd",                      kMediaTagDvdrRmd},
    {"kMediaTagDvdrPreRecordedInfo",          kMediaTagDvdrPreRecordedInfo},
    {"kMediaTagDvdrMediaIdentifier",          kMediaTagDvdrMediaIdentifier},
    {"kMediaTagDvdrPfi",                      kMediaTagDvdrPfi},
    {"kMediaTagDvdAdip",                      kMediaTagDvdAdip},
    {"kMediaTagHddvdCpi",                     kMediaTagHddvdCpi},
    {"kMediaTagHddvdMediumStatus",            kMediaTagHddvdMediumStatus},
    {"kMediaTagDvddlLayerCapacity",           kMediaTagDvddlLayerCapacity},
    {"kMediaTagDvddlMiddleZoneAddress",       kMediaTagDvddlMiddleZoneAddress},
    {"kMediaTagDvddlJumpIntervalSize",        kMediaTagDvddlJumpIntervalSize},
    {"kMediaTagDvddlManualLayerJumpLba",      kMediaTagDvddlManualLayerJumpLba},
    {"kMediaTagBlurayDi",                         kMediaTagBlurayDi},
    {"kMediaTagBlurayBca",                        kMediaTagBlurayBca},
    {"kMediaTagBlurayDds",                        kMediaTagBlurayDds},
    {"kMediaTagBlurayCartridgeStatus",            kMediaTagBlurayCartridgeStatus},
    {"kMediaTagBluraySpareArea",                  kMediaTagBluraySpareArea},
    {"kMediaTagAacsVolumeIdentifier",         kMediaTagAacsVolumeIdentifier},
    {"kMediaTagAacsSerialNumber",             kMediaTagAacsSerialNumber},
    {"kMediaTagAacsMediaIdentifier",          kMediaTagAacsMediaIdentifier},
    {"kMediaTagAacsMkb",                      kMediaTagAacsMkb},
    {"kMediaTagAacsDataKeys",                 kMediaTagAacsDataKeys},
    {"kMediaTagAacsLbaExtents",               kMediaTagAacsLbaExtents},
    {"kMediaTagCprmMkb",                 kMediaTagCprmMkb},
    {"kMediaTagHybridRecognizedLayers",       kMediaTagHybridRecognizedLayers},
    {"kMediaTagMmcWriteProtection",           kMediaTagMmcWriteProtection},
    {"kMediaTagMmcDiscInformation",           kMediaTagMmcDiscInformation},
    {"kMediaTagMmcTrackResourcesInformation", kMediaTagMmcTrackResourcesInformation},
    {"kMediaTagMmcPowResourcesInformation",   kMediaTagMmcPowResourcesInformation},
    {"kMediaTagScsiInquiry",                  kMediaTagScsiInquiry},
    {"kMediaTagScsiModePage2A",              kMediaTagScsiModePage2A},
    {"kMediaTagAtaIdentify",                  kMediaTagAtaIdentify},
    {"kMediaTagAtapiIdentify",                kMediaTagAtapiIdentify},
    {"kMediaTagPcmciaCis",                    kMediaTagPcmciaCis},
    {"kMediaTagSdCid",                        kMediaTagSdCid},
    {"kMediaTagSdCsd",                        kMediaTagSdCsd},
    {"kMediaTagSdScr",                        kMediaTagSdScr},
    {"kMediaTagSdOcr",                        kMediaTagSdOcr},
    {"kMediaTagMmcCid",                       kMediaTagMmcCid},
    {"kMediaTagMmcCsd",                       kMediaTagMmcCsd},
    {"kMediaTagMmcOcr",                       kMediaTagMmcOcr},
    {"kMediaTagExtendedCsd",               kMediaTagExtendedCsd},
    {"kMediaTagXboxSecuritySector",           kMediaTagXboxSecuritySector},
    {"kMediaTagFloppyLeadOut",                kMediaTagFloppyLeadOut},
    {"kMediaTagDiscControlBlock",              kMediaTagDiscControlBlock},
    {"kMediaTagCdFirstTrackPregap",           kMediaTagCdFirstTrackPregap},
    {"kMediaTagCdLeadOut",                    kMediaTagCdLeadOut},
    {"kMediaTagScsiModeSense6",              kMediaTagScsiModeSense6},
    {"kMediaTagScsiModeSense10",             kMediaTagScsiModeSense10},
    {"kMediaTagUsbDescriptors",               kMediaTagUsbDescriptors},
    {"kMediaTagXboxDmi",                      kMediaTagXboxDmi},
    {"kMediaTagXboxPfi",                      kMediaTagXboxPfi},
    {"kMediaTagCdLeadIn",                     kMediaTagCdLeadIn},
    {"kMediaTagMiniDiscType",                  kMediaTagMiniDiscType},
    {"kMediaTagMiniDiscD5",                    kMediaTagMiniDiscD5},
    {"kMediaTagMiniDiscUtoc",                  kMediaTagMiniDiscUtoc},
    {"kMediaTagMiniDiscDtoc",                  kMediaTagMiniDiscDtoc},
    {"kMediaTagDvdDiscKeyDecrypted",         kMediaTagDvdDiscKeyDecrypted},
    {"kMediaTagDvdPfi2ndLayer",              kMediaTagDvdPfi2ndLayer},
    {"kMediaTagFloppyWriteProtect",           kMediaTagFloppyWriteProtect},
};
// clang-format on

static const size_t num_media_tag_mappings = sizeof(media_tag_mappings) / sizeof(media_tag_mappings[0]);

/**
 * @brief Convert MediaTagType to DataType
 */
static int32_t get_datatype_for_media_tag_type(MediaTagType tag_type)
{
    switch(tag_type)
    {
        case kMediaTagCdToc:
            return kDataTypeCdToc;
        case kMediaTagSessionInfo:
            return kDataTypeSessionInfo;
        case kMediaTagFullToc:
            return kDataTypeFullToc;
        case kMediaTagCdPma:
            return kDataTypeCdPma;
        case kMediaTagCdAtip:
            return kDataTypeCdAtip;
        case kMediaTagCdText:
            return kDataTypeCdText;
        case kMediaTagDvdPfi:
            return kDataTypeDvdPfi;
        case kMediaTagDvdPfi2ndLayer:
            return kDataTypeDvdPfi2ndLayer;
        case kMediaTagDvdCmi:
            return kDataTypeDvdCmi;
        case kMediaTagDvdDiscKey:
            return kDataTypeDvdDiscKey;
        case kMediaTagDvdBca:
            return kDataTypeDvdBca;
        case kMediaTagDvdDmi:
            return kDataTypeDvdDmi;
        case kMediaTagDvdMediaIdentifier:
            return kDataTypeDvdMediaIdentifier;
        case kMediaTagDvdMkb:
            return kDataTypeDvdMkb;
        case kMediaTagDvdRamDds:
            return kDataTypeDvdRamDds;
        case kMediaTagDvdRamMediumStatus:
            return kDataTypeDvdRamMediumStatus;
        case kMediaTagDvdRamSpareArea:
            return kDataTypeDvdRamSpareArea;
        case kMediaTagDvdrRmd:
            return kDataTypeDvdrRmd;
        case kMediaTagDvdrPreRecordedInfo:
            return kDataTypeDvdrPreRecordedInfo;
        case kMediaTagDvdrMediaIdentifier:
            return kDataTypeDvdrMediaIdentifier;
        case kMediaTagDvdrPfi:
            return kDataTypeDvdrPfi;
        case kMediaTagDvdAdip:
            return kDataTypeDvdAdip;
        case kMediaTagHddvdCpi:
            return kDataTypeHddvdCpi;
        case kMediaTagHddvdMediumStatus:
            return kDataTypeHddvdMediumStatus;
        case kMediaTagDvddlLayerCapacity:
            return kDataTypeDvddlLayerCapacity;
        case kMediaTagDvddlMiddleZoneAddress:
            return kDataTypeDvddlMiddleZoneAddress;
        case kMediaTagDvddlJumpIntervalSize:
            return kDataTypeDvddlJumpIntervalSize;
        case kMediaTagDvddlManualLayerJumpLba:
            return kDataTypeDvddlManualLayerJumpLba;
        case kMediaTagBlurayDi:
            return kDataTypeBlurayDi;
        case kMediaTagBlurayBca:
            return kDataTypeBlurayBca;
        case kMediaTagBlurayDds:
            return kDataTypeBlurayDds;
        case kMediaTagBlurayCartridgeStatus:
            return kDataTypeBlurayCartridgeStatus;
        case kMediaTagBluraySpareArea:
            return kDataTypeBluraySpareArea;
        case kMediaTagAacsVolumeIdentifier:
            return kDataTypeAacsVolumeIdentifier;
        case kMediaTagAacsSerialNumber:
            return kDataTypeAacsSerialNumber;
        case kMediaTagAacsMediaIdentifier:
            return kDataTypeAacsMediaIdentifier;
        case kMediaTagAacsMkb:
            return kDataTypeAacsMkb;
        case kMediaTagAacsDataKeys:
            return kDataTypeAacsDataKeys;
        case kMediaTagAacsLbaExtents:
            return kDataTypeAacsLbaExtents;
        case kMediaTagCprmMkb:
            return kDataTypeCprmMkb;
        case kMediaTagHybridRecognizedLayers:
            return kDataTypeHybridRecognizedLayers;
        case kMediaTagMmcWriteProtection:
            return kDataTypeMmcWriteProtection;
        case kMediaTagMmcDiscInformation:
            return kDataTypeMmcDiscInformation;
        case kMediaTagMmcTrackResourcesInformation:
            return kDataTypeMmcTrackResourcesInformation;
        case kMediaTagMmcPowResourcesInformation:
            return kDataTypeMmcPowResourcesInformation;
        case kMediaTagScsiInquiry:
            return kDataTypeScsiInquiry;
        case kMediaTagScsiModePage2A:
            return kDataTypeScsiModePage2A;
        case kMediaTagAtaIdentify:
            return kDataTypeAtaIdentify;
        case kMediaTagAtapiIdentify:
            return kDataTypeAtapiIdentify;
        case kMediaTagPcmciaCis:
            return kDataTypePcmciaCis;
        case kMediaTagSdCid:
            return kDataTypeSdCid;
        case kMediaTagSdCsd:
            return kDataTypeSdCsd;
        case kMediaTagSdScr:
            return kDataTypeSdScr;
        case kMediaTagSdOcr:
            return kDataTypeSdOcr;
        case kMediaTagMmcCid:
            return kDataTypeMmcCid;
        case kMediaTagMmcCsd:
            return kDataTypeMmcCsd;
        case kMediaTagMmcOcr:
            return kDataTypeMmcOcr;
        case kMediaTagExtendedCsd:
            return kDataTypeExtendedCsd;
        case kMediaTagXboxSecuritySector:
            return kDataTypeXboxSecuritySector;
        case kMediaTagFloppyLeadOut:
            return kDataTypeFloppyLeadOut;
        case kMediaTagDiscControlBlock:
            return kDataTypeDiscControlBlock;
        case kMediaTagCdFirstTrackPregap:
            return kDataTypeCdFirstTrackPregap;
        case kMediaTagCdLeadOut:
            return kDataTypeCdLeadOut;
        case kMediaTagScsiModeSense6:
            return kDataTypeScsiModeSense6;
        case kMediaTagScsiModeSense10:
            return kDataTypeScsiModeSense10;
        case kMediaTagUsbDescriptors:
            return kDataTypeUsbDescriptors;
        case kMediaTagXboxDmi:
            return kDataTypeXboxDmi;
        case kMediaTagXboxPfi:
            return kDataTypeXboxPfi;
        case kMediaTagCdMcn:
            return kDataTypeCdMcn;
        case kMediaTagCdLeadIn:
            return kDataTypeCdLeadIn;
        case kMediaTagDvdDiscKeyDecrypted:
            return kDataTypeDvdDiscKeyDecrypted;
        case kMediaTagFloppyWriteProtect:
            return kDataTypeFloppyWriteProtect;
        default:
            return -1;
    }
}

/**
 * @brief Get current time as Windows FILETIME (100ns intervals since 1601-01-01)
 */
static uint64_t get_current_filetime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Convert Unix epoch (1970) to Windows FILETIME epoch (1601)
    // Difference is 11644473600 seconds
    uint64_t filetime = ((uint64_t)tv.tv_sec + 11644473600ULL) * 10000000ULL;
    filetime += (uint64_t)tv.tv_usec * 10ULL;

    return filetime;
}

/**
 * @brief Parse a tag type string into a MediaTagType enum value.
 *
 * Accepts either:
 * - An integer value (e.g., "8" for DVD_CMI)
 * - A string name (e.g., "DVD_CMI") - case-insensitive comparison
 *
 * @param tag_type_str The string to parse
 * @param out_tag_type Pointer to store the parsed MediaTagType value
 * @return true if parsing succeeded, false otherwise
 */
static bool parse_media_tag_type(const char *tag_type_str, MediaTagType *out_tag_type)
{
    if(tag_type_str == NULL || out_tag_type == NULL) return false;

    // First, try to parse as an integer
    char *endptr;
    errno        = 0;
    long int_val = strtol(tag_type_str, &endptr, 10);

    // Check if the entire string was consumed (it's a valid integer)
    if(*endptr == '\0' && errno == 0)
    {
        // Validate the integer is within valid MediaTagType range
        if(int_val >= 0 && int_val <= MaxMediaTag)
        {
            *out_tag_type = (MediaTagType)int_val;
            return true;
        }
        // Integer out of range
        return false;
    }

    // Not a valid integer, try to match as a string name (case-insensitive)
    for(size_t i = 0; i < num_media_tag_mappings; i++)
    {
        if(strcasecmp(tag_type_str, media_tag_mappings[i].name) == 0)
        {
            *out_tag_type = media_tag_mappings[i].value;
            return true;
        }
    }

    return false;
}

/**
 * @brief Print the list of valid media tag types.
 */
static void print_valid_media_tag_types(void)
{
    printf("\nValid media tag types:\n");
    for(size_t i = 0; i < num_media_tag_mappings; i++)
    {
        printf("  %3d  %s\n", media_tag_mappings[i].value, media_tag_mappings[i].name);
    }
}

/**
 * @brief Free the index hash table
 */
static void free_index_hash(IndexEntryHash **index_hash)
{
    IndexEntryHash *current, *tmp;
    HASH_ITER(hh, *index_hash, current, tmp)
    {
        HASH_DEL(*index_hash, current);
        free(current);
    }
}

int inject_media_tag(const char *tag_type_str, const char *media_tag_file, const char *image_file)
{
    MediaTagType tag_type;
    int          result = -1;

    // Parse and validate the tag type
    if(!parse_media_tag_type(tag_type_str, &tag_type))
    {
        fprintf(stderr, "ERROR: Invalid media tag type: '%s'\n", tag_type_str);
        print_valid_media_tag_types();
        return -1;
    }

    // Convert MediaTagType to DataType
    int32_t data_type = get_datatype_for_media_tag_type(tag_type);
    if(data_type < 0)
    {
        fprintf(stderr, "ERROR: Cannot convert media tag type '%s' to data type.\n", tag_type_str);
        return -1;
    }

    // Read the media tag file into memory
    FILE *tag_file = fopen(media_tag_file, "rb");
    if(tag_file == NULL)
    {
        fprintf(stderr, "ERROR: Cannot open media tag file: '%s'\n", media_tag_file);
        fprintf(stderr, "       %s\n", strerror(errno));
        return -1;
    }

    // Get file size
    aaruf_fseek(tag_file, 0, SEEK_END);
    aaru_off_t tag_file_size = aaruf_ftell(tag_file);
    aaruf_fseek(tag_file, 0, SEEK_SET);

    if(tag_file_size <= 0)
    {
        fprintf(stderr, "ERROR: Media tag file is empty or cannot determine size: '%s'\n", media_tag_file);
        fclose(tag_file);
        return -1;
    }

    // Allocate and read tag data
    uint8_t *tag_data = (uint8_t *)malloc((size_t)tag_file_size);
    if(tag_data == NULL)
    {
        fprintf(stderr, "ERROR: Cannot allocate memory for media tag data (%" PRId64 " bytes)\n", tag_file_size);
        fclose(tag_file);
        return -1;
    }

    if(fread(tag_data, 1, (size_t)tag_file_size, tag_file) != (size_t)tag_file_size)
    {
        fprintf(stderr, "ERROR: Cannot read media tag file: '%s'\n", media_tag_file);
        free(tag_data);
        fclose(tag_file);
        return -1;
    }
    fclose(tag_file);

    // Open image file for reading first to validate
    FILE *image_stream = fopen(image_file, "rb");
    if(image_stream == NULL)
    {
        fprintf(stderr, "ERROR: Cannot open image file: '%s'\n", image_file);
        fprintf(stderr, "       %s\n", strerror(errno));
        free(tag_data);
        return -1;
    }

    // Read the header to validate it's an AaruFormat file
    AaruHeader header;
    if(fread(&header, 1, sizeof(AaruHeader), image_stream) != sizeof(AaruHeader))
    {
        fprintf(stderr, "ERROR: Cannot read image header from: '%s'\n", image_file);
        fprintf(stderr, "       File may be too small or corrupted.\n");
        fclose(image_stream);
        free(tag_data);
        return -1;
    }
    fclose(image_stream);

    // Check the magic identifier
    if(header.identifier != AARU_MAGIC && header.identifier != DIC_MAGIC)
    {
        fprintf(stderr, "ERROR: '%s' is not a valid AaruFormat image.\n", image_file);
        free(tag_data);
        return -1;
    }

    // Check the image version (must be >= 2)
    if(header.imageMajorVersion < AARUF_VERSION_V2)
    {
        fprintf(stderr, "ERROR: Image version %d.%d is not supported.\n", header.imageMajorVersion,
                header.imageMinorVersion);
        fprintf(stderr, "       Only AaruFormat version 2 or higher images are supported.\n");
        fprintf(stderr, "       Please upgrade the image using the 'convert' command first.\n");
        free(tag_data);
        return -1;
    }

    // Re-read the full v2 header
    image_stream = fopen(image_file, "rb");
    if(image_stream == NULL)
    {
        fprintf(stderr, "ERROR: Cannot reopen image file: '%s'\n", image_file);
        fprintf(stderr, "       %s\n", strerror(errno));
        free(tag_data);
        return -1;
    }

    AaruHeaderV2 header_v2;
    if(fread(&header_v2, 1, sizeof(AaruHeaderV2), image_stream) != sizeof(AaruHeaderV2))
    {
        fprintf(stderr, "ERROR: Cannot read v2 header from: '%s'\n", image_file);
        fclose(image_stream);
        free(tag_data);
        return -1;
    }

    // Seek to index position
    if(aaruf_fseek(image_stream, (aaru_off_t)header_v2.indexOffset, SEEK_SET) != 0)
    {
        fprintf(stderr, "ERROR: Cannot seek to index offset %llu: %s\n", (unsigned long long)header_v2.indexOffset,
                strerror(errno));
        fclose(image_stream);
        free(tag_data);
        return -1;
    }

    // Read index header (v3)
    IndexHeader3 index_header;
    if(fread(&index_header, 1, sizeof(IndexHeader3), image_stream) != sizeof(IndexHeader3))
    {
        fprintf(stderr, "ERROR: Cannot read index header from: '%s'\n", image_file);
        fclose(image_stream);
        free(tag_data);
        return -1;
    }

    // Verify it's an IndexBlock3
    if(index_header.identifier != IndexBlock3)
    {
        fprintf(stderr, "ERROR: Expected IndexBlock3 identifier, found 0x%08X\n", index_header.identifier);
        fclose(image_stream);
        free(tag_data);
        return -1;
    }

    // Allocate memory for index entries
    IndexEntry *entries = (IndexEntry *)malloc(sizeof(IndexEntry) * index_header.entries);
    if(entries == NULL)
    {
        fprintf(stderr, "ERROR: Cannot allocate memory for %llu index entries\n",
                (unsigned long long)index_header.entries);
        fclose(image_stream);
        free(tag_data);
        return -1;
    }

    // Read all index entries
    size_t read_count = fread(entries, sizeof(IndexEntry), index_header.entries, image_stream);
    if(read_count != index_header.entries)
    {
        fprintf(stderr, "ERROR: Cannot read index entries (read %zu, expected %llu)\n", read_count,
                (unsigned long long)index_header.entries);
        free(entries);
        fclose(image_stream);
        free(tag_data);
        return -1;
    }

    fclose(image_stream);

    // Build hash table from index entries
    IndexEntryHash *index_hash = NULL;

    for(uint64_t i = 0; i < index_header.entries; i++)
    {
        IndexEntryHash *entry = (IndexEntryHash *)malloc(sizeof(IndexEntryHash));
        if(entry == NULL)
        {
            fprintf(stderr, "ERROR: Cannot allocate memory for hash table entry %llu\n", (unsigned long long)i);
            free_index_hash(&index_hash);
            free(entries);
            free(tag_data);
            return -1;
        }

        entry->offset    = entries[i].offset;
        entry->blockType = entries[i].blockType;
        entry->dataType  = entries[i].dataType;

        HASH_ADD(hh, index_hash, offset, sizeof(uint64_t), entry);
    }

    // Find the DDT2 entry with dataType == UserData
    IndexEntryHash *userdata_ddt = NULL;
    IndexEntryHash *current, *tmp;

    HASH_ITER(hh, index_hash, current, tmp)
    {
        if(current->blockType == DeDuplicationTable2 && current->dataType == kDataTypeUserData)
        {
            userdata_ddt = current;
            break;
        }
    }

    if(userdata_ddt == NULL)
    {
        fprintf(stderr, "ERROR: No UserData DDT2 block found in index.\n");
        fprintf(stderr, "       This image appears to be invalid or corrupted.\n");
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    // Read the DDT2 header to get blockAlignmentShift if needed
    image_stream = fopen(image_file, "rb");
    if(image_stream == NULL)
    {
        fprintf(stderr, "ERROR: Cannot reopen image file to read DDT2 header: '%s'\n", image_file);
        fprintf(stderr, "       %s\n", strerror(errno));
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    if(aaruf_fseek(image_stream, (aaru_off_t)userdata_ddt->offset, SEEK_SET) != 0)
    {
        fprintf(stderr, "ERROR: Cannot seek to DDT2 offset %llu: %s\n", (unsigned long long)userdata_ddt->offset,
                strerror(errno));
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    DdtHeader2 ddt_header;
    if(fread(&ddt_header, 1, sizeof(DdtHeader2), image_stream) != sizeof(DdtHeader2))
    {
        fprintf(stderr, "ERROR: Cannot read DDT2 header from offset %llu\n", (unsigned long long)userdata_ddt->offset);
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    fclose(image_stream);

    // Verify it's a DDT2 block
    if(ddt_header.identifier != DeDuplicationTable2)
    {
        fprintf(stderr, "ERROR: Expected DeDuplicationTable2 identifier at offset %llu, found 0x%08X\n",
                (unsigned long long)userdata_ddt->offset, ddt_header.identifier);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    // Check if header needs blockAlignmentShift update
    uint8_t block_alignment_shift = header_v2.blockAlignmentShift;
    if(block_alignment_shift == 0)
    {
        block_alignment_shift = ddt_header.blockAlignmentShift;
        printf("  Note: Header blockAlignmentShift is 0, using value %u from DDT2 header.\n", block_alignment_shift);
    }

    printf("Injecting media tag into image...\n");
    printf("  tag-type: %s (%d) -> DataType %d\n", media_tag_type_to_string(tag_type), tag_type, data_type);
    printf("  media-tag-file: %s (%" PRId64 " bytes)\n", media_tag_file, tag_file_size);
    printf("  image-file: %s\n", image_file);
    printf("  index entries: %llu\n", (unsigned long long)index_header.entries);
    printf("  blockAlignmentShift: %u\n", block_alignment_shift);

    // Remove any existing index entry with the same data type
    uint64_t new_entry_count  = 0;
    bool     removed_existing = false;
    uint64_t original_count   = index_header.entries;

    for(uint64_t i = 0; i < original_count; i++)
    {
        if(entries[i].blockType == DataBlock && entries[i].dataType == (uint16_t)data_type)
        {
            printf("  Removing existing DataBlock with dataType %d at offset %llu\n", data_type,
                   (unsigned long long)entries[i].offset);
            removed_existing = true;
            // Skip this entry (don't copy it)
        }
        else
        {
            // Keep this entry
            if(new_entry_count != i) { entries[new_entry_count] = entries[i]; }
            new_entry_count++;
        }
    }

    if(removed_existing) { printf("  Removed existing media tag entry from index.\n"); }

    // Open the image file for read+write
    image_stream = fopen(image_file, "r+b");
    if(image_stream == NULL)
    {
        fprintf(stderr, "ERROR: Cannot open image file for writing: '%s'\n", image_file);
        fprintf(stderr, "       %s\n", strerror(errno));
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    // Seek to end of file to find where to write the new data block
    aaruf_fseek(image_stream, 0, SEEK_END);
    aaru_off_t current_position = aaruf_ftell(image_stream);

    // Align the position
    uint64_t alignment_mask = (1ULL << block_alignment_shift) - 1;
    if((uint64_t)current_position & alignment_mask)
    {
        uint64_t aligned_position = ((uint64_t)current_position + alignment_mask) & ~alignment_mask;
        aaruf_fseek(image_stream, (aaru_off_t)aligned_position, SEEK_SET);
        current_position = (aaru_off_t)aligned_position;
    }

    uint64_t data_block_position = (uint64_t)current_position;
    printf("  Writing data block at offset %llu\n", (unsigned long long)data_block_position);

    // Create the data block header
    BlockHeader block_header = {0};
    block_header.identifier  = DataBlock;
    block_header.type        = (uint16_t)data_type;
    block_header.compression = kCompressionNone;  // No compression for simplicity
    block_header.sectorSize  = 1;                 // Media tags don't have sectors, use 1
    block_header.length      = (uint32_t)tag_file_size;
    block_header.cmpLength   = (uint32_t)tag_file_size;
    block_header.crc64       = aaruf_crc64_data(tag_data, (uint32_t)tag_file_size);
    block_header.cmpCrc64    = block_header.crc64;

    // Write the block header
    if(fwrite(&block_header, sizeof(BlockHeader), 1, image_stream) != 1)
    {
        fprintf(stderr, "ERROR: Cannot write block header.\n");
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    // Write the tag data
    if(fwrite(tag_data, 1, (size_t)tag_file_size, image_stream) != (size_t)tag_file_size)
    {
        fprintf(stderr, "ERROR: Cannot write tag data.\n");
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    printf("  Written data block (%zu + %" PRId64 " bytes)\n", sizeof(BlockHeader), tag_file_size);

    // Add new index entry
    entries = realloc(entries, sizeof(IndexEntry) * (new_entry_count + 1));
    if(entries == NULL)
    {
        fprintf(stderr, "ERROR: Cannot reallocate memory for new index entry.\n");
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(tag_data);
        return -1;
    }

    entries[new_entry_count].blockType = DataBlock;
    entries[new_entry_count].dataType  = (uint16_t)data_type;
    entries[new_entry_count].offset    = data_block_position;
    new_entry_count++;

    printf("  Added new index entry: blockType=DataBlock, dataType=%d, offset=%llu\n", data_type,
           (unsigned long long)data_block_position);

    // Align position for index
    current_position = aaruf_ftell(image_stream);
    if((uint64_t)current_position & alignment_mask)
    {
        uint64_t aligned_position = ((uint64_t)current_position + alignment_mask) & ~alignment_mask;
        aaruf_fseek(image_stream, (aaru_off_t)aligned_position, SEEK_SET);
        current_position = (aaru_off_t)aligned_position;
    }

    uint64_t new_index_position = (uint64_t)current_position;
    printf("  Writing new index at offset %llu with %llu entries\n", (unsigned long long)new_index_position,
           (unsigned long long)new_entry_count);

    // Prepare new index header
    IndexHeader3 new_index_header;
    new_index_header.identifier = IndexBlock3;
    new_index_header.entries    = new_entry_count;
    new_index_header.previous   = 0;

    // Calculate CRC64 of index entries
    new_index_header.crc64 =
        aaruf_crc64_data((const uint8_t *)entries, (uint32_t)(new_entry_count * sizeof(IndexEntry)));

    // Write index header
    if(fwrite(&new_index_header, sizeof(IndexHeader3), 1, image_stream) != 1)
    {
        fprintf(stderr, "ERROR: Cannot write index header.\n");
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    // Write index entries
    if(fwrite(entries, sizeof(IndexEntry), new_entry_count, image_stream) != new_entry_count)
    {
        fprintf(stderr, "ERROR: Cannot write index entries.\n");
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    printf("  Written index header and %llu entries\n", (unsigned long long)new_entry_count);

    // Update the header with new index offset and timestamps
    header_v2.indexOffset         = new_index_position;
    header_v2.lastWrittenTime     = (int64_t)get_current_filetime();
    header_v2.blockAlignmentShift = block_alignment_shift;

    // Seek back to beginning and rewrite header
    fseek(image_stream, 0, SEEK_SET);
    if(fwrite(&header_v2, sizeof(AaruHeaderV2), 1, image_stream) != 1)
    {
        fprintf(stderr, "ERROR: Cannot write updated header.\n");
        fclose(image_stream);
        free_index_hash(&index_hash);
        free(entries);
        free(tag_data);
        return -1;
    }

    printf("  Updated header with new index offset %llu\n", (unsigned long long)new_index_position);

    fclose(image_stream);

    printf("\nSuccessfully injected media tag '%s' into image.\n", media_tag_type_to_string(tag_type));
    result = 0;

    // Clean up
    free_index_hash(&index_hash);
    free(entries);
    free(tag_data);

    return result;
}
