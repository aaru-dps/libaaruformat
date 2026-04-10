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

#include <aaru.h>
#include <aaruformat/enums.h>
#include <stdio.h>
#include <stdlib.h>

char *byte_array_to_hex_string(const unsigned char *array, int array_size)
{
    char *hex_string = NULL;
    int   j          = 0;

    hex_string = malloc(array_size * 2 + 1);

    if(hex_string == NULL) return NULL;

    j = 0;
    for(int i = 0; i < array_size; i++)
    {
        hex_string[j] = (array[i] >> 4) + '0';
        if(hex_string[j] > '9') hex_string[j] += 0x7;

        j++;

        hex_string[j] = (array[i] & 0xF) + '0';
        if(hex_string[j] > '9') hex_string[j] += 0x7;

        j++;
    }

    hex_string[j] = 0;

    return hex_string;
}

const char *media_tag_type_to_string(int32_t type)
{
    switch(type)
    {
        case kMediaTagCdToc:
            return "CD Table of Contents";
        case kMediaTagSessionInfo:
            return "CD Session Information";
        case kMediaTagFullToc:
            return "CD Full TOC (multi-session)";
        case kMediaTagCdPma:
            return "CD Program Memory Area";
        case kMediaTagCdAtip:
            return "CD Absolute Time in Pregroove";
        case kMediaTagCdText:
            return "CD-Text";
        case kMediaTagCdMcn:
            return "CD Media Catalogue Number";
        case kMediaTagDvdPfi:
            return "DVD Physical Format Information";
        case kMediaTagDvdCmi:
            return "DVD Copyright Management Information";
        case kMediaTagDvdDiscKey:
            return "DVD Disc Key";
        case kMediaTagDvdBca:
            return "DVD Burst Cutting Area";
        case kMediaTagDvdDmi:
            return "DVD Disc Manufacturing Information";
        case kMediaTagDvdMediaIdentifier:
            return "DVD Media Identifier";
        case kMediaTagDvdMkb:
            return "DVD Media Key Block";
        case kMediaTagDvdRamDds:
            return "DVD-RAM Defect Data Structure";
        case kMediaTagDvdRamMediumStatus:
            return "DVD-RAM Medium Status";
        case kMediaTagDvdRamSpareArea:
            return "DVD-RAM Spare Area";
        case kMediaTagDvdrRmd:
            return "DVD-R Recording Management Data";
        case kMediaTagDvdrPreRecordedInfo:
            return "DVD-R Pre-recorded Information";
        case kMediaTagDvdrMediaIdentifier:
            return "DVD-R Media Identifier";
        case kMediaTagDvdrPfi:
            return "DVD-R Physical Format Information";
        case kMediaTagDvdAdip:
            return "DVD Address in Pregroove";
        case kMediaTagHddvdCpi:
            return "HD DVD Content Protection Information";
        case kMediaTagHddvdMediumStatus:
            return "HD DVD Medium Status";
        case kMediaTagDvddlLayerCapacity:
            return "DVD Dual Layer - Layer Capacity";
        case kMediaTagDvddlMiddleZoneAddress:
            return "DVD Dual Layer - Middle Zone Address";
        case kMediaTagDvddlJumpIntervalSize:
            return "DVD Dual Layer - Jump Interval Size";
        case kMediaTagDvddlManualLayerJumpLba:
            return "DVD Dual Layer - Manual Layer Jump LBA";
        case kMediaTagBlurayDi:
            return "Blu-ray Disc Information";
        case kMediaTagBlurayBca:
            return "Blu-ray Burst Cutting Area";
        case kMediaTagBlurayDds:
            return "Blu-ray Disc Definition Structure";
        case kMediaTagBlurayCartridgeStatus:
            return "Blu-ray Cartridge Status";
        case kMediaTagBluraySpareArea:
            return "Blu-ray Spare Area";
        case kMediaTagAacsVolumeIdentifier:
            return "AACS Volume Identifier";
        case kMediaTagAacsSerialNumber:
            return "AACS Serial Number";
        case kMediaTagAacsMediaIdentifier:
            return "AACS Media Identifier";
        case kMediaTagAacsMkb:
            return "AACS Media Key Block";
        case kMediaTagAacsDataKeys:
            return "AACS Data Keys";
        case kMediaTagAacsLbaExtents:
            return "AACS LBA Extents";
        case kMediaTagCprmMkb:
            return "CPRM Media Key Block";
        case kMediaTagHybridRecognizedLayers:
            return "Hybrid Disc - Recognized Layers";
        case kMediaTagMmcWriteProtection:
            return "MMC Write Protection Status";
        case kMediaTagMmcDiscInformation:
            return "MMC Disc Information";
        case kMediaTagMmcTrackResourcesInformation:
            return "MMC Track Resources Information";
        case kMediaTagMmcPowResourcesInformation:
            return "MMC POW Resources Information";
        case kMediaTagScsiInquiry:
            return "SCSI INQUIRY Data";
        case kMediaTagScsiModePage2A:
            return "SCSI Mode Page 2Ah (CD/DVD Capabilities)";
        case kMediaTagAtaIdentify:
            return "ATA IDENTIFY DEVICE";
        case kMediaTagAtapiIdentify:
            return "ATAPI IDENTIFY DEVICE";
        case kMediaTagPcmciaCis:
            return "PCMCIA Card Information Structure";
        case kMediaTagSdCid:
            return "Secure Digital Card ID";
        case kMediaTagSdCsd:
            return "Secure Digital Card Specific Data";
        case kMediaTagSdScr:
            return "Secure Digital Configuration Register";
        case kMediaTagSdOcr:
            return "Secure Digital Operation Conditions";
        case kMediaTagMmcCid:
            return "MultiMediaCard Card ID";
        case kMediaTagMmcCsd:
            return "MultiMediaCard Card Specific Data";
        case kMediaTagMmcOcr:
            return "MultiMediaCard Operation Conditions";
        case kMediaTagExtendedCsd:
            return "MultiMediaCard Extended CSD";
        case kMediaTagXboxSecuritySector:
            return "Xbox Security Sector";
        case kMediaTagFloppyLeadOut:
            return "Floppy Lead-out";
        case kMediaTagDiscControlBlock:
            return "DVD Disc Control Block";
        case kMediaTagCdFirstTrackPregap:
            return "CD First Track Pre-gap";
        case kMediaTagCdLeadOut:
            return "CD Lead-out";
        case kMediaTagScsiModeSense6:
            return "SCSI MODE SENSE (6)";
        case kMediaTagScsiModeSense10:
            return "SCSI MODE SENSE (10)";
        case kMediaTagUsbDescriptors:
            return "USB Descriptors";
        case kMediaTagXboxDmi:
            return "Xbox Disc Manufacturing Information";
        case kMediaTagXboxPfi:
            return "Xbox Physical Format Information";
        case kMediaTagCdLeadIn:
            return "CD Lead-in";
        case kMediaTagMiniDiscType:
            return "MiniDisc Type";
        case kMediaTagMiniDiscD5:
            return "MiniDisc D5h Response";
        case kMediaTagMiniDiscUtoc:
            return "MiniDisc User TOC";
        case kMediaTagMiniDiscDtoc:
            return "MiniDisc Data TOC";
        case kMediaTagDvdDiscKeyDecrypted:
            return "DVD Disc Key (decrypted)";
        case kMediaTagDvdPfi2ndLayer:
            return "DVD Physical Format Information (2nd layer)";
        case kMediaTagFloppyWriteProtect:
            return "Floppy Write-Protect Status";
        case kMediaTagAacsMediaKey:
            return "AACS Media Key";
        case kMediaTagAacsVolumeUniqueKey:
            return "AACS Volume Unique Key";
        default:
            return "Unknown Media Tag";
    }
}

const char *data_type_to_string(uint16_t type)
{
    switch(type)
    {
        case kDataTypeUserData:
            return "User Data";
        case kDataTypeCdToc:
            return "CD Partial TOC";
        case kDataTypeSessionInfo:
            return "CD Session Information";
        case kDataTypeFullToc:
            return "CD Table of Contents";
        case kDataTypeCdPma:
            return "CD Power Management Area";
        case kDataTypeCdAtip:
            return "CD Absolute Time in Pregroove";
        case kDataTypeCdText:
            return "CD Lead-in CD-TEXT";
        case kDataTypeDvdPfi:
            return "DVD Physical Format Information";
        case kDataTypeDvdCmi:
            return "DVD Lead-in Copyright Management Information";
        case kDataTypeDvdDiscKey:
            return "DVD Disc Key";
        case kDataTypeDvdBca:
            return "DVD Burst Cutting Area";
        case kDataTypeDvdDmi:
            return "DVD Disc Manufacturing Information";
        case kDataTypeDvdMediaIdentifier:
            return "DVD Media Identifier";
        case kDataTypeDvdMkb:
            return "DVD Media Key Block";
        case kDataTypeDvdRamDds:
            return "DVD-RAM Disc Definition Structure";
        case kDataTypeDvdRamMediumStatus:
            return "DVD-RAM Medium Status";
        case kDataTypeDvdRamSpareArea:
            return "DVD-RAM Spare Area Information";
        case kDataTypeDvdrRmd:
            return "DVD-R Recording Management Data";
        case kDataTypeDvdrPreRecordedInfo:
            return "DVD-R Pre-recorded Information";
        case kDataTypeDvdrMediaIdentifier:
            return "DVD-R Media Identifier";
        case kDataTypeDvdrPfi:
            return "DVD-R Physical Format Information";
        case kDataTypeDvdAdip:
            return "DVD Address in Pregroove";
        case kDataTypeHddvdCpi:
            return "HD DVD Content Protection Information";
        case kDataTypeHddvdMediumStatus:
            return "HD DVD Medium Status";
        case kDataTypeDvddlLayerCapacity:
            return "DVD Dual Layer - Layer Capacity";
        case kDataTypeDvddlMiddleZoneAddress:
            return "DVD Dual Layer - Middle Zone Address";
        case kDataTypeDvddlJumpIntervalSize:
            return "DVD Dual Layer - Jump Interval Size";
        case kDataTypeDvddlManualLayerJumpLba:
            return "DVD Dual Layer - Manual Layer Jump LBA";
        case kDataTypeBlurayDi:
            return "Blu-ray Disc Information";
        case kDataTypeBlurayBca:
            return "Blu-ray Burst Cutting Area";
        case kDataTypeBlurayDds:
            return "Blu-ray Disc Definition Structure";
        case kDataTypeBlurayCartridgeStatus:
            return "Blu-ray Cartridge Status";
        case kDataTypeBluraySpareArea:
            return "Blu-ray Spare Area Information";
        case kDataTypeAacsVolumeIdentifier:
            return "AACS Volume Identifier";
        case kDataTypeAacsMediaIdentifier:
            return "AACS Media Identifier";
        case kDataTypeAacsMkb:
            return "AACS Media Key Block";
        case kDataTypeAacsDataKeys:
            return "AACS Data Keys";
        case kDataTypeAacsLbaExtents:
            return "AACS LBA Extents";
        case kDataTypeCprmMkb:
            return "CPRM Media Key Block";
        case kDataTypeHybridRecognizedLayers:
            return "Hybrid Disc - Recognized Layers";
        case kDataTypeMmcWriteProtection:
            return "SCSI MMC Write Protection";
        case kDataTypeMmcDiscInformation:
            return "SCSI MMC Disc Information";
        case kDataTypeMmcTrackResourcesInformation:
            return "SCSI MMC Track Resources Information";
        case kDataTypeMmcPowResourcesInformation:
            return "SCSI MMC POW Resources Information";
        case kDataTypeScsiInquiry:
            return "SCSI INQUIRY Response";
        case kDataTypeScsiModePage2A:
            return "SCSI MODE PAGE 2Ah";
        case kDataTypeAtaIdentify:
            return "ATA IDENTIFY DEVICE";
        case kDataTypeAtapiIdentify:
            return "ATAPI IDENTIFY PACKET DEVICE";
        case kDataTypePcmciaCis:
            return "PCMCIA Card Information Structure";
        case kDataTypeSdCid:
            return "Secure Digital CID Register";
        case kDataTypeSdCsd:
            return "Secure Digital CSD Register";
        case kDataTypeSdScr:
            return "Secure Digital SCR Register";
        case kDataTypeSdOcr:
            return "Secure Digital OCR Register";
        case kDataTypeMmcCid:
            return "MultiMediaCard CID Register";
        case kDataTypeMmcCsd:
            return "MultiMediaCard CSD Register";
        case kDataTypeExtendedCsd:
            return "MultiMediaCard Extended CSD Register";
        case kDataTypeMmcOcr:
            return "MultiMediaCard OCR Register";
        case kDataTypeXboxSecuritySector:
            return "Xbox Security Sector";
        case kDataTypeFloppyLeadOut:
            return "Floppy Lead-out";
        case kDataTypeDiscControlBlock:
            return "DVD Disc Control Block";
        case kDataTypeCdFirstTrackPregap:
            return "CD First Track Pre-gap";
        case kDataTypeCdLeadOut:
            return "CD Lead-out";
        case kDataTypeScsiModeSense6:
            return "SCSI MODE SENSE (6)";
        case kDataTypeScsiModeSense10:
            return "SCSI MODE SENSE (10)";
        case kDataTypeUsbDescriptors:
            return "USB Descriptors";
        case kDataTypeXboxDmi:
            return "Xbox Disc Manufacturing Information";
        case kDataTypeXboxPfi:
            return "Xbox Physical Format Information";
        case kDataTypeCdSectorPrefix:
            return "CD Sector Prefix (sync + header)";
        case kDataTypeCdSectorSuffix:
            return "CD Sector Suffix (EDC + ECC)";
        case kDataTypeCdSubchannel:
            return "CD Sector Subchannel";
        case kDataTypeAppleProfileTag:
            return "Apple Profile Tag";
        case kDataTypeAppleSonyTag:
            return "Apple Sony Tag";
        case kDataTypePriamDataTowerTag:
            return "Priam DataTower Tag";
        case kDataTypeCdMcn:
            return "CD Media Catalogue Number";
        case kDataTypeCdSectorPrefixCorrected:
            return "CD Sector Prefix (corrected)";
        case kDataTypeCdSectorSuffixCorrected:
            return "CD Sector Suffix (corrected)";
        case kDataTypeCdSubHeader:
            return "CD Mode 2 Subheader";
        case kDataTypeCdLeadIn:
            return "CD Lead-in";
        case kDataTypeDvdDiscKeyDecrypted:
            return "DVD Disc Key (decrypted)";
        case kDataTypeDvdSectorCprMai:
            return "DVD Sector Copyright Management Info";
        case kDataTypeDvdTitleKeyDecrypted:
            return "DVD Title Key (decrypted)";
        case kDataTypeDvdSectorId:
            return "DVD Sector ID";
        case kDataTypeDvdSectorIed:
            return "DVD Sector ID Error Detection";
        case kDataTypeDvdSectorEdc:
            return "DVD Sector Error Detection Code";
        case kDataTypeDvdSectorEccPi:
            return "DVD Sector ECC Parity Inner";
        case kDataTypeDvdEccBlockPo:
            return "DVD ECC Block Parity Outer";
        case kDataTypeDvdPfi2ndLayer:
            return "DVD Physical Format Info (2nd layer)";
        case kDataTypeFloppyWriteProtect:
            return "Floppy Write-Protect Status";
        case kDataTypeAacsMediaKey:
            return "AACS Media Key";
        case kDataTypeAacsVolumeUniqueKey:
            return "AACS Volume Unique Key";
        case kDataTypeBdSectorEdc:
            return "Blu-ray Sector EDC";
        default:
            return "Unknown Data Type";
    }
}

const char *media_type_to_string(MediaType type)
{
    switch(type)
    {
        // Generics, types 0 to 9
        case UnknownMedia:
            return "Unknown";
        case UnknownMO:
            return "Unknown Magneto-Optical";
        case GENERIC_HDD:
            return "Generic Hard Disk";
        case Microdrive:
            return "Microdrive";
        case Zone_HDD:
            return "Zoned Hard Disk";
        case FlashDrive:
            return "USB Flash Drive";

        // Compact Disc formats, types 10 to 39
        case CD:
            return "Compact Disc";
        case CDDA:
            return "CD Digital Audio (Red Book)";
        case CDG:
            return "CD+G";
        case CDEG:
            return "CD+EG";
        case CDI:
            return "CD-i (Green Book)";
        case CDROM:
            return "CD-ROM (Yellow Book)";
        case CDROMXA:
            return "CD-ROM XA";
        case CDPLUS:
            return "CD+ (Blue Book)";
        case CDMO:
            return "CD-MO (Orange Book)";
        case CDR:
            return "CD-R";
        case CDRW:
            return "CD-RW";
        case CDMRW:
            return "Mount-Rainier CD-RW";
        case VCD:
            return "Video CD";
        case SVCD:
            return "Super Video CD";
        case PCD:
            return "Photo CD";
        case SACD:
            return "Super Audio CD";
        case DDCD:
            return "Double-Density CD-ROM";
        case DDCDR:
            return "DD CD-R";
        case DDCDRW:
            return "DD CD-RW";
        case DTSCD:
            return "DTS CD";
        case CDMIDI:
            return "CD-MIDI";
        case CDV:
            return "CD-Video";
        case PD650:
            return "PD650";
        case PD650_WORM:
            return "PD650 WORM";
        case CDIREADY:
            return "CD-i Ready";
        case FMTOWNS:
            return "FM Towns CD";

        // DVD formats, types 40 to 50
        case DVDROM:
            return "DVD-ROM";
        case DVDR:
            return "DVD-R";
        case DVDRW:
            return "DVD-RW";
        case DVDPR:
            return "DVD+R";
        case DVDPRW:
            return "DVD+RW";
        case DVDPRWDL:
            return "DVD+RW DL";
        case DVDRDL:
            return "DVD-R DL";
        case DVDPRDL:
            return "DVD+R DL";
        case DVDRAM:
            return "DVD-RAM";
        case DVDRWDL:
            return "DVD-RW DL";
        case DVDDownload:
            return "DVD-Download";

        // HD-DVD formats, types 51 to 59
        case HDDVDROM:
            return "HD DVD-ROM";
        case HDDVDRAM:
            return "HD DVD-RAM";
        case HDDVDR:
            return "HD DVD-R";
        case HDDVDRW:
            return "HD DVD-RW";
        case HDDVDRDL:
            return "HD DVD-R DL";
        case HDDVDRWDL:
            return "HD DVD-RW DL";

        // Blu-ray formats, types 60 to 69
        case BDROM:
            return "BD-ROM";
        case BDR:
            return "BD-R";
        case BDRE:
            return "BD-RE";
        case BDRXL:
            return "BD-R XL";
        case BDREXL:
            return "BD-RE XL";

        // Rare optical formats, types 70 to 79
        case EVD:
            return "Enhanced Versatile Disc";
        case FVD:
            return "Forward Versatile Disc";
        case HVD:
            return "Holographic Versatile Disc";
        case CBHD:
            return "China Blue High Definition";
        case HDVMD:
            return "High Definition Versatile Multilayer Disc";
        case VCDHD:
            return "Versatile Compact Disc High Density";
        case SVOD:
            return "Stacked Volumetric Optical Disc";
        case FDDVD:
            return "Five Dimensional Disc";

        // LaserDisc, types 80 to 89
        case LD:
            return "LaserDisc";
        case LDROM:
            return "LaserDisc Data";
        case LDROM2:
            return "LaserDisc ROM 2";
        case LVROM:
            return "LVROM";
        case MegaLD:
            return "MegaLD";

        // MiniDisc, types 90 to 99
        case HiMD:
            return "Hi-MD";
        case MD:
            return "MiniDisc";
        case MDData:
            return "MiniDisc DATA";
        case MDData2:
            return "MiniDisc DATA 2";

        // Plasmon UDO, types 100 to 109
        case UDO:
            return "Ultra Density Optical";
        case UDO2:
            return "Ultra Density Optical 2";
        case UDO2_WORM:
            return "Ultra Density Optical 2 WORM";

        // Sony game media, types 110 to 129
        case PlayStationMemoryCard:
            return "PlayStation Memory Card";
        case PlayStationMemoryCard2:
            return "PlayStation 2 Memory Card";
        case PS1CD:
            return "PlayStation Game CD";
        case PS2CD:
            return "PlayStation 2 Game CD";
        case PS2DVD:
            return "PlayStation 2 Game DVD";
        case PS3DVD:
            return "PlayStation 3 Game DVD";
        case PS3BD:
            return "PlayStation 3 Game Blu-ray";
        case PS4BD:
            return "PlayStation 4 Game Blu-ray";
        case UMD:
            return "Universal Media Disc";
        case PlayStationVitaGameCard:
            return "PS Vita Game Card";

        // Microsoft game media, types 130 to 149
        case XGD:
            return "Xbox Game Disc";
        case XGD2:
            return "Xbox 360 Game Disc";
        case XGD3:
            return "Xbox 360 Game Disc v3";
        case XGD4:
            return "Xbox One Game Disc";

        // Sega game media, types 150 to 169
        case MEGACD:
            return "Mega CD";
        case SATURNCD:
            return "Saturn CD";
        case GDROM:
            return "GD-ROM";
        case GDR:
            return "GD-R";
        case SegaCard:
            return "Sega Card";
        case MilCD:
            return "MIL-CD";

        // Other game media, types 170 to 179
        case HuCard:
            return "HuCard";
        case SuperCDROM2:
            return "Super CD-ROM²";
        case JaguarCD:
            return "Jaguar CD";
        case ThreeDO:
            return "3DO CD";
        case PCFX:
            return "PC-FX";
        case NeoGeoCD:
            return "Neo Geo CD";
        case CDTV:
            return "CDTV";
        case CD32:
            return "CD32";
        case Nuon:
            return "Nuon";
        case Playdia:
            return "Playdia";

        // Apple floppies, types 180 to 189
        case Apple32SS:
            return "Apple 5.25\" SS DD 13 spt";
        case Apple32DS:
            return "Apple 5.25\" DS DD 13 spt";
        case Apple33SS:
            return "Apple 5.25\" SS DD 16 spt";
        case Apple33DS:
            return "Apple 5.25\" DS DD 16 spt";
        case AppleSonySS:
            return "Apple 3.5\" SS DD GCR";
        case AppleSonyDS:
            return "Apple 3.5\" DS DD GCR";
        case AppleFileWare:
            return "Apple FileWare (Twiggy)";

        // PC floppies, types 190 to 209
        case DOS_525_SS_DD_8:
            return "5.25\" SS DD 8 spt";
        case DOS_525_SS_DD_9:
            return "5.25\" SS DD 9 spt";
        case DOS_525_DS_DD_8:
            return "5.25\" DS DD 8 spt";
        case DOS_525_DS_DD_9:
            return "5.25\" DS DD 9 spt";
        case DOS_525_HD:
            return "5.25\" DS HD";
        case DOS_35_SS_DD_8:
            return "3.5\" SS DD 8 spt";
        case DOS_35_SS_DD_9:
            return "3.5\" SS DD 9 spt";
        case DOS_35_DS_DD_8:
            return "3.5\" DS DD 8 spt";
        case DOS_35_DS_DD_9:
            return "3.5\" DS DD 9 spt";
        case DOS_35_HD:
            return "3.5\" DS HD";
        case DOS_35_ED:
            return "3.5\" DS ED";
        case DMF:
            return "DMF (3.5\" 21 spt)";
        case DMF_82:
            return "DMF 82 tracks";
        case XDF_525:
            return "XDF 5.25\"";
        case XDF_35:
            return "XDF 3.5\"";

        // IBM floppies, types 210 to 219
        case IBM23FD:
            return "IBM 23FD";
        case IBM33FD_128:
            return "IBM 33FD 128 bps";
        case IBM33FD_256:
            return "IBM 33FD 256 bps";
        case IBM33FD_512:
            return "IBM 33FD 512 bps";
        case IBM43FD_128:
            return "IBM 43FD 128 bps";
        case IBM43FD_256:
            return "IBM 43FD 256 bps";
        case IBM53FD_256:
            return "IBM 53FD 256 bps";
        case IBM53FD_512:
            return "IBM 53FD 512 bps";
        case IBM53FD_1024:
            return "IBM 53FD 1024 bps";

        // DEC floppies, types 220 to 229
        case RX01:
            return "RX01";
        case RX02:
            return "RX02";
        case RX03:
            return "RX03";
        case RX50:
            return "RX50";

        // Acorn floppies, types 230 to 239
        case ACORN_525_SS_SD_40:
            return "Acorn 5.25\" SS SD 40 tracks";
        case ACORN_525_SS_SD_80:
            return "Acorn 5.25\" SS SD 80 tracks";
        case ACORN_525_SS_DD_40:
            return "Acorn 5.25\" SS DD 40 tracks";
        case ACORN_525_SS_DD_80:
            return "Acorn 5.25\" SS DD 80 tracks";
        case ACORN_525_DS_DD:
            return "Acorn 5.25\" DS DD";
        case ACORN_35_DS_DD:
            return "Acorn 3.5\" DS DD";
        case ACORN_35_DS_HD:
            return "Acorn 3.5\" DS HD";

        // Atari floppies, types 240 to 249
        case ATARI_525_SD:
            return "Atari 5.25\" SD";
        case ATARI_525_ED:
            return "Atari 5.25\" ED";
        case ATARI_525_DD:
            return "Atari 5.25\" DD";
        case ATARI_35_SS_DD:
            return "Atari 3.5\" SS DD";
        case ATARI_35_DS_DD:
            return "Atari 3.5\" DS DD";
        case ATARI_35_SS_DD_11:
            return "Atari 3.5\" SS DD 11 spt";
        case ATARI_35_DS_DD_11:
            return "Atari 3.5\" DS DD 11 spt";

        // Commodore floppies, types 250 to 259
        case CBM_35_DD:
            return "Commodore 3.5\" DD (1581)";
        case CBM_AMIGA_35_DD:
            return "Amiga 3.5\" DD";
        case CBM_AMIGA_35_HD:
            return "Amiga 3.5\" HD";
        case CBM_1540:
            return "Commodore 1540/1541";
        case CBM_1540_Ext:
            return "Commodore 1540 Extended";
        case CBM_1571:
            return "Commodore 1571";

        // NEC/SHARP floppies, types 260 to 269
        case NEC_8_SD:
            return "NEC 8\" SD";
        case NEC_8_DD:
            return "NEC 8\" DD";
        case NEC_525_SS:
            return "NEC 5.25\" SS";
        case NEC_525_DS:
            return "NEC 5.25\" DS";
        case NEC_525_HD:
            return "NEC 5.25\" HD";
        case NEC_35_HD_8:
            return "NEC 3.5\" HD 8 spt";
        case NEC_35_HD_15:
            return "NEC 3.5\" HD 15 spt";
        case NEC_35_TD:
            return "NEC 3.5\" TD";
        case SHARP_525_9:
            return "Sharp 5.25\" 9 spt";
        case SHARP_35_9:
            return "Sharp 3.5\" 9 spt";

        // ECMA floppies, types 270 to 289
        case ECMA_99_8:
            return "ECMA-99 8 spt";
        case ECMA_99_15:
            return "ECMA-99 15 spt";
        case ECMA_99_26:
            return "ECMA-99 26 spt";
        case ECMA_54:
            return "ECMA-54";
        case ECMA_59:
            return "ECMA-59";
        case ECMA_66:
            return "ECMA-66";
        case ECMA_69_8:
            return "ECMA-69 8 spt";
        case ECMA_69_15:
            return "ECMA-69 15 spt";
        case ECMA_69_26:
            return "ECMA-69 26 spt";
        case ECMA_70:
            return "ECMA-70";
        case ECMA_78:
            return "ECMA-78";
        case ECMA_78_2:
            return "ECMA-78-2";

        // Non-standard PC formats, types 290 to 308
        case FDFORMAT_525_DD:
            return "FDFORMAT 5.25\" DD";
        case FDFORMAT_525_HD:
            return "FDFORMAT 5.25\" HD";
        case FDFORMAT_35_DD:
            return "FDFORMAT 3.5\" DD";
        case FDFORMAT_35_HD:
            return "FDFORMAT 3.5\" HD";

        // Apricot, type 309
        case Apricot_35:
            return "Apricot 3.5\"";

        // OnStream ADR, types 310 to 319
        case ADR2120:
            return "ADR 2120";
        case ADR260:
            return "ADR 260";
        case ADR30:
            return "ADR 30";
        case ADR50:
            return "ADR 50";

        // AIT, types 320 to 339
        case AIT1:
            return "AIT-1";
        case AIT1Turbo:
            return "AIT-1 Turbo";
        case AIT2:
            return "AIT-2";
        case AIT2Turbo:
            return "AIT-2 Turbo";
        case AIT3:
            return "AIT-3";
        case AIT3Ex:
            return "AIT-3 Ex";
        case AIT3Turbo:
            return "AIT-3 Turbo";
        case AIT4:
            return "AIT-4";
        case AIT5:
            return "AIT-5";
        case AITETurbo:
            return "AITE Turbo";
        case SAIT1:
            return "SAIT-1";
        case SAIT2:
            return "SAIT-2";

        // Iomega, types 340 to 359
        case Bernoulli:
            return "Bernoulli";
        case Bernoulli2:
            return "Bernoulli II";
        case Ditto:
            return "Ditto";
        case DittoMax:
            return "Ditto Max";
        case Jaz:
            return "Jaz";
        case Jaz2:
            return "Jaz 2";
        case PocketZip:
            return "PocketZip";
        case REV120:
            return "REV 120";
        case REV35:
            return "REV 35";
        case REV70:
            return "REV 70";
        case ZIP100:
            return "Zip 100";
        case ZIP250:
            return "Zip 250";
        case ZIP750:
            return "Zip 750";

        // Audio/video, types 360 to 369
        case CompactCassette:
            return "Compact Cassette";
        case Data8:
            return "Data8";
        case MiniDV:
            return "MiniDV";
        case Dcas25:
            return "D/CAS-25";
        case Dcas85:
            return "D/CAS-85";
        case Dcas103:
            return "D/CAS-103";

        // CompactFlash, types 370 to 379
        case CFast:
            return "CFast";
        case CompactFlash:
            return "CompactFlash";
        case CompactFlashType2:
            return "CompactFlash Type II";

        // DAT/DDS, types 380 to 389
        case DigitalAudioTape:
            return "Digital Audio Tape";
        case DAT160:
            return "DAT 160";
        case DAT320:
            return "DAT 320";
        case DAT72:
            return "DAT 72";
        case DDS1:
            return "DDS-1";
        case DDS2:
            return "DDS-2";
        case DDS3:
            return "DDS-3";
        case DDS4:
            return "DDS-4";

        // DEC tapes, types 390 to 399
        case CompactTapeI:
            return "CompactTape I";
        case CompactTapeII:
            return "CompactTape II";
        case DECtapeII:
            return "DECtape II";
        case DLTtapeIII:
            return "DLTtape III";
        case DLTtapeIIIxt:
            return "DLTtape IIIxt";
        case DLTtapeIV:
            return "DLTtape IV";
        case DLTtapeS4:
            return "DLTtape S4";
        case SDLT1:
            return "SDLT-1";
        case SDLT2:
            return "SDLT-2";
        case VStapeI:
            return "VStape I";

        // Exabyte, types 400 to 419
        case Exatape15m:
            return "Exatape 15m";
        case Exatape22m:
            return "Exatape 22m";
        case Exatape22mAME:
            return "Exatape 22m AME";
        case Exatape28m:
            return "Exatape 28m";
        case Exatape40m:
            return "Exatape 40m";
        case Exatape45m:
            return "Exatape 45m";
        case Exatape54m:
            return "Exatape 54m";
        case Exatape75m:
            return "Exatape 75m";
        case Exatape76m:
            return "Exatape 76m";
        case Exatape80m:
            return "Exatape 80m";
        case Exatape106m:
            return "Exatape 106m";
        case Exatape160mXL:
            return "Exatape 160m XL";
        case Exatape112m:
            return "Exatape 112m";
        case Exatape125m:
            return "Exatape 125m";
        case Exatape150m:
            return "Exatape 150m";
        case Exatape170m:
            return "Exatape 170m";
        case Exatape225m:
            return "Exatape 225m";

        // PCMCIA/ExpressCard, types 420 to 429
        case ExpressCard34:
            return "ExpressCard/34";
        case ExpressCard54:
            return "ExpressCard/54";
        case PCCardTypeI:
            return "PC Card Type I";
        case PCCardTypeII:
            return "PC Card Type II";
        case PCCardTypeIII:
            return "PC Card Type III";
        case PCCardTypeIV:
            return "PC Card Type IV";

        // SyQuest, types 430 to 449
        case EZ135:
            return "EZ 135";
        case EZ230:
            return "EZ 230";
        case Quest:
            return "Quest";
        case SparQ:
            return "SparQ";
        case SQ100:
            return "SyQuest 100";
        case SQ200:
            return "SyQuest 200";
        case SQ300:
            return "SyQuest 300";
        case SQ310:
            return "SyQuest 310";
        case SQ327:
            return "SyQuest 327";
        case SQ400:
            return "SyQuest 400";
        case SQ800:
            return "SyQuest 800";
        case SQ1500:
            return "SyQuest 1500";
        case SQ2000:
            return "SyQuest 2000";
        case SyJet:
            return "SyJet";

        // Nintendo, types 450 to 469
        case FamicomGamePak:
            return "Famicom Cartridge";
        case GameBoyAdvanceGamePak:
            return "Game Boy Advance Cartridge";
        case GameBoyGamePak:
            return "Game Boy Cartridge";
        case GOD:
            return "GameCube Optical Disc";
        case N64DD:
            return "Nintendo 64DD";
        case N64GamePak:
            return "Nintendo 64 Cartridge";
        case NESGamePak:
            return "NES Cartridge";
        case Nintendo3DSGameCard:
            return "Nintendo 3DS Game Card";
        case NintendoDiskCard:
            return "Famicom Disk System";
        case NintendoDSGameCard:
            return "Nintendo DS Game Card";
        case NintendoDSiGameCard:
            return "Nintendo DSi Game Card";
        case SNESGamePak:
            return "SNES Cartridge";
        case SNESGamePakUS:
            return "SNES Cartridge (US)";
        case WOD:
            return "Wii Optical Disc";
        case WUOD:
            return "Wii U Optical Disc";
        case SwitchGameCard:
            return "Switch Game Card";

        // IBM tapes, types 470 to 479
        case IBM3470:
            return "IBM 3470";
        case IBM3480:
            return "IBM 3480";
        case IBM3490:
            return "IBM 3490";
        case IBM3490E:
            return "IBM 3490E";
        case IBM3592:
            return "IBM 3592";

        // LTO, types 480 to 509
        case LTO:
            return "LTO Ultrium";
        case LTO2:
            return "LTO-2";
        case LTO3:
            return "LTO-3";
        case LTO3WORM:
            return "LTO-3 WORM";
        case LTO4:
            return "LTO-4";
        case LTO4WORM:
            return "LTO-4 WORM";
        case LTO5:
            return "LTO-5";
        case LTO5WORM:
            return "LTO-5 WORM";
        case LTO6:
            return "LTO-6";
        case LTO6WORM:
            return "LTO-6 WORM";
        case LTO7:
            return "LTO-7";
        case LTO7WORM:
            return "LTO-7 WORM";

        // MemoryStick, types 510 to 519
        case MemoryStick:
            return "Memory Stick";
        case MemoryStickDuo:
            return "Memory Stick Duo";
        case MemoryStickMicro:
            return "Memory Stick Micro";
        case MemoryStickPro:
            return "Memory Stick PRO";
        case MemoryStickProDuo:
            return "Memory Stick PRO Duo";

        // SecureDigital, types 520 to 529
        case microSD:
            return "microSD";
        case miniSD:
            return "miniSD";
        case SecureDigital:
            return "Secure Digital";

        // MultiMediaCard, types 530 to 539
        case MMC:
            return "MultiMediaCard";
        case MMCmicro:
            return "MMCmicro";
        case RSMMC:
            return "RS-MMC";
        case MMCplus:
            return "MMCplus";
        case MMCmobile:
            return "MMCmobile";

        // SLR, types 540 to 569
        case MLR1:
            return "MLR1";
        case MLR1SL:
            return "MLR1 SL";
        case MLR3:
            return "MLR3";
        case SLR1:
            return "SLR1";
        case SLR2:
            return "SLR2";
        case SLR3:
            return "SLR3";
        case SLR32:
            return "SLR32";
        case SLR32SL:
            return "SLR32 SL";
        case SLR4:
            return "SLR4";
        case SLR5:
            return "SLR5";
        case SLR5SL:
            return "SLR5 SL";
        case SLR6:
            return "SLR6";
        case SLRtape7:
            return "SLRtape 7";
        case SLRtape7SL:
            return "SLRtape 7 SL";
        case SLRtape24:
            return "SLRtape 24";
        case SLRtape24SL:
            return "SLRtape 24 SL";
        case SLRtape40:
            return "SLRtape 40";
        case SLRtape50:
            return "SLRtape 50";
        case SLRtape60:
            return "SLRtape 60";
        case SLRtape75:
            return "SLRtape 75";
        case SLRtape100:
            return "SLRtape 100";
        case SLRtape140:
            return "SLRtape 140";

        // QIC, types 570 to 589
        case QIC11:
            return "QIC-11";
        case QIC120:
            return "QIC-120";
        case QIC1350:
            return "QIC-1350";
        case QIC150:
            return "QIC-150";
        case QIC24:
            return "QIC-24";
        case QIC3010:
            return "QIC-3010";
        case QIC3020:
            return "QIC-3020";
        case QIC3080:
            return "QIC-3080";
        case QIC3095:
            return "QIC-3095";
        case QIC320:
            return "QIC-320";
        case QIC40:
            return "QIC-40";
        case QIC525:
            return "QIC-525";
        case QIC80:
            return "QIC-80";

        // StorageTek, types 590 to 609
        case STK4480:
            return "STK 4480";
        case STK4490:
            return "STK 4490";
        case STK9490:
            return "STK 9490";
        case T9840A:
            return "T9840A";
        case T9840B:
            return "T9840B";
        case T9840C:
            return "T9840C";
        case T9840D:
            return "T9840D";
        case T9940A:
            return "T9940A";
        case T9940B:
            return "T9940B";
        case T10000A:
            return "T10000A";
        case T10000B:
            return "T10000B";
        case T10000C:
            return "T10000C";
        case T10000D:
            return "T10000D";

        // Travan, types 610 to 619
        case Travan1:
            return "Travan";
        case Travan1Ex:
            return "Travan-1 Ex";
        case Travan3:
            return "Travan-3";
        case Travan3Ex:
            return "Travan-3 Ex";
        case Travan4:
            return "Travan-4";
        case Travan5:
            return "Travan-5";
        case Travan7:
            return "Travan-7";

        // VXA, types 620 to 629
        case VXA1:
            return "VXA-1";
        case VXA2:
            return "VXA-2";
        case VXA3:
            return "VXA-3";

        // Magneto-optical, types 630 to 659
        case ECMA_153:
            return "ECMA-153 (M.O.)";
        case ECMA_153_512:
            return "ECMA-153 512 bps (M.O.)";
        case ECMA_154:
            return "ECMA-154 (M.O.)";
        case ECMA_183_512:
            return "ECMA-183 512 bps (M.O.)";
        case ECMA_183:
            return "ECMA-183 (M.O.)";
        case ECMA_184_512:
            return "ECMA-184 512 bps (M.O.)";
        case ECMA_184:
            return "ECMA-184 (M.O.)";
        case ECMA_189:
            return "ECMA-189 (M.O.)";
        case ECMA_190:
            return "ECMA-190 (M.O.)";
        case ECMA_195:
            return "ECMA-195 (M.O.)";
        case ECMA_195_512:
            return "ECMA-195 512 bps (M.O.)";
        case ECMA_201:
            return "ECMA-201 (M.O.)";
        case ECMA_201_ROM:
            return "ECMA-201 ROM (M.O.)";
        case ECMA_223:
            return "ECMA-223 (M.O.)";
        case ECMA_223_512:
            return "ECMA-223 512 bps (M.O.)";
        case ECMA_238:
            return "ECMA-238 (M.O.)";
        case ECMA_239:
            return "ECMA-239 (M.O.)";
        case ECMA_260:
            return "ECMA-260 (M.O.)";
        case ECMA_260_Double:
            return "ECMA-260 Double (M.O.)";
        case ECMA_280:
            return "ECMA-280 (M.O.)";
        case ECMA_317:
            return "ECMA-317 (M.O.)";
        case ECMA_322:
            return "ECMA-322 (M.O.)";
        case ECMA_322_2k:
            return "ECMA-322 2k (M.O.)";
        case GigaMo:
            return "GigaMo";
        case GigaMo2:
            return "GigaMo 2";

        // Other floppies, types 660 to 689
        case CompactFloppy:
            return "Compact Floppy";
        case DemiDiskette:
            return "Demi Diskette";
        case Floptical:
            return "Floptical";
        case HiFD:
            return "HiFD";
        case QuickDisk:
            return "QuickDisk";
        case UHD144:
            return "UHD 144";
        case VideoFloppy:
            return "Video Floppy";
        case Wafer:
            return "Wafer";
        case ZXMicrodrive:
            return "ZX Microdrive";

        // Miscellaneous, types 670 to 689
        case BeeCard:
            return "BeeCard";
        case Borsu:
            return "Borsu";
        case DataStore:
            return "DataStore";
        case DIR:
            return "DIR";
        case DST:
            return "DST";
        case DTF:
            return "DTF";
        case DTF2:
            return "DTF2";
        case Flextra3020:
            return "Flextra 3020";
        case Flextra3225:
            return "Flextra 3225";
        case HiTC1:
            return "HiTC1";
        case HiTC2:
            return "HiTC2";
        case LT1:
            return "LT-1";
        case MiniCard:
            return "MiniCard";
        case Orb:
            return "Orb";
        case Orb5:
            return "Orb 5";
        case SmartMedia:
            return "SmartMedia";
        case xD:
            return "xD-Picture Card";
        case XQD:
            return "XQD";
        case DataPlay:
            return "DataPlay";

        // Apple media, types 690 to 699
        case AppleProfile:
            return "Apple Profile";
        case AppleWidget:
            return "Apple Widget";
        case AppleHD20:
            return "Apple HD20";
        case PriamDataTower:
            return "Priam DataTower";
        case Pippin:
            return "Pippin";

        // DEC hard disks, types 700 to 729
        case RA60:
            return "RA60";
        case RA80:
            return "RA80";
        case RA81:
            return "RA81";
        case RC25:
            return "RC25";
        case RD31:
            return "RD31";
        case RD32:
            return "RD32";
        case RD51:
            return "RD51";
        case RD52:
            return "RD52";
        case RD53:
            return "RD53";
        case RD54:
            return "RD54";
        case RK06:
            return "RK06";
        case RK06_18:
            return "RK06 18-bit";
        case RK07:
            return "RK07";
        case RK07_18:
            return "RK07 18-bit";
        case RM02:
            return "RM02";
        case RM03:
            return "RM03";
        case RM05:
            return "RM05";
        case RP02:
            return "RP02";
        case RP02_18:
            return "RP02 18-bit";
        case RP03:
            return "RP03";
        case RP03_18:
            return "RP03 18-bit";
        case RP04:
            return "RP04";
        case RP04_18:
            return "RP04 18-bit";
        case RP05:
            return "RP05";
        case RP05_18:
            return "RP05 18-bit";
        case RP06:
            return "RP06";
        case RP06_18:
            return "RP06 18-bit";

        // Imation, types 730 to 739
        case LS120:
            return "LS-120 SuperDisk";
        case LS240:
            return "LS-240 SuperDisk";
        case FD32MB:
            return "32MB Floppy";
        case RDX:
            return "RDX";
        case RDX320:
            return "RDX 320";

        // VideoNow, types 740 to 749
        case VideoNow:
            return "VideoNow";
        case VideoNowColor:
            return "VideoNow Color";
        case VideoNowXp:
            return "VideoNow XP";

        // Iomega, types 750 to 759
        case Bernoulli10:
            return "Bernoulli Box (10Mb)";
        case Bernoulli20:
            return "Bernoulli Box (20Mb)";
        case BernoulliBox2_20:
            return "Bernoulli Box II (20Mb)";

        // Kodak, types 760 to 769
        case KodakVerbatim3:
            return "Kodak/Verbatim (3Mb)";
        case KodakVerbatim6:
            return "Kodak/Verbatim (6Mb)";
        case KodakVerbatim12:
            return "Kodak/Verbatim (12Mb)";

        // Sony and Panasonic Blu-ray derived, types 770 to 799
        case ProfessionalDisc:
            return "Professional Disc for video";
        case ProfessionalDiscDual:
            return "Professional Disc for video";
        case ProfessionalDiscTriple:
            return "Professional Disc for video";
        case ProfessionalDiscQuad:
            return "Professional Disc for video";
        case PDD:
            return "Professional Disc for DATA";
        case PDD_WORM:
            return "Professional Disc for DATA";
        case ArchivalDisc:
            return "Archival Disc";
        case ArchivalDisc2:
            return "Archival Disc";
        case ArchivalDisc3:
            return "Archival Disc";
        case ODC300R:
            return "Optical Disc archive";
        case ODC300RE:
            return "Optical Disc archive";
        case ODC600R:
            return "Optical Disc archive";
        case ODC600RE:
            return "Optical Disc archive";
        case ODC1200RE:
            return "Optical Disc archive";
        case ODC1500R:
            return "Optical Disc archive";
        case ODC3300R:
            return "Optical Disc archive";
        case ODC5500R:
            return "Optical Disc archive";

        // Magneto-optical, types 800 to 819
        case ECMA_322_1k:
            return "5,25\", M.O., 4383356 sectors, 1024 bytes/sector, ECMA-322, ISO 22092, 9.1Gb/cart";
        case ECMA_322_512:
            return "5,25\", M.O., ??????? sectors, 512 bytes/sector, ECMA-322, ISO 22092, 9.1Gb/cart";
        case ISO_14517:
            return "5,25\", M.O., 1273011 sectors, 1024 bytes/sector, ISO 14517, 2.6Gb/cart";
        case ISO_14517_512:
            return "5,25\", M.O., 2244958 sectors, 512 bytes/sector, ISO 14517, 2.3Gb/cart";
        case ISO_15041_512:
            return "3,5\", M.O., 1041500 sectors, 512 bytes/sector, ISO 15041, 540Mb/cart";
        case HSM650:
            return "Sony HyperStorage";

        // More floppy formats, types 820 to deprecated
        case MetaFloppy_Mod_I:
            return "5.25\", SS, DD, 35 tracks, 16 spt, 256 bytes/sector, MFM, 48 tpi, ???rpm";
        case HF12:
            return "HyperFlex (12Mb)";
        case HF24:
            return "HyperFlex (24Mb)";

        case AtariLynxCard:
            return "Atari Lynx card";
        case AtariJaguarCartridge:
            return "Atari Jaguar cartridge";
        default:
            return "Unknown Media Type";
    }
}

const char *sector_tag_type_to_string(int32_t type)
{
    switch(type)
    {
        case kSectorTagAppleSony:
            return "Apple Sony Tag";
        case kSectorTagCdSync:
            return "CD Sector Sync";
        case kSectorTagCdHeader:
            return "CD Sector Header";
        case kSectorTagCdSubHeader:
            return "CD Sector Sub-Header";
        case kSectorTagCdEdc:
            return "CD Sector EDC";
        case kSectorTagCdEccP:
            return "CD Sector ECC P";
        case kSectorTagCdEccQ:
            return "CD Sector ECC Q";
        case kSectorTagCdEcc:
            return "CD Sector ECC (P+Q)";
        case kSectorTagCdSubchannel:
            return "CD Sector Subchannel";
        case kSectorTagCdTrackIsrc:
            return "CD Track ISRC";
        case kSectorTagCdTrackText:
            return "CD Track Text";
        case kSectorTagCdTrackFlags:
            return "CD Track Flags";
        case kSectorTagDvdCmi:
            return "DVD Copyright Management Information";
        case kSectorTagFloppyAddressMark:
            return "Floppy Address Mark";
        case kSectorTagDvdTitleKey:
            return "DVD Sector Title Key";
        case kSectorTagDvdTitleKeyDecrypted:
            return "DVD Title Key (Decrypted)";
        case kSectorTagDvdSectorInformation:
            return "DVD Sector Information";
        case kSectorTagDvdSectorNumber:
            return "DVD Sector Number";
        case kSectorTagDvdSectorIed:
            return "DVD Sector IED";
        case kSectorTagDvdSectorEdc:
            return "DVD Sector EDC";
        case kSectorTagAppleProfile:
            return "Apple Profile Tag";
        case kSectorTagPriamDataTower:
            return "Priam DataTower Tag";
        case kSectorTagBdSectorEdc:
            return "Blu-ray Sector EDC";
        default:
            return "Unknown Sector Tag";
    }
}
