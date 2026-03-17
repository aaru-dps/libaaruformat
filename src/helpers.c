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

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <wincrypt.h>
#endif

#include <aaru.h>

#include <aaruformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * @brief Converts an image data type to an Aaru media tag type.
 *
 * Maps the given image data type to the corresponding Aaru media tag type.
 *
 * @param type Image data type identifier.
 * @return Corresponding Aaru media tag type, or -1 if not found.
 */
AARU_LOCAL int32_t AARU_CALL aaruf_get_media_tag_type_for_datatype(const int32_t type)
{
    switch(type)
    {
        case kDataTypeCdToc:
            return kMediaTagCdToc;
        case kDataTypeSessionInfo:
            return kMediaTagSessionInfo;
        case kDataTypeFullToc:
            return kMediaTagFullToc;
        case kDataTypeCdPma:
            return kMediaTagCdPma;
        case kDataTypeCdAtip:
            return kMediaTagCdAtip;
        case kDataTypeCdText:
            return kMediaTagCdText;
        case kDataTypeDvdPfi:
            return kMediaTagDvdPfi;
        case kDataTypeDvdPfi2ndLayer:
            return kMediaTagDvdPfi2ndLayer;
        case kDataTypeDvdCmi:
            return kMediaTagDvdCmi;
        case kDataTypeDvdDiscKey:
            return kMediaTagDvdDiscKey;
        case kDataTypeDvdBca:
            return kMediaTagDvdBca;
        case kDataTypeDvdDmi:
            return kMediaTagDvdDmi;
        case kDataTypeDvdMediaIdentifier:
            return kMediaTagDvdMediaIdentifier;
        case kDataTypeDvdMkb:
            return kMediaTagDvdMkb;
        case kDataTypeDvdRamDds:
            return kMediaTagDvdRamDds;
        case kDataTypeDvdRamMediumStatus:
            return kMediaTagDvdRamMediumStatus;
        case kDataTypeDvdRamSpareArea:
            return kMediaTagDvdRamSpareArea;
        case kDataTypeDvdrRmd:
            return kMediaTagDvdrRmd;
        case kDataTypeDvdrPreRecordedInfo:
            return kMediaTagDvdrPreRecordedInfo;
        case kDataTypeDvdrMediaIdentifier:
            return kMediaTagDvdrMediaIdentifier;
        case kDataTypeDvdrPfi:
            return kMediaTagDvdrPfi;
        case kDataTypeDvdAdip:
            return kMediaTagDvdAdip;
        case kDataTypeHddvdCpi:
            return kMediaTagHddvdCpi;
        case kDataTypeHddvdMediumStatus:
            return kMediaTagHddvdMediumStatus;
        case kDataTypeDvddlLayerCapacity:
            return kMediaTagDvddlLayerCapacity;
        case kDataTypeDvddlMiddleZoneAddress:
            return kMediaTagDvddlMiddleZoneAddress;
        case kDataTypeDvddlJumpIntervalSize:
            return kMediaTagDvddlJumpIntervalSize;
        case kDataTypeDvddlManualLayerJumpLba:
            return kMediaTagDvddlManualLayerJumpLba;
        case kDataTypeBlurayDi:
            return kMediaTagBlurayDi;
        case kDataTypeBlurayBca:
            return kMediaTagBlurayBca;
        case kDataTypeBlurayDds:
            return kMediaTagBlurayDds;
        case kDataTypeBlurayCartridgeStatus:
            return kMediaTagBlurayCartridgeStatus;
        case kDataTypeBluraySpareArea:
            return kMediaTagBluraySpareArea;
        case kDataTypeAacsVolumeIdentifier:
            return kMediaTagAacsVolumeIdentifier;
        case kDataTypeAacsSerialNumber:
            return kMediaTagAacsSerialNumber;
        case kDataTypeAacsMediaIdentifier:
            return kMediaTagAacsMediaIdentifier;
        case kDataTypeAacsMkb:
            return kMediaTagAacsMkb;
        case kDataTypeAacsDataKeys:
            return kMediaTagAacsDataKeys;
        case kDataTypeAacsLbaExtents:
            return kMediaTagAacsLbaExtents;
        case kDataTypeCprmMkb:
            return kMediaTagCprmMkb;
        case kDataTypeHybridRecognizedLayers:
            return kMediaTagHybridRecognizedLayers;
        case kDataTypeMmcWriteProtection:
            return kMediaTagMmcWriteProtection;
        case kDataTypeMmcDiscInformation:
            return kMediaTagMmcDiscInformation;
        case kDataTypeMmcTrackResourcesInformation:
            return kMediaTagMmcTrackResourcesInformation;
        case kDataTypeMmcPowResourcesInformation:
            return kMediaTagMmcPowResourcesInformation;
        case kDataTypeScsiInquiry:
            return kMediaTagScsiInquiry;
        case kDataTypeScsiModePage2A:
            return kMediaTagScsiModePage2A;
        case kDataTypeAtaIdentify:
            return kMediaTagAtaIdentify;
        case kDataTypeAtapiIdentify:
            return kMediaTagAtapiIdentify;
        case kDataTypePcmciaCis:
            return kMediaTagPcmciaCis;
        case kDataTypeSdCid:
            return kMediaTagSdCid;
        case kDataTypeSdCsd:
            return kMediaTagSdCsd;
        case kDataTypeSdScr:
            return kMediaTagSdScr;
        case kDataTypeSdOcr:
            return kMediaTagSdOcr;
        case kDataTypeMmcCid:
            return kMediaTagMmcCid;
        case kDataTypeMmcCsd:
            return kMediaTagMmcCsd;
        case kDataTypeMmcOcr:
            return kMediaTagMmcOcr;
        case kDataTypeExtendedCsd:
            return kMediaTagExtendedCsd;
        case kDataTypeXboxSecuritySector:
            return kMediaTagXboxSecuritySector;
        case kDataTypeFloppyLeadOut:
            return kMediaTagFloppyLeadOut;
        case kDataTypeDiscControlBlock:
            return kMediaTagDiscControlBlock;
        case kDataTypeCdFirstTrackPregap:
            return kMediaTagCdFirstTrackPregap;
        case kDataTypeCdLeadOut:
            return kMediaTagCdLeadOut;
        case kDataTypeScsiModeSense6:
            return kMediaTagScsiModeSense6;
        case kDataTypeScsiModeSense10:
            return kMediaTagScsiModeSense10;
        case kDataTypeUsbDescriptors:
            return kMediaTagUsbDescriptors;
        case kDataTypeXboxDmi:
            return kMediaTagXboxDmi;
        case kDataTypeXboxPfi:
            return kMediaTagXboxPfi;
        case kDataTypeCdMcn:
            return kMediaTagCdMcn;
        case kDataTypeCdLeadIn:
            return kMediaTagCdLeadIn;
        case kDataTypeDvdDiscKeyDecrypted:
            return kMediaTagDvdDiscKeyDecrypted;
        case kDataTypeFloppyWriteProtect:
            return kMediaTagFloppyWriteProtect;
        case kDataTypeWiiUDiscKey:
            return kMediaTagWiiUDiscKey;
        case kDataTypePs3DiscKey:
            return kMediaTagPs3DiscKey;
        case kDataTypePs3Data1:
            return kMediaTagPs3Data1;
        case kDataTypePs3Data2:
            return kMediaTagPs3Data2;
        case kDataTypePs3Pic:
            return kMediaTagPs3Pic;
        case kDataTypePs3EncryptionMap:
            return kMediaTagPs3EncryptionMap;
        case kDataTypeWiiUPartitionKeyMap:
            return kMediaTagWiiUPartitionKeyMap;
        case kDataTypeWiiPartitionKeyMap:
            return kMediaTagWiiPartitionKeyMap;
        case kDataTypeNgcwJunkMap:
            return kMediaTagNgcwJunkMap;
        default:
            return -1;
    }
}

/**
 * @brief Converts an Aaru media tag type to an image data type.
 *
 * Maps the given Aaru media tag type to the corresponding image data type.
 * This is the inverse function of aaruf_get_media_tag_type_for_datatype.
 *
 * @param tag_type Aaru media tag type identifier.
 * @return Corresponding image data type, or -1 if not found.
 */
AARU_LOCAL int32_t AARU_CALL aaruf_get_datatype_for_media_tag_type(const int32_t tag_type)
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
        case kMediaTagWiiUDiscKey:
            return kDataTypeWiiUDiscKey;
        case kMediaTagPs3DiscKey:
            return kDataTypePs3DiscKey;
        case kMediaTagPs3Data1:
            return kDataTypePs3Data1;
        case kMediaTagPs3Data2:
            return kDataTypePs3Data2;
        case kMediaTagPs3Pic:
            return kDataTypePs3Pic;
        case kMediaTagPs3EncryptionMap:
            return kDataTypePs3EncryptionMap;
        case kMediaTagWiiUPartitionKeyMap:
            return kDataTypeWiiUPartitionKeyMap;
        case kMediaTagWiiPartitionKeyMap:
            return kDataTypeWiiPartitionKeyMap;
        case kMediaTagNgcwJunkMap:
            return kDataTypeNgcwJunkMap;
        default:
            return -1;
    }
}

// Get the CICM XML media type from AARU media type
AARU_LOCAL int32_t AARU_CALL aaruf_get_xml_mediatype(const int32_t type)
{
    switch(type)
    {
        case CD:
        case CDDA:
        case CDG:
        case CDEG:
        case CDI:
        case CDIREADY:
        case CDROM:
        case CDROMXA:
        case CDPLUS:
        case CDMO:
        case CDR:
        case CDRW:
        case CDMRW:
        case VCD:
        case SVCD:
        case PCD:
        case SACD:
        case DDCD:
        case DDCDR:
        case DDCDRW:
        case DTSCD:
        case CDMIDI:
        case CDV:
        case DVDROM:
        case DVDR:
        case DVDRW:
        case DVDPR:
        case DVDPRW:
        case DVDPRWDL:
        case DVDRDL:
        case DVDPRDL:
        case DVDRAM:
        case DVDRWDL:
        case DVDDownload:
        case HDDVDROM:
        case HDDVDRAM:
        case HDDVDR:
        case HDDVDRW:
        case HDDVDRDL:
        case HDDVDRWDL:
        case BDROM:
        case BDR:
        case BDRE:
        case BDRXL:
        case BDREXL:
        case EVD:
        case FVD:
        case HVD:
        case CBHD:
        case HDVMD:
        case VCDHD:
        case SVOD:
        case FDDVD:
        case LD:
        case LDROM:
        case LDROM2:
        case LVROM:
        case MegaLD:
        case PS1CD:
        case PS2CD:
        case PS2DVD:
        case PS3DVD:
        case PS3BD:
        case PS4BD:
        case UMD:
        case XGD:
        case XGD2:
        case XGD3:
        case XGD4:
        case MEGACD:
        case SATURNCD:
        case GDROM:
        case GDR:
        case SuperCDROM2:
        case JaguarCD:
        case ThreeDO:
        case PCFX:
        case NeoGeoCD:
        case GOD:
        case WOD:
        case WUOD:
        case CDTV:
        case CD32:
        case Nuon:
        case Playdia:
        case Pippin:
        case FMTOWNS:
        case MilCD:
        case VideoNow:
        case VideoNowColor:
        case VideoNowXp:
            return OpticalDisc;
        default:
            return BlockMedia;
    }
}

/**
 * @brief Comparison function for sorting DumpExtent arrays by start sector.
 *
 * This function is used by qsort() to order dump extents in ascending order based on their
 * start sector values. Extents with lower start sectors will appear first in the sorted array.
 * This ordering is important for efficient extent lookup and validation during image operations.
 *
 * @param a Pointer to the first DumpExtent to compare.
 * @param b Pointer to the second DumpExtent to compare.
 * @return Negative value if a->start < b->start, zero if equal, positive if a->start > b->start.
 */
int compare_extents(const void *a, const void *b)
{
    const DumpExtent *extent_a = a;
    const DumpExtent *extent_b = b;

    if(extent_a->start < extent_b->start) return -1;
    if(extent_a->start > extent_b->start) return 1;
    return 0;
}

/**
 * @brief Generates cryptographically strong random bytes.
 *
 * This function fills the provided buffer with random bytes using platform-appropriate
 * cryptographic random number generators. On Unix-like systems (including macOS and Linux),
 * it reads from /dev/urandom. On Windows, it uses CryptGenRandom. If the platform-specific
 * methods fail, it falls back to a time-seeded pseudo-random generator.
 *
 * @param buffer Pointer to the buffer to fill with random bytes.
 * @param length Number of random bytes to generate.
 */
void generate_random_bytes(uint8_t *buffer, size_t length)
{
    if(buffer == NULL || length == 0) return;

#if defined(_WIN32) || defined(_WIN64)
    // Windows implementation using CryptGenRandom
    HCRYPTPROV hCryptProv;
    if(CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hCryptProv, (DWORD)length, buffer);
        CryptReleaseContext(hCryptProv, 0);
        return;
    }
#else
    // Unix-like systems (Linux, macOS, BSD, etc.) - use /dev/urandom
    FILE *urandom = fopen("/dev/urandom", "rb");
    if(urandom != NULL)
    {
        size_t bytes_read = fread(buffer, 1, length, urandom);
        fclose(urandom);
        if(bytes_read == length) return;
    }
#endif

    // Fallback: use time-seeded rand() if platform-specific methods fail
    // This is less secure but ensures we always generate some random data
    srand((unsigned int)time(NULL));
    for(size_t i = 0; i < length; i++) { buffer[i] = (uint8_t)(rand() % 256); }
}
