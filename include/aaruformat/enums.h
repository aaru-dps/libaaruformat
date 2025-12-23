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

#ifndef LIBAARUFORMAT_ENUMS_H
#define LIBAARUFORMAT_ENUMS_H

#ifndef _MSC_VER
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#endif

/**
 * \enum CompressionType
 * \brief List of known compression types.
 */
typedef enum
{
    None                           = 0,  ///< Not compressed.
    Lzma                           = 1,  ///< LZMA compression.
    Flac                           = 2,  ///< FLAC compression.
    LzmaClauniaSubchannelTransform = 3   ///< LZMA applied to Claunia Subchannel Transform processed data.
} CompressionType;

/**
 * \enum DataType
 * \brief List of known data types stored within an Aaru image.
 */
typedef enum
{
    NoData                           = 0,   ///< No data.
    UserData                         = 1,   ///< User (main) data.
    CompactDiscPartialToc            = 2,   ///< Compact Disc partial Table of Contents.
    CompactDiscSessionInfo           = 3,   ///< Compact Disc session information.
    CompactDiscToc                   = 4,   ///< Compact Disc full Table of Contents.
    CompactDiscPma                   = 5,   ///< Compact Disc Power Management Area (PMA).
    CompactDiscAtip                  = 6,   ///< Compact Disc Absolute Time In Pregroove (ATIP).
    CompactDiscLeadInCdText          = 7,   ///< Compact Disc lead-in CD-Text.
    DvdPfi                           = 8,   ///< DVD Physical Format Information.
    DvdLeadInCmi                     = 9,   ///< DVD lead-in Copyright Management Information (CMI).
    DvdDiscKey                       = 10,  ///< DVD disc key.
    DvdBca                           = 11,  ///< DVD Burst Cutting Area (BCA).
    DvdDmi                           = 12,  ///< DVD Disc Manufacturing Information (DMI).
    DvdMediaIdentifier               = 13,  ///< DVD media identifier.
    DvdMediaKeyBlock                 = 14,  ///< DVD Media Key Block (MKB).
    DvdRamDds                        = 15,  ///< DVD-RAM Disc Definition Structure (DDS).
    DvdRamMediumStatus               = 16,  ///< DVD-RAM medium status.
    DvdRamSpareArea                  = 17,  ///< DVD-RAM spare area information.
    DvdRRmd                          = 18,  ///< DVD-R RMD (Recording Management Data).
    DvdRPrerecordedInfo              = 19,  ///< DVD-R pre‑recorded information.
    DvdRMediaIdentifier              = 20,  ///< DVD-R media identifier.
    DvdRPfi                          = 21,  ///< DVD-R Physical Format Information.
    DvdAdip                          = 22,  ///< DVD Address In Pregroove (ADIP).
    HdDvdCpi                         = 23,  ///< HD DVD Copy Protection Information (CPI).
    HdDvdMediumStatus                = 24,  ///< HD DVD medium status.
    DvdDlLayerCapacity               = 25,  ///< DVD dual-layer capacity.
    DvdDlMiddleZoneAddress           = 26,  ///< DVD dual-layer middle zone address.
    DvdDlJumpIntervalSize            = 27,  ///< DVD dual-layer jump interval size.
    DvdDlManualLayerJumpLba          = 28,  ///< DVD dual-layer manual layer jump LBA.
    BlurayDi                         = 29,  ///< Blu-ray Disc Information (DI).
    BlurayBca                        = 30,  ///< Blu-ray Burst Cutting Area (BCA).
    BlurayDds                        = 31,  ///< Blu-ray Disc Definition Structure (DDS).
    BlurayCartridgeStatus            = 32,  ///< Blu-ray cartridge status.
    BluraySpareArea                  = 33,  ///< Blu-ray spare area information.
    AacsVolumeIdentifier             = 34,  ///< AACS volume identifier.
    AacsSerialNumber                 = 35,  ///< AACS serial number.
    AacsMediaIdentifier              = 36,  ///< AACS media identifier.
    AacsMediaKeyBlock                = 37,  ///< AACS Media Key Block (MKB).
    AacsDataKeys                     = 38,  ///< AACS data keys.
    AacsLbaExtents                   = 39,  ///< AACS LBA extents.
    CprmMediaKeyBlock                = 40,  ///< CPRM Media Key Block (MKB).
    HybridRecognizedLayers           = 41,  ///< Recognized layers (hybrid media).
    ScsiMmcWriteProtection           = 42,  ///< MMC write-protection data.
    ScsiMmcDiscInformation           = 43,  ///< MMC disc information.
    ScsiMmcTrackResourcesInformation = 44,  ///< MMC track resources information.
    ScsiMmcPowResourcesInformation   = 45,  ///< MMC POW (Persistent Optical Write?) resources information.
    ScsiInquiry                      = 46,  ///< SCSI INQUIRY response.
    ScsiModePage2A                   = 47,  ///< SCSI MODE PAGE 2Ah.
    AtaIdentify                      = 48,  ///< ATA IDENTIFY DEVICE data.
    AtapiIdentify                    = 49,  ///< ATAPI IDENTIFY PACKET DEVICE data.
    PcmciaCis                        = 50,  ///< PCMCIA Card Information Structure (CIS).
    SecureDigitalCid                 = 51,  ///< Secure Digital CID register.
    SecureDigitalCsd                 = 52,  ///< Secure Digital CSD register.
    SecureDigitalScr                 = 53,  ///< Secure Digital SCR register.
    SecureDigitalOcr                 = 54,  ///< Secure Digital OCR register.
    MultiMediaCardCid                = 55,  ///< MultiMediaCard CID register.
    MultiMediaCardCsd                = 56,  ///< MultiMediaCard CSD register.
    MultiMediaCardOcr                = 57,  ///< MultiMediaCard OCR register.
    MultiMediaCardExtendedCsd        = 58,  ///< MultiMediaCard Extended CSD register.
    XboxSecuritySector               = 59,  ///< Xbox Security Sector.
    FloppyLeadOut                    = 60,  ///< Floppy lead‑out data.
    DvdDiscControlBlock              = 61,  ///< DVD Disc Control Block.
    CompactDiscFirstTrackPregap      = 62,  ///< Compact Disc first track pre-gap.
    CompactDiscLeadOut               = 63,  ///< Compact Disc lead‑out.
    ScsiModeSense6                   = 64,  ///< SCSI MODE SENSE (6) response.
    ScsiModeSense10                  = 65,  ///< SCSI MODE SENSE (10) response.
    UsbDescriptors                   = 66,  ///< USB descriptors set.
    XboxDmi                          = 67,  ///< Xbox DMI.
    XboxPfi                          = 68,  ///< Xbox Physical Format Information (PFI).
    CdSectorPrefix                   = 69,  ///< Compact Disc sector prefix (sync, header).
    CdSectorSuffix                   = 70,  ///< Compact Disc sector suffix (EDC, ECC P, ECC Q).
    CdSectorSubchannel               = 71,  ///< Compact Disc subchannel data.
    AppleProfileTag                  = 72,  ///< Apple Profile (20‑byte) tag.
    AppleSonyTag                     = 73,  ///< Apple Sony (12‑byte) tag.
    PriamDataTowerTag                = 74,  ///< Priam Data Tower (24‑byte) tag.
    CompactDiscMediaCatalogueNumber  = 75,  ///< Compact Disc Media Catalogue Number (lead‑in, 13 ASCII bytes).
    CdSectorPrefixCorrected          = 76,  ///< Compact Disc sector prefix (sync, header) corrected-only stored.
    CdSectorSuffixCorrected          = 77,  ///< Compact Disc sector suffix (EDC, ECC P, ECC Q) corrected-only stored.
    CompactDiscMode2Subheader        = 78,  ///< Compact Disc MODE 2 subheader.
    CompactDiscLeadIn                = 79,  ///< Compact Disc lead‑in.
    DvdDiscKeyDecrypted              = 80,  ///< Decrypted DVD Disc Key
    DvdSectorCprMai                  = 81,  ///< DVD Copyright Management Information (CPR_MAI)
    DvdSectorTitleKeyDecrypted       = 82,  ///< Decrypted DVD Title Key
    DvdSectorId                      = 83,  ///< DVD Identification Data (ID)
    DvdSectorIed                     = 84,  ///< DVD ID Error Detection Code (IED)
    DvdSectorEdc                     = 85,  ///< DVD Error Detection Code (EDC)
    DvdSectorEccPi                   = 86,  ///< DVD Error Correction Code (ECC) Parity of Inner Code (PI)
    DvdEccBlockPo                    = 87,  ///< DVD Error Correction Code (ECC) Parity of Outer Code (PO)
    DvdPfi2ndLayer                   = 88   ///< DVD Physical Format Information for the second layer
} DataType;

/**
 * \enum BlockType
 * \brief List of known block types contained in an Aaru image.
 */
typedef enum
{
    DataBlock                    = 0x4B4C4244,  ///< Block containing data.
    DeDuplicationTable           = 0x2A544444,  ///< Block containing a deduplication table (v1).
    DeDuplicationTable2          = 0x32544444,  ///< Block containing a deduplication table v2.
    DeDuplicationTableSecondary  = 0x53545444,  ///< Block containing a secondary deduplication table (v2).
    IndexBlock                   = 0x58444E49,  ///< Block containing the index (v1).
    IndexBlock2                  = 0x32584449,  ///< Block containing the index v2.
    IndexBlock3                  = 0x33584449,  ///< Block containing the index v3.
    GeometryBlock                = 0x4D4F4547,  ///< Block containing logical geometry.
    MetadataBlock                = 0x4154454D,  ///< Block containing metadata.
    TracksBlock                  = 0x534B5254,  ///< Block containing optical disc tracks.
    CicmBlock                    = 0x4D434943,  ///< Block containing CICM XML metadata.
    ChecksumBlock                = 0x4D534B43,  ///< Block containing contents checksums.
    DataPositionMeasurementBlock = 0x2A4D5044,  ///< Block containing data position measurements (reserved / TODO).
    SnapshotBlock                = 0x50414E53,  ///< Block containing a snapshot index (reserved / TODO).
    ParentBlock                  = 0x50524E54,  ///< Block describing how to locate the parent image (reserved / TODO).
    DumpHardwareBlock            = 0x2A504D44,  ///< Block containing an array of hardware used to create the image.
    TapeFileBlock                = 0x454C4654,  ///< Block containing list of files for a tape image.
    TapePartitionBlock           = 0x54425054,  ///< Block containing list of partitions for a tape image.
    AaruMetadataJsonBlock        = 0x444D534A   ///< Block containing JSON version of Aaru Metadata
} BlockType;

/**
 * \enum ChecksumAlgorithm
 * \brief Supported checksum / hash algorithms.
 */
typedef enum
{
    Invalid = 0,  ///< Invalid / unspecified algorithm.
    Md5     = 1,  ///< MD5 hash.
    Sha1    = 2,  ///< SHA-1 hash.
    Sha256  = 3,  ///< SHA-256 hash.
    SpamSum = 4,  ///< SpamSum (context-triggered piecewise hash).
    Blake3  = 5,  ///< BLAKE3 hash.
} ChecksumAlgorithm;

/**
 * \enum CdFixFlags
 * \brief Flags describing Compact Disc sector fix-up status.
 */
typedef enum
{
    NotDumped       = 0x10000000,  ///< Sector(s) have not yet been dumped.
    Correct         = 0x20000000,  ///< Sector(s) contain valid MODE 1 data with regenerable suffix/prefix.
    Mode2Form1Ok    = 0x30000000,  ///< Sector suffix valid for MODE 2 Form 1; regenerable.
    Mode2Form2Ok    = 0x40000000,  ///< Sector suffix valid for MODE 2 Form 2 with correct CRC.
    Mode2Form2NoCrc = 0x50000000   ///< Sector suffix valid for MODE 2 Form 2 but CRC absent/empty.
} CdFixFlags;

/**
 * \enum TrackType
 * \brief Track (partitioning element) types for optical media.
 */
typedef enum
{
    Audio           = 0,  ///< Audio track.
    Data            = 1,  ///< Generic data track (not further specified).
    CdMode1         = 2,  ///< Compact Disc Mode 1 data track.
    CdMode2Formless = 3,  ///< Compact Disc Mode 2 (formless) data track.
    CdMode2Form1    = 4,  ///< Compact Disc Mode 2 Form 1 data track.
    CdMode2Form2    = 5   ///< Compact Disc Mode 2 Form 2 data track.
} TrackType;

/**
 * \enum AaruformatStatus
 * \brief Status / error codes specific to libaaruformat.
 */
typedef enum
{
    AARUF_STATUS_INVALID_CONTEXT = -1  ///< Provided context/handle is invalid.
} AaruformatStatus;

/**
 * \enum XmlMediaType
 * \brief Enumeration of media types defined in CICM metadata.
 */
typedef enum
{
    OpticalDisc = 0,  ///< Purely optical discs.
    BlockMedia  = 1,  ///< Media that is physically block-based or abstracted like that.
    LinearMedia = 2,  ///< Media that can be accessed by-byte or by-bit, like chips.
    AudioMedia  = 3   ///< Media that can only store data when modulated to audio.
} XmlMediaType;

/**
 * \enum SectorStatus
 * \brief Acquisition / content status for one or more sectors.
 */
typedef enum
{
    SectorStatusNotDumped       = 0x0,  ///< Sector(s) not yet acquired during image dumping.
    SectorStatusDumped          = 0x1,  ///< Sector(s) successfully dumped without error.
    SectorStatusErrored         = 0x2,  ///< Error during dumping; data may be incomplete or corrupt.
    SectorStatusMode1Correct    = 0x3,  ///< Valid MODE 1 data with regenerable suffix/prefix.
    SectorStatusMode2Form1Ok    = 0x4,  ///< Suffix verified/regenerable for MODE 2 Form 1.
    SectorStatusMode2Form2Ok    = 0x5,  ///< Suffix matches MODE 2 Form 2 with valid CRC.
    SectorStatusMode2Form2NoCrc = 0x6,  ///< Suffix matches MODE 2 Form 2 but CRC empty/missing.
    SectorStatusTwin            = 0x7,  ///< Pointer references a twin sector table.
    SectorStatusUnrecorded      = 0x8,  ///< Sector physically unrecorded; repeated reads non-deterministic.
    SectorStatusEncrypted       = 0x9,  ///< Content encrypted and stored encrypted in image.
    SectorStatusUnencrypted     = 0xA   ///< Content originally encrypted but stored decrypted in image.
} SectorStatus;

/**
 * \enum FeaturesCompatible
 * \brief Bit-mask of optional, backward-compatible features stored in an image.
 *
 * These flags advertise additional data structures or capabilities embedded in the
 * image that older readers MAY safely ignore. An unknown bit MUST be treated as
 * "feature unsupported" without failing to open the image. Writers set the bits for
 * features they included; readers test them to enable extended behaviors.
 *
 * Usage example:
 * \code{.c}
 * uint64_t features = header->featuresCompatible; // value read from on-disk header
 * if(features & AARU_FEATURE_RW_BLAKE3)
 * {
 *     // Image contains BLAKE3 checksums; enable BLAKE3 verification path.
 * }
 * \endcode
 *
 * Future compatible features SHALL use the next available bit (1ULL << n).
 */
typedef enum
{
    AARU_FEATURE_RW_BLAKE3 = 0x1,  ///< BLAKE3 checksum is present (read/write support for BLAKE3 hashes).
} FeaturesCompatible;

#ifndef _MSC_VER
#pragma clang diagnostic pop
#endif

#endif  // LIBAARUFORMAT_ENUMS_H
