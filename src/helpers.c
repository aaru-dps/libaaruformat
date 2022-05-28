// /***************************************************************************
// Aaru Data Preservation Suite
// ----------------------------------------------------------------------------
//
// Filename       : helpers.c
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : Disk image plugins.
//
// --[ Description ] ----------------------------------------------------------
//
//     Contains helpers for Aaru format disk images.
//
// --[ License ] --------------------------------------------------------------
//
//     This library is free software; you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General Public License as
//     published by the Free Software Foundation; either version 2.1 of the
//     License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful, but
//     WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//     Lesser General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License along with this library; if not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2020 Natalia Portillo
// ****************************************************************************/

#include <aaru.h>
#include <aaruformat.h>

// Converts between image data type and aaru media tag type
int32_t aaruf_get_media_tag_type_for_datatype(int32_t type)
{
    switch(type)
    {
        case CompactDiscPartialToc: return CD_TOC;
        case CompactDiscSessionInfo: return CD_SessionInfo;
        case CompactDiscToc: return CD_FullTOC;
        case CompactDiscPma: return CD_PMA;
        case CompactDiscAtip: return CD_ATIP;
        case CompactDiscLeadInCdText: return CD_TEXT;
        case DvdPfi: return DVD_PFI;
        case DvdLeadInCmi: return DVD_CMI;
        case DvdDiscKey: return DVD_DiscKey;
        case DvdBca: return DVD_BCA;
        case DvdDmi: return DVD_DMI;
        case DvdMediaIdentifier: return DVD_MediaIdentifier;
        case DvdMediaKeyBlock: return DVD_MKB;
        case DvdRamDds: return DVDRAM_DDS;
        case DvdRamMediumStatus: return DVDRAM_MediumStatus;
        case DvdRamSpareArea: return DVDRAM_SpareArea;
        case DvdRRmd: return DVDR_RMD;
        case DvdRPrerecordedInfo: return DVDR_PreRecordedInfo;
        case DvdRMediaIdentifier: return DVDR_MediaIdentifier;
        case DvdRPfi: return DVDR_PFI;
        case DvdAdip: return DVD_ADIP;
        case HdDvdCpi: return HDDVD_CPI;
        case HdDvdMediumStatus: return HDDVD_MediumStatus;
        case DvdDlLayerCapacity: return DVDDL_LayerCapacity;
        case DvdDlMiddleZoneAddress: return DVDDL_MiddleZoneAddress;
        case DvdDlJumpIntervalSize: return DVDDL_JumpIntervalSize;
        case DvdDlManualLayerJumpLba: return DVDDL_ManualLayerJumpLBA;
        case BlurayDi: return BD_DI;
        case BlurayBca: return BD_BCA;
        case BlurayDds: return BD_DDS;
        case BlurayCartridgeStatus: return BD_CartridgeStatus;
        case BluraySpareArea: return BD_SpareArea;
        case AacsVolumeIdentifier: return AACS_VolumeIdentifier;
        case AacsSerialNumber: return AACS_SerialNumber;
        case AacsMediaIdentifier: return AACS_MediaIdentifier;
        case AacsMediaKeyBlock: return AACS_MKB;
        case AacsDataKeys: return AACS_DataKeys;
        case AacsLbaExtents: return AACS_LBAExtents;
        case CprmMediaKeyBlock: return AACS_CPRM_MKB;
        case HybridRecognizedLayers: return Hybrid_RecognizedLayers;
        case ScsiMmcWriteProtection: return MMC_WriteProtection;
        case ScsiMmcDiscInformation: return MMC_DiscInformation;
        case ScsiMmcTrackResourcesInformation: return MMC_TrackResourcesInformation;
        case ScsiMmcPowResourcesInformation: return MMC_POWResourcesInformation;
        case ScsiInquiry: return SCSI_INQUIRY;
        case ScsiModePage2A: return SCSI_MODEPAGE_2A;
        case AtaIdentify: return ATA_IDENTIFY;
        case AtapiIdentify: return ATAPI_IDENTIFY;
        case PcmciaCis: return PCMCIA_CIS;
        case SecureDigitalCid: return SD_CID;
        case SecureDigitalCsd: return SD_CSD;
        case SecureDigitalScr: return SD_SCR;
        case SecureDigitalOcr: return SD_OCR;
        case MultiMediaCardCid: return MMC_CID;
        case MultiMediaCardCsd: return MMC_CSD;
        case MultiMediaCardOcr: return MMC_OCR;
        case MultiMediaCardExtendedCsd: return MMC_ExtendedCSD;
        case XboxSecuritySector: return Xbox_SecuritySector;
        case FloppyLeadOut: return Floppy_LeadOut;
        case DvdDiscControlBlock: return DCB;
        case CompactDiscFirstTrackPregap: return CD_FirstTrackPregap;
        case CompactDiscLeadOut: return CD_LeadOut;
        case ScsiModeSense6: return SCSI_MODESENSE_6;
        case ScsiModeSense10: return SCSI_MODESENSE_10;
        case UsbDescriptors: return USB_Descriptors;
        case XboxDmi: return Xbox_DMI;
        case XboxPfi: return Xbox_PFI;
        case CompactDiscMediaCatalogueNumber: return CD_MCN;
        case CompactDiscLeadIn: return CD_LeadIn;
        default: return -1;
    }
}

// Get the CICM XML media type from AARU media type
int32_t aaruf_get_xml_mediatype(int32_t type)
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
        case VideoNowXp: return OpticalDisc;
        default: return BlockMedia;
    }
}
