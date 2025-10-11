/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

#include <aaru.h>

#include <aaruformat.h>

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
            return CD_TOC;
        case CompactDiscSessionInfo:
            return CD_SessionInfo;
        case CompactDiscToc:
            return CD_FullTOC;
        case CompactDiscPma:
            return CD_PMA;
        case CompactDiscAtip:
            return CD_ATIP;
        case CompactDiscLeadInCdText:
            return CD_TEXT;
        case DvdPfi:
            return DVD_PFI;
        case DvdLeadInCmi:
            return DVD_CMI;
        case DvdDiscKey:
            return DVD_DiscKey;
        case DvdBca:
            return DVD_BCA;
        case DvdDmi:
            return DVD_DMI;
        case DvdMediaIdentifier:
            return DVD_MediaIdentifier;
        case DvdMediaKeyBlock:
            return DVD_MKB;
        case DvdRamDds:
            return DVDRAM_DDS;
        case DvdRamMediumStatus:
            return DVDRAM_MediumStatus;
        case DvdRamSpareArea:
            return DVDRAM_SpareArea;
        case DvdRRmd:
            return DVDR_RMD;
        case DvdRPrerecordedInfo:
            return DVDR_PreRecordedInfo;
        case DvdRMediaIdentifier:
            return DVDR_MediaIdentifier;
        case DvdRPfi:
            return DVDR_PFI;
        case DvdAdip:
            return DVD_ADIP;
        case HdDvdCpi:
            return HDDVD_CPI;
        case HdDvdMediumStatus:
            return HDDVD_MediumStatus;
        case DvdDlLayerCapacity:
            return DVDDL_LayerCapacity;
        case DvdDlMiddleZoneAddress:
            return DVDDL_MiddleZoneAddress;
        case DvdDlJumpIntervalSize:
            return DVDDL_JumpIntervalSize;
        case DvdDlManualLayerJumpLba:
            return DVDDL_ManualLayerJumpLBA;
        case BlurayDi:
            return BD_DI;
        case BlurayBca:
            return BD_BCA;
        case BlurayDds:
            return BD_DDS;
        case BlurayCartridgeStatus:
            return BD_CartridgeStatus;
        case BluraySpareArea:
            return BD_SpareArea;
        case AacsVolumeIdentifier:
            return AACS_VolumeIdentifier;
        case AacsSerialNumber:
            return AACS_SerialNumber;
        case AacsMediaIdentifier:
            return AACS_MediaIdentifier;
        case AacsMediaKeyBlock:
            return AACS_MKB;
        case AacsDataKeys:
            return AACS_DataKeys;
        case AacsLbaExtents:
            return AACS_LBAExtents;
        case CprmMediaKeyBlock:
            return AACS_CPRM_MKB;
        case HybridRecognizedLayers:
            return Hybrid_RecognizedLayers;
        case ScsiMmcWriteProtection:
            return MMC_WriteProtection;
        case ScsiMmcDiscInformation:
            return MMC_DiscInformation;
        case ScsiMmcTrackResourcesInformation:
            return MMC_TrackResourcesInformation;
        case ScsiMmcPowResourcesInformation:
            return MMC_POWResourcesInformation;
        case ScsiInquiry:
            return SCSI_INQUIRY;
        case ScsiModePage2A:
            return SCSI_MODEPAGE_2A;
        case AtaIdentify:
            return ATA_IDENTIFY;
        case AtapiIdentify:
            return ATAPI_IDENTIFY;
        case PcmciaCis:
            return PCMCIA_CIS;
        case SecureDigitalCid:
            return SD_CID;
        case SecureDigitalCsd:
            return SD_CSD;
        case SecureDigitalScr:
            return SD_SCR;
        case SecureDigitalOcr:
            return SD_OCR;
        case MultiMediaCardCid:
            return MMC_CID;
        case MultiMediaCardCsd:
            return MMC_CSD;
        case MultiMediaCardOcr:
            return MMC_OCR;
        case MultiMediaCardExtendedCsd:
            return MMC_ExtendedCSD;
        case XboxSecuritySector:
            return Xbox_SecuritySector;
        case FloppyLeadOut:
            return Floppy_LeadOut;
        case DvdDiscControlBlock:
            return DCB;
        case CompactDiscFirstTrackPregap:
            return CD_FirstTrackPregap;
        case CompactDiscLeadOut:
            return CD_LeadOut;
        case ScsiModeSense6:
            return SCSI_MODESENSE_6;
        case ScsiModeSense10:
            return SCSI_MODESENSE_10;
        case UsbDescriptors:
            return USB_Descriptors;
        case XboxDmi:
            return Xbox_DMI;
        case XboxPfi:
            return Xbox_PFI;
        case CompactDiscMediaCatalogueNumber:
            return CD_MCN;
        case CompactDiscLeadIn:
            return CD_LeadIn;
        case DvdDiscKeyDecrypted:
            return DVD_DiscKey_Decrypted;
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
        case CD_TOC:
            return CompactDiscPartialToc;
        case CD_SessionInfo:
            return CompactDiscSessionInfo;
        case CD_FullTOC:
            return CompactDiscToc;
        case CD_PMA:
            return CompactDiscPma;
        case CD_ATIP:
            return CompactDiscAtip;
        case CD_TEXT:
            return CompactDiscLeadInCdText;
        case DVD_PFI:
            return DvdPfi;
        case DVD_CMI:
            return DvdLeadInCmi;
        case DVD_DiscKey:
            return DvdDiscKey;
        case DVD_BCA:
            return DvdBca;
        case DVD_DMI:
            return DvdDmi;
        case DVD_MediaIdentifier:
            return DvdMediaIdentifier;
        case DVD_MKB:
            return DvdMediaKeyBlock;
        case DVDRAM_DDS:
            return DvdRamDds;
        case DVDRAM_MediumStatus:
            return DvdRamMediumStatus;
        case DVDRAM_SpareArea:
            return DvdRamSpareArea;
        case DVDR_RMD:
            return DvdRRmd;
        case DVDR_PreRecordedInfo:
            return DvdRPrerecordedInfo;
        case DVDR_MediaIdentifier:
            return DvdRMediaIdentifier;
        case DVDR_PFI:
            return DvdRPfi;
        case DVD_ADIP:
            return DvdAdip;
        case HDDVD_CPI:
            return HdDvdCpi;
        case HDDVD_MediumStatus:
            return HdDvdMediumStatus;
        case DVDDL_LayerCapacity:
            return DvdDlLayerCapacity;
        case DVDDL_MiddleZoneAddress:
            return DvdDlMiddleZoneAddress;
        case DVDDL_JumpIntervalSize:
            return DvdDlJumpIntervalSize;
        case DVDDL_ManualLayerJumpLBA:
            return DvdDlManualLayerJumpLba;
        case BD_DI:
            return BlurayDi;
        case BD_BCA:
            return BlurayBca;
        case BD_DDS:
            return BlurayDds;
        case BD_CartridgeStatus:
            return BlurayCartridgeStatus;
        case BD_SpareArea:
            return BluraySpareArea;
        case AACS_VolumeIdentifier:
            return AacsVolumeIdentifier;
        case AACS_SerialNumber:
            return AacsSerialNumber;
        case AACS_MediaIdentifier:
            return AacsMediaIdentifier;
        case AACS_MKB:
            return AacsMediaKeyBlock;
        case AACS_DataKeys:
            return AacsDataKeys;
        case AACS_LBAExtents:
            return AacsLbaExtents;
        case AACS_CPRM_MKB:
            return CprmMediaKeyBlock;
        case Hybrid_RecognizedLayers:
            return HybridRecognizedLayers;
        case MMC_WriteProtection:
            return ScsiMmcWriteProtection;
        case MMC_DiscInformation:
            return ScsiMmcDiscInformation;
        case MMC_TrackResourcesInformation:
            return ScsiMmcTrackResourcesInformation;
        case MMC_POWResourcesInformation:
            return ScsiMmcPowResourcesInformation;
        case SCSI_INQUIRY:
            return ScsiInquiry;
        case SCSI_MODEPAGE_2A:
            return ScsiModePage2A;
        case ATA_IDENTIFY:
            return AtaIdentify;
        case ATAPI_IDENTIFY:
            return AtapiIdentify;
        case PCMCIA_CIS:
            return PcmciaCis;
        case SD_CID:
            return SecureDigitalCid;
        case SD_CSD:
            return SecureDigitalCsd;
        case SD_SCR:
            return SecureDigitalScr;
        case SD_OCR:
            return SecureDigitalOcr;
        case MMC_CID:
            return MultiMediaCardCid;
        case MMC_CSD:
            return MultiMediaCardCsd;
        case MMC_OCR:
            return MultiMediaCardOcr;
        case MMC_ExtendedCSD:
            return MultiMediaCardExtendedCsd;
        case Xbox_SecuritySector:
            return XboxSecuritySector;
        case Floppy_LeadOut:
            return FloppyLeadOut;
        case DCB:
            return DvdDiscControlBlock;
        case CD_FirstTrackPregap:
            return CompactDiscFirstTrackPregap;
        case CD_LeadOut:
            return CompactDiscLeadOut;
        case SCSI_MODESENSE_6:
            return ScsiModeSense6;
        case SCSI_MODESENSE_10:
            return ScsiModeSense10;
        case USB_Descriptors:
            return UsbDescriptors;
        case Xbox_DMI:
            return XboxDmi;
        case Xbox_PFI:
            return XboxPfi;
        case CD_MCN:
            return CompactDiscMediaCatalogueNumber;
        case CD_LeadIn:
            return CompactDiscLeadIn;
        case DVD_DiscKey_Decrypted:
            return DvdDiscKeyDecrypted;
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

    if(extent_a->start < extent_b->start)
        return -1;
    if(extent_a->start > extent_b->start)
        return 1;
    return 0;
}
