#pragma clang diagnostic push
#pragma ide diagnostic   ignored "OCUnusedGlobalDeclarationInspection"
#ifndef LIBAARUFORMAT_ENUMS_H
#define LIBAARUFORMAT_ENUMS_H

/** List of known compression types */
typedef enum
{
    /** Not compressed */
    None                           = 0, /** LZMA */
    Lzma                           = 1, /** FLAC */
    Flac                           = 2, /** LZMA in Claunia Subchannel Transform processed data */
    LzmaClauniaSubchannelTransform = 3
} CompressionType;

/** List of known data types */
typedef enum
{
    /** No data */
    NoData = 0,
    /** User data */
    UserData = 1,
    /** CompactDisc partial Table of Contents */
    CompactDiscPartialToc = 2,
    /** CompactDisc session information */
    CompactDiscSessionInfo = 3,
    /** CompactDisc Table of Contents */
    CompactDiscToc = 4,
    /** CompactDisc Power Management Area */
    CompactDiscPma = 5,
    /** CompactDisc Absolute Time In Pregroove */
    CompactDiscAtip = 6,
    /** CompactDisc Lead-in's CD-Text */
    CompactDiscLeadInCdText = 7,
    /** DVD Physical Format Information */
    DvdPfi = 8,
    /** DVD Lead-in's Copyright Management Information */
    DvdLeadInCmi = 9,
    /** DVD Disc Key */
    DvdDiscKey = 10,
    /** DVD Burst Cutting Area */
    DvdBca = 11,
    /** DVD DMI */
    DvdDmi = 12,
    /** DVD Media Identifier */
    DvdMediaIdentifier = 13,
    /** DVD Media Key Block */
    DvdMediaKeyBlock = 14,
    /** DVD-RAM Disc Definition Structure */
    DvdRamDds = 15,
    /** DVD-RAM Medium Status */
    DvdRamMediumStatus = 16,
    /** DVD-RAM Spare Area Information */
    DvdRamSpareArea = 17,
    /** DVD-R RMD */
    DvdRRmd = 18,
    /** DVD-R Pre-recorded Information */
    DvdRPrerecordedInfo = 19,
    /** DVD-R Media Identifier */
    DvdRMediaIdentifier = 20,
    /** DVD-R Physical Format Information */
    DvdRPfi = 21,
    /** DVD ADress In Pregroove */
    DvdAdip = 22,
    /** HD DVD Copy Protection Information */
    HdDvdCpi = 23,
    /** HD DVD Medium Status */
    HdDvdMediumStatus = 24,
    /** DVD DL Layer Capacity */
    DvdDlLayerCapacity = 25,
    /** DVD DL Middle Zone Address */
    DvdDlMiddleZoneAddress = 26,
    /** DVD DL Jump Interval Size */
    DvdDlJumpIntervalSize = 27,
    /** DVD DL Manual Layer Jump LBA */
    DvdDlManualLayerJumpLba = 28,
    /** Bluray Disc Information */
    BlurayDi = 29,
    /** Bluray Burst Cutting Area */
    BlurayBca = 30,
    /** Bluray Disc Definition Structure */
    BlurayDds = 31,
    /** Bluray Cartridge Status */
    BlurayCartridgeStatus = 32,
    /** Bluray Spare Area Information */
    BluraySpareArea = 33,
    /** AACS Volume Identifier */
    AacsVolumeIdentifier = 34,
    /** AACS Serial Number */
    AacsSerialNumber = 35,
    /** AACS Media Identifier */
    AacsMediaIdentifier = 36,
    /** AACS Media Key Block */
    AacsMediaKeyBlock = 37,
    /** AACS Data Keys */
    AacsDataKeys = 38,
    /** AACS LBA Extents */
    AacsLbaExtents = 39,
    /** CPRM Media Key Block */
    CprmMediaKeyBlock = 40,
    /** Recognized Layers */
    HybridRecognizedLayers = 41,
    /** MMC Write Protection */
    ScsiMmcWriteProtection = 42,
    /** MMC Disc Information */
    ScsiMmcDiscInformation = 43,
    /** MMC Track Resources Information */
    ScsiMmcTrackResourcesInformation = 44,
    /** MMC POW Resources Information */
    ScsiMmcPowResourcesInformation = 45,
    /** SCSI INQUIRY RESPONSE */
    ScsiInquiry = 46,
    /** SCSI MODE PAGE 2Ah */
    ScsiModePage2A = 47,
    /** ATA IDENTIFY response */
    AtaIdentify = 48,
    /** ATAPI IDENTIFY response */
    AtapiIdentify = 49,
    /** PCMCIA CIS */
    PcmciaCis = 50,
    /** SecureDigital CID */
    SecureDigitalCid = 51,
    /** SecureDigital CSD */
    SecureDigitalCsd = 52,
    /** SecureDigital SCR */
    SecureDigitalScr = 53,
    /** SecureDigital OCR */
    SecureDigitalOcr = 54,
    /** MultiMediaCard CID */
    MultiMediaCardCid = 55,
    /** MultiMediaCard CSD */
    MultiMediaCardCsd = 56,
    /** MultiMediaCard OCR */
    MultiMediaCardOcr = 57,
    /** MultiMediaCard Extended CSD */
    MultiMediaCardExtendedCsd = 58,
    /** Xbox Security Sector */
    XboxSecuritySector = 59,
    /** Floppy Lead-out */
    FloppyLeadOut = 60,
    /** Dvd Disc Control Block */
    DvdDiscControlBlock = 61,
    /** CompactDisc First track pregap */
    CompactDiscFirstTrackPregap = 62,
    /** CompactDisc Lead-out */
    CompactDiscLeadOut = 63,
    /** SCSI MODE SENSE (6) response */
    ScsiModeSense6 = 64,
    /** SCSI MODE SENSE (10) response */
    ScsiModeSense10 = 65,
    /** USB descriptors */
    UsbDescriptors = 66,
    /** Xbox DMI */
    XboxDmi = 67,
    /** Xbox Physical Format Information */
    XboxPfi = 68,
    /** CompactDisc sector prefix (sync, header */
    CdSectorPrefix = 69,
    /** CompactDisc sector suffix (edc, ecc p, ecc q) */
    CdSectorSuffix = 70,
    /** CompactDisc subchannel */
    CdSectorSubchannel = 71,
    /** Apple Profile (20 byte) tag */
    AppleProfileTag = 72,
    /** Apple Sony (12 byte) tag */
    AppleSonyTag = 73,
    /** Priam Data Tower (24 byte) tag */
    PriamDataTowerTag = 74,
    /** CompactDisc Media Catalogue Number (as in Lead-in), 13 bytes, ASCII */
    CompactDiscMediaCatalogueNumber = 75,
    /** CompactDisc sector prefix (sync, header), only incorrect stored */
    CdSectorPrefixCorrected = 76,
    /** CompactDisc sector suffix (edc, ecc p, ecc q), only incorrect stored */
    CdSectorSuffixCorrected = 77,
    /** CompactDisc MODE 2 subheader */
    CompactDiscMode2Subheader = 78,
    /** CompactDisc Lead-in */
    CompactDiscLeadIn = 79
} DataType;

/** List of known blocks types */
typedef enum
{
    /** Block containing data */
    DataBlock = 0x4B4C4244,
    /** Block containing a deduplication table */
    DeDuplicationTable = 0X2A544444,
    /** Block containing the index */
    IndexBlock = 0X58444E49,
    /** Block containing logical geometry */
    GeometryBlock = 0x4D4F4547,
    /** Block containing metadata */
    MetadataBlock = 0x4154454D,
    /** Block containing optical disc tracks */
    TracksBlock = 0x534B5254,
    /** Block containing CICM XML metadata */
    CicmBlock = 0x4D434943,
    /** Block containing contents checksums */
    ChecksumBlock = 0x4D534B43,
    /** TODO: Block containing data position measurements */
    DataPositionMeasurementBlock = 0x2A4D5044,
    /** TODO: Block containing a snapshot index */
    SnapshotBlock = 0x50414E53,
    /** TODO: Block containing how to locate the parent image */
    ParentBlock = 0x50524E54,
    /** Block containing an array of hardware used to create the image */
    DumpHardwareBlock = 0x2A504D44,
    /** TODO: Block containing list of files for a tape image */
    TapeFileBlock = 0x454C4654
} BlockType;

typedef enum
{
    Invalid = 0,
    Md5     = 1,
    Sha1    = 2,
    Sha256  = 3,
    SpamSum = 4
} ChecksumAlgorithm;

typedef enum
{
    NotDumped       = 0x10000000,
    Correct         = 0x20000000,
    Mode2Form1Ok    = 0x30000000,
    Mode2Form2Ok    = 0x40000000,
    Mode2Form2NoCrc = 0x50000000
} CdFixFlags;

/** Track (as partitioning element) types. */
typedef enum
{
    /** Audio track */
    Audio           = 0, /** Data track (not any of the below defined ones) */
    Data            = 1, /** Data track, compact disc mode 1 */
    CdMode1         = 2, /** Data track, compact disc mode 2, formless */
    CdMode2Formless = 3, /** Data track, compact disc mode 2, form 1 */
    CdMode2Form1    = 4, /** Data track, compact disc mode 2, form 2 */
    CdMode2Form2    = 5
} TrackType;

typedef enum
{
    AARUF_STATUS_INVALID_CONTEXT = -1,
} DicformatStatus;

/**
 *     Enumeration of media types defined in CICM metadata
 */
typedef enum
{
    /**
     *     Purely optical discs
     */
    OpticalDisc = 0, /**
            * Media that is physically block-based or abstracted like that

*/
    BlockMedia = 1,  /**
                      *     Media that can be accessed by-byte or by-bit, like chips
                      */
    LinearMedia = 2, /**
                      *     Media that can only store data when it is modulated to audio
                      */
    AudioMedia = 3
} XmlMediaType;

#endif // LIBAARUFORMAT_ENUMS_H

#pragma clang diagnostic pop