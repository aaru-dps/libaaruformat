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
#include <wincrypt.h>
#include <windows.h>
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
        case CompactDiscPartialToc:
            return kMediaTagCdToc;
        case CompactDiscSessionInfo:
            return kMediaTagSessionInfo;
        case CompactDiscToc:
            return kMediaTagFullToc;
        case CompactDiscPma:
            return kMediaTagCdPma;
        case CompactDiscAtip:
            return kMediaTagCdAtip;
        case CompactDiscLeadInCdText:
            return kMediaTagCdText;
        case DvdPfi:
            return kMediaTagDvdPfi;
        case DvdPfi2ndLayer:
            return kMediaTagDvdPfi2ndLayer;
        case DvdLeadInCmi:
            return kMediaTagDvdCmi;
        case DvdDiscKey:
            return kMediaTagDvdDiscKey;
        case DvdBca:
            return kMediaTagDvdBca;
        case DvdDmi:
            return kMediaTagDvdDmi;
        case DvdMediaIdentifier:
            return kMediaTagDvdMediaIdentifier;
        case DvdMediaKeyBlock:
            return kMediaTagDvdMkb;
        case DvdRamDds:
            return kMediaTagDvdRamDds;
        case DvdRamMediumStatus:
            return kMediaTagDvdRamMediumStatus;
        case DvdRamSpareArea:
            return kMediaTagDvdRamSpareArea;
        case DvdRRmd:
            return kMediaTagDvdrRmd;
        case DvdRPrerecordedInfo:
            return kMediaTagDvdrPreRecordedInfo;
        case DvdRMediaIdentifier:
            return kMediaTagDvdrMediaIdentifier;
        case DvdRPfi:
            return kMediaTagDvdrPfi;
        case DvdAdip:
            return kMediaTagDvdAdip;
        case HdDvdCpi:
            return kMediaTagHddvdCpi;
        case HdDvdMediumStatus:
            return kMediaTagHddvdMediumStatus;
        case DvdDlLayerCapacity:
            return kMediaTagDvddlLayerCapacity;
        case DvdDlMiddleZoneAddress:
            return kMediaTagDvddlMiddleZoneAddress;
        case DvdDlJumpIntervalSize:
            return kMediaTagDvddlJumpIntervalSize;
        case DvdDlManualLayerJumpLba:
            return kMediaTagDvddlManualLayerJumpLba;
        case BlurayDi:
            return kMediaTagBlurayDi;
        case BlurayBca:
            return kMediaTagBlurayBca;
        case BlurayDds:
            return kMediaTagBlurayDds;
        case BlurayCartridgeStatus:
            return kMediaTagBlurayCartridgeStatus;
        case BluraySpareArea:
            return kMediaTagBluraySpareArea;
        case AacsVolumeIdentifier:
            return kMediaTagAacsVolumeIdentifier;
        case AacsSerialNumber:
            return kMediaTagAacsSerialNumber;
        case AacsMediaIdentifier:
            return kMediaTagAacsMediaIdentifier;
        case AacsMediaKeyBlock:
            return kMediaTagAacsMkb;
        case AacsDataKeys:
            return kMediaTagAacsDataKeys;
        case AacsLbaExtents:
            return kMediaTagAacsLbaExtents;
        case CprmMediaKeyBlock:
            return kMediaTagCprmMkb;
        case HybridRecognizedLayers:
            return kMediaTagHybridRecognizedLayers;
        case ScsiMmcWriteProtection:
            return kMediaTagMmcWriteProtection;
        case ScsiMmcDiscInformation:
            return kMediaTagMmcDiscInformation;
        case ScsiMmcTrackResourcesInformation:
            return kMediaTagMmcTrackResourcesInformation;
        case ScsiMmcPowResourcesInformation:
            return kMediaTagMmcPowResourcesInformation;
        case ScsiInquiry:
            return kMediaTagScsiInquiry;
        case ScsiModePage2A:
            return kMediaTagScsiModePage2A;
        case AtaIdentify:
            return kMediaTagAtaIdentify;
        case AtapiIdentify:
            return kMediaTagAtapiIdentify;
        case PcmciaCis:
            return kMediaTagPcmciaCis;
        case SecureDigitalCid:
            return kMediaTagSdCid;
        case SecureDigitalCsd:
            return kMediaTagSdCsd;
        case SecureDigitalScr:
            return kMediaTagSdScr;
        case SecureDigitalOcr:
            return kMediaTagSdOcr;
        case MultiMediaCardCid:
            return kMediaTagMmcCid;
        case MultiMediaCardCsd:
            return kMediaTagMmcCsd;
        case MultiMediaCardOcr:
            return kMediaTagMmcOcr;
        case MultiMediaCardExtendedCsd:
            return kMediaTagExtendedCsd;
        case XboxSecuritySector:
            return kMediaTagXboxSecuritySector;
        case FloppyLeadOut:
            return kMediaTagFloppyLeadOut;
        case DvdDiscControlBlock:
            return kMediaTagDiscControlBlock;
        case CompactDiscFirstTrackPregap:
            return kMediaTagCdFirstTrackPregap;
        case CompactDiscLeadOut:
            return kMediaTagCdLeadOut;
        case ScsiModeSense6:
            return kMediaTagScsiModeSense6;
        case ScsiModeSense10:
            return kMediaTagScsiModeSense10;
        case UsbDescriptors:
            return kMediaTagUsbDescriptors;
        case XboxDmi:
            return kMediaTagXboxDmi;
        case XboxPfi:
            return kMediaTagXboxPfi;
        case CompactDiscMediaCatalogueNumber:
            return kMediaTagCdMcn;
        case CompactDiscLeadIn:
            return kMediaTagCdLeadIn;
        case DvdDiscKeyDecrypted:
            return kMediaTagDvdDiscKeyDecrypted;
        case FloppyWriteProtectStatus:
            return kMediaTagFloppyWriteProtect;
        case NintendoWiiUDiscKey:
            return kMediaTagWiiUDiscKey;
        case PS3DiscKey:
            return kMediaTagPs3DiscKey;
        case PS3Data1:
            return kMediaTagPs3Data1;
        case PS3Data2:
            return kMediaTagPs3Data2;
        case PS3PIC:
            return kMediaTagPs3Pic;
        case PS3EncryptionMap:
            return kMediaTagPs3EncryptionMap;
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
            return CompactDiscPartialToc;
        case kMediaTagSessionInfo:
            return CompactDiscSessionInfo;
        case kMediaTagFullToc:
            return CompactDiscToc;
        case kMediaTagCdPma:
            return CompactDiscPma;
        case kMediaTagCdAtip:
            return CompactDiscAtip;
        case kMediaTagCdText:
            return CompactDiscLeadInCdText;
        case kMediaTagDvdPfi:
            return DvdPfi;
        case kMediaTagDvdPfi2ndLayer:
            return DvdPfi2ndLayer;
        case kMediaTagDvdCmi:
            return DvdLeadInCmi;
        case kMediaTagDvdDiscKey:
            return DvdDiscKey;
        case kMediaTagDvdBca:
            return DvdBca;
        case kMediaTagDvdDmi:
            return DvdDmi;
        case kMediaTagDvdMediaIdentifier:
            return DvdMediaIdentifier;
        case kMediaTagDvdMkb:
            return DvdMediaKeyBlock;
        case kMediaTagDvdRamDds:
            return DvdRamDds;
        case kMediaTagDvdRamMediumStatus:
            return DvdRamMediumStatus;
        case kMediaTagDvdRamSpareArea:
            return DvdRamSpareArea;
        case kMediaTagDvdrRmd:
            return DvdRRmd;
        case kMediaTagDvdrPreRecordedInfo:
            return DvdRPrerecordedInfo;
        case kMediaTagDvdrMediaIdentifier:
            return DvdRMediaIdentifier;
        case kMediaTagDvdrPfi:
            return DvdRPfi;
        case kMediaTagDvdAdip:
            return DvdAdip;
        case kMediaTagHddvdCpi:
            return HdDvdCpi;
        case kMediaTagHddvdMediumStatus:
            return HdDvdMediumStatus;
        case kMediaTagDvddlLayerCapacity:
            return DvdDlLayerCapacity;
        case kMediaTagDvddlMiddleZoneAddress:
            return DvdDlMiddleZoneAddress;
        case kMediaTagDvddlJumpIntervalSize:
            return DvdDlJumpIntervalSize;
        case kMediaTagDvddlManualLayerJumpLba:
            return DvdDlManualLayerJumpLba;
        case kMediaTagBlurayDi:
            return BlurayDi;
        case kMediaTagBlurayBca:
            return BlurayBca;
        case kMediaTagBlurayDds:
            return BlurayDds;
        case kMediaTagBlurayCartridgeStatus:
            return BlurayCartridgeStatus;
        case kMediaTagBluraySpareArea:
            return BluraySpareArea;
        case kMediaTagAacsVolumeIdentifier:
            return AacsVolumeIdentifier;
        case kMediaTagAacsSerialNumber:
            return AacsSerialNumber;
        case kMediaTagAacsMediaIdentifier:
            return AacsMediaIdentifier;
        case kMediaTagAacsMkb:
            return AacsMediaKeyBlock;
        case kMediaTagAacsDataKeys:
            return AacsDataKeys;
        case kMediaTagAacsLbaExtents:
            return AacsLbaExtents;
        case kMediaTagCprmMkb:
            return CprmMediaKeyBlock;
        case kMediaTagHybridRecognizedLayers:
            return HybridRecognizedLayers;
        case kMediaTagMmcWriteProtection:
            return ScsiMmcWriteProtection;
        case kMediaTagMmcDiscInformation:
            return ScsiMmcDiscInformation;
        case kMediaTagMmcTrackResourcesInformation:
            return ScsiMmcTrackResourcesInformation;
        case kMediaTagMmcPowResourcesInformation:
            return ScsiMmcPowResourcesInformation;
        case kMediaTagScsiInquiry:
            return ScsiInquiry;
        case kMediaTagScsiModePage2A:
            return ScsiModePage2A;
        case kMediaTagAtaIdentify:
            return AtaIdentify;
        case kMediaTagAtapiIdentify:
            return AtapiIdentify;
        case kMediaTagPcmciaCis:
            return PcmciaCis;
        case kMediaTagSdCid:
            return SecureDigitalCid;
        case kMediaTagSdCsd:
            return SecureDigitalCsd;
        case kMediaTagSdScr:
            return SecureDigitalScr;
        case kMediaTagSdOcr:
            return SecureDigitalOcr;
        case kMediaTagMmcCid:
            return MultiMediaCardCid;
        case kMediaTagMmcCsd:
            return MultiMediaCardCsd;
        case kMediaTagMmcOcr:
            return MultiMediaCardOcr;
        case kMediaTagExtendedCsd:
            return MultiMediaCardExtendedCsd;
        case kMediaTagXboxSecuritySector:
            return XboxSecuritySector;
        case kMediaTagFloppyLeadOut:
            return FloppyLeadOut;
        case kMediaTagDiscControlBlock:
            return DvdDiscControlBlock;
        case kMediaTagCdFirstTrackPregap:
            return CompactDiscFirstTrackPregap;
        case kMediaTagCdLeadOut:
            return CompactDiscLeadOut;
        case kMediaTagScsiModeSense6:
            return ScsiModeSense6;
        case kMediaTagScsiModeSense10:
            return ScsiModeSense10;
        case kMediaTagUsbDescriptors:
            return UsbDescriptors;
        case kMediaTagXboxDmi:
            return XboxDmi;
        case kMediaTagXboxPfi:
            return XboxPfi;
        case kMediaTagCdMcn:
            return CompactDiscMediaCatalogueNumber;
        case kMediaTagCdLeadIn:
            return CompactDiscLeadIn;
        case kMediaTagDvdDiscKeyDecrypted:
            return DvdDiscKeyDecrypted;
        case kMediaTagFloppyWriteProtect:
            return FloppyWriteProtectStatus;
        case kMediaTagWiiUDiscKey:
            return NintendoWiiUDiscKey;
        case kMediaTagPs3DiscKey:
            return PS3DiscKey;
        case kMediaTagPs3Data1:
            return PS3Data1;
        case kMediaTagPs3Data2:
            return PS3Data2;
        case kMediaTagPs3Pic:
            return PS3PIC;
        case kMediaTagPs3EncryptionMap:
            return PS3EncryptionMap;
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
