#pragma endian little

#include <std/core.pat>
#include <std/mem.pat>
#include <type/guid.pat>
#include <std/sys.pat>
#include <std/string.pat>

enum MediaType : u32
{
    Unknown = 0,
    UnknownMO = 1,
    GENERIC_HDD = 2,
    Microdrive  = 3,
    Zone_HDD    = 4,
    FlashDrive = 5,
    UnknownTape = 6,

    CD = 10,
    CDDA = 11,
    CDG = 12,
    CDEG = 13,
    CDI = 14,
    CDROM = 15,
    CDROMXA = 16,
    CDPLUS = 17,
    CDMO = 18,
    CDR = 19,
    CDRW = 20,
    CDMRW = 21,
    VCD = 22,
    SVCD = 23,
    PCD = 24,
    SACD = 25,
    DDCD = 26,
    DDCDR = 27,
    DDCDRW = 28,
    DTSCD = 29,
    CDMIDI = 30,
    CDV = 31,
    PD650 = 32,
    PD650_WORM = 33,
    CDIREADY = 34,
    FMTOWNS = 35,

    DVDROM = 40,
    DVDR = 41,
    DVDRW = 42,
    DVDPR = 43,
    DVDPRW = 44,
    DVDPRWDL = 45,
    DVDRDL = 46,
    DVDPRDL = 47,
    DVDRAM = 48,
    DVDRWDL = 49,
    DVDDownload = 50,

    HDDVDROM = 51,
    HDDVDRAM = 52,
    HDDVDR = 53,
    HDDVDRW = 54,
    HDDVDRDL = 55,
    HDDVDRWDL = 56,

    BDROM = 60,
    BDR = 61,
    BDRE = 62,
    BDRXL = 63,
    BDREXL = 64,

    EVD = 70,
    FVD = 71,
    HVD = 72,
    CBHD = 73,
    HDVMD = 74,
    VCDHD = 75,
    SVOD = 76,
    FDDVD = 77,

    LD = 80,
    LDROM = 81,
    LDROM2 = 82,
    LVROM = 83,
    MegaLD = 84,

    HiMD = 90,
    MD = 91,
    MDData = 92,
    MDData2 = 93,
    MD60 = 94,
    MD74 = 95,
    MD80 = 96,

    UDO = 100,
    UDO2 = 101,
    UDO2_WORM = 102,

    PlayStationMemoryCard = 110,
    PlayStationMemoryCard2 = 111,
    PS1CD = 112,
    PS2CD = 113,
    PS2DVD = 114,
    PS3DVD = 115,
    PS3BD = 116,
    PS4BD = 117,
    UMD = 118,
    PlayStationVitaGameCard = 119,

    XGD = 130,
    XGD2 = 131,
    XGD3 = 132,
    XGD4 = 133,

    MEGACD = 150,
    SATURNCD = 151,
    GDROM = 152,
    GDR = 153,
    SegaCard = 154,
    MilCD = 155,

    HuCard = 170,
    SuperCDROM2 = 171,
    JaguarCD = 172,
    ThreeDO = 173,
    PCFX = 174,
    NeoGeoCD = 175,
    CDTV = 176,
    CD32 = 177,
    Nuon = 178,
    Playdia = 179,

    Apple32SS = 180,
    Apple32DS = 181,
    Apple33SS = 182,
    Apple33DS = 183,
    AppleSonySS = 184,
    AppleSonyDS = 185,
    AppleFileWare = 186,

    DOS_525_SS_DD_8 = 190,
    DOS_525_SS_DD_9 = 191,
    DOS_525_DS_DD_8 = 192,
    DOS_525_DS_DD_9 = 193,
    DOS_525_HD = 194,
    DOS_35_SS_DD_8 = 195,
    DOS_35_SS_DD_9 = 196,
    DOS_35_DS_DD_8 = 197,
    DOS_35_DS_DD_9 = 198,
    DOS_35_HD = 199,
    DOS_35_ED = 200,
    DMF = 201,
    DMF_82 = 202,
    XDF_525 = 203,
    XDF_35 = 204,

    IBM23FD = 210,
    IBM33FD_128 = 211,
    IBM33FD_256 = 212,
    IBM33FD_512 = 213,
    IBM43FD_128 = 214,
    IBM43FD_256 = 215,
    IBM53FD_256 = 216,
    IBM53FD_512 = 217,
    IBM53FD_1024 = 218,

    RX01 = 220,
    RX02 = 221,
    RX03 = 222,
    RX50 = 223,

    ACORN_525_SS_SD_40 = 230,
    ACORN_525_SS_SD_80 = 231,
    ACORN_525_SS_DD_40 = 232,
    ACORN_525_SS_DD_80 = 233,
    ACORN_525_DS_DD = 234,
    ACORN_35_DS_DD = 235,
    ACORN_35_DS_HD = 236,

    ATARI_525_SD = 240,
    ATARI_525_ED = 241,
    ATARI_525_DD = 242,
    ATARI_35_SS_DD = 243,
    ATARI_35_DS_DD = 244,
    ATARI_35_SS_DD_11 = 245,
    ATARI_35_DS_DD_11 = 246,

    CBM_35_DD = 250,
    CBM_AMIGA_35_DD = 251,
    CBM_AMIGA_35_HD = 252,
    CBM_1540 = 253,
    CBM_1540_Ext = 254,
    CBM_1571 = 255,

    NEC_8_SD = 260,
    NEC_8_DD = 261,
    NEC_525_SS = 262,
    NEC_525_DS = 263,
    NEC_525_HD = 264,
    NEC_35_HD_8 = 265,
    NEC_35_HD_15 = 266,
    NEC_35_TD = 267,
    SHARP_525 = MediaType::NEC_525_HD,
    SHARP_525_9 = 268,
    SHARP_35 = MediaType::NEC_35_HD_8,
    SHARP_35_9 = 269,

    ECMA_99_8 = 270,
    ECMA_99_15 = 271,
    ECMA_99_26 = 272,
    ECMA_100 = MediaType::DOS_35_DS_DD_9,
    ECMA_125 = MediaType::DOS_35_HD,
    ECMA_147 = MediaType::DOS_35_ED,
    ECMA_54 = 273,
    ECMA_59 = 274,
    ECMA_66 = 275,
    ECMA_69_8 = 276,
    ECMA_69_15 = 277,
    ECMA_69_26 = 278,
    ECMA_70 = 279,
    ECMA_78 = 280,
    ECMA_78_2 = 281,

    FDFORMAT_525_DD = 290,
    FDFORMAT_525_HD = 291,
    FDFORMAT_35_DD = 292,
    FDFORMAT_35_HD = 293,

    Apricot_35 = 309,

    ADR2120 = 310,
    ADR260 = 311,
    ADR30 = 312,
    ADR50 = 313,

    AIT1 = 320,
    AIT1Turbo = 321,
    AIT2 = 322,
    AIT2Turbo = 323,
    AIT3 = 324,
    AIT3Ex = 325,
    AIT3Turbo = 326,
    AIT4 = 327,
    AIT5 = 328,
    AITETurbo = 329,
    SAIT1 = 330,
    SAIT2 = 331,

    Bernoulli = 340,
    Bernoulli2 = 341,
    Ditto  = 342,
    DittoMax  = 343,
    Jaz        = 344,
    Jaz2   = 345,
    PocketZip = 346,
    REV120     = 347,
    REV35  = 348,
    REV70     = 349,
    ZIP100     = 350,
    ZIP250 = 351,
    ZIP750    = 352,
    Bernoulli35 = 353,
    Bernoulli44 = 354,
    Bernoulli65 = 355,
    Bernoulli90 = 356,
    Bernoulli105 = 357,
    Bernoulli150 = 358,
    Bernoulli230 = 359,

    CompactCassette = 360,
    Data8 = 361,
    MiniDV = 362,
    Dcas25 = 363,
    Dcas85 = 364,
    Dcas103 = 365,

    CFast = 370,
    CompactFlash = 371,
    CompactFlashType2 = 372,

    DigitalAudioTape = 380,
    DAT160 = 381,
    DAT320 = 382,
    DAT72 = 383,
    DDS1 = 384,
    DDS2 = 385,
    DDS3 = 386,
    DDS4 = 387,

    CompactTapeI = 390,
    CompactTapeII = 391,
    DECtapeII = 392,
    DLTtapeIII = 393,
    DLTtapeIIIxt = 394,
    DLTtapeIV = 395,
    DLTtapeS4 = 396,
    SDLT1 = 397,
    SDLT2 = 398,
    VStapeI = 399,

    Exatape15m = 400,
    Exatape22m = 401,
    Exatape22mAME = 402,
    Exatape28m = 403,
    Exatape40m = 404,
    Exatape45m = 405,
    Exatape54m = 406,
    Exatape75m = 407,
    Exatape76m = 408,
    Exatape80m = 409,
    Exatape106m = 410,
    Exatape160mXL = 411,
    Exatape112m = 412,
    Exatape125m = 413,
    Exatape150m = 414,
    Exatape170m = 415,
    Exatape225m = 416,

    ExpressCard34 = 420,
    ExpressCard54 = 421,
    PCCardTypeI = 422,
    PCCardTypeII = 423,
    PCCardTypeIII = 424,
    PCCardTypeIV = 425,

    EZ135 = 430,
    EZ230 = 431,
    Quest = 432,
    SparQ = 433,
    SQ100 = 434,
    SQ200 = 435,
    SQ300 = 436,
    SQ310 = 437,
    SQ327 = 438,
    SQ400 = 439,
    SQ800 = 440,
    SQ1500 = 441,
    SQ2000 = 442,
    SyJet = 443,

    FamicomGamePak = 450,
    GameBoyAdvanceGamePak = 451,
    GameBoyGamePak = 452,
    GOD = 453,
    N64DD = 454,
    N64GamePak = 455,
    NESGamePak = 456,
    Nintendo3DSGameCard = 457,
    NintendoDiskCard = 458,
    NintendoDSGameCard = 459,
    NintendoDSiGameCard = 460,
    SNESGamePak = 461,
    SNESGamePakUS = 462,
    WOD = 463,
    WUOD = 464,
    SwitchGameCard = 465,

    IBM3470 = 470,
    IBM3480 = 471,
    IBM3490 = 472,
    IBM3490E = 473,
    IBM3592 = 474,

    LTO = 480,
    LTO2 = 481,
    LTO3 = 482,
    LTO3WORM = 483,
    LTO4 = 484,
    LTO4WORM = 485,
    LTO5 = 486,
    LTO5WORM = 487,
    LTO6 = 488,
    LTO6WORM = 489,
    LTO7 = 490,
    LTO7WORM = 491,

    MemoryStick = 510,
    MemoryStickDuo = 511,
    MemoryStickMicro = 512,
    MemoryStickPro = 513,
    MemoryStickProDuo = 514,

    microSD = 520,
    miniSD = 521,
    SecureDigital = 522,

    MMC = 530,
    MMCmicro = 531,
    RSMMC = 532,
    MMCplus = 533,
    MMCmobile = 534,

    MLR1 = 540,
    MLR1SL = 541,
    MLR3 = 542,
    SLR1 = 543,
    SLR2 = 544,
    SLR3 = 545,
    SLR32 = 546,
    SLR32SL = 547,
    SLR4 = 548,
    SLR5 = 549,
    SLR5SL = 550,
    SLR6 = 551,
    SLRtape7 = 552,
    SLRtape7SL = 553,
    SLRtape24 = 554,
    SLRtape24SL = 555,
    SLRtape40 = 556,
    SLRtape50 = 557,
    SLRtape60 = 558,
    SLRtape75 = 559,
    SLRtape100 = 560,
    SLRtape140 = 561,

    QIC11 = 570,
    QIC120 = 571,
    QIC1350 = 572,
    QIC150 = 573,
    QIC24 = 574,
    QIC3010 = 575,
    QIC3020 = 576,
    QIC3080 = 577,
    QIC3095 = 578,
    QIC320 = 579,
    QIC40 = 580,
    QIC525 = 581,
    QIC80 = 582,

    STK4480 = 590,
    STK4490 = 591,
    STK9490 = 592,
    T9840A = 593,
    T9840B = 594,
    T9840C = 595,
    T9840D = 596,
    T9940A = 597,
    T9940B = 598,
    T10000A = 599,
    T10000B = 600,
    T10000C = 601,
    T10000D = 602,

    Travan = 610,
    Travan1Ex = 611,
    Travan3 = 612,
    Travan3Ex = 613,
    Travan4 = 614,
    Travan5 = 615,
    Travan7 = 616,

    VXA1 = 620,
    VXA2 = 621,
    VXA3 = 622,

    ECMA_153 = 630,
    ECMA_153_512 = 631,
    ECMA_154 = 632,
    ECMA_183_512 = 633,
    ECMA_183 = 634,
    ECMA_184_512 = 635,
    ECMA_184 = 636,
    ECMA_189 = 637,
    ECMA_190 = 638,
    ECMA_195 = 639,
    ECMA_195_512 = 640,
    ECMA_201 = 641,
    ECMA_201_ROM = 642,
    ECMA_223 = 643,
    ECMA_223_512 = 644,
    ECMA_238 = 645,
    ECMA_239 = 646,
    ECMA_260 = 647,
    ECMA_260_Double = 648,
    ECMA_280 = 649,
    ECMA_317 = 650,
    ECMA_322 = 651,
    ECMA_322_2k = 652,
    GigaMo = 653,
    GigaMo2 = 654,

    CompactFloppy = 660,
    DemiDiskette = 661,
    Floptical = 662,
    HiFD = 663,
    QuickDisk = 664,
    UHD144 = 665,
    VideoFloppy = 666,
    Wafer = 667,
    ZXMicrodrive = 668,

    BeeCard = 670,
    Borsu = 671,
    DataStore = 672,
    DIR = 673,
    DST = 674,
    DTF = 675,
    DTF2 = 676,
    Flextra3020 = 677,
    Flextra3225 = 678,
    HiTC1 = 679,
    HiTC2 = 680,
    LT1 = 681,
    MiniCard = 872,
    Orb = 683,
    Orb5 = 684,
    SmartMedia = 685,
    xD = 686,
    XQD = 687,
    DataPlay = 688,

    AppleProfile = 690,
    AppleWidget = 691,
    AppleHD20 = 692,
    PriamDataTower = 693,
    Pippin = 694,

    RA60 = 700,
    RA80 = 701,
    RA81 = 702,
    RC25 = 703,
    RD31 = 704,
    RD32 = 705,
    RD51 = 706,
    RD52 = 707,
    RD53 = 708,
    RD54 = 709,
    RK06 = 710,
    RK06_18 = 711,
    RK07 = 712,
    RK07_18 = 713,
    RM02 = 714,
    RM03 = 715,
    RM05 = 716,
    RP02 = 717,
    RP02_18 = 718,
    RP03 = 719,
    RP03_18 = 720,
    RP04 = 721,
    RP04_18 = 722,
    RP05 = 723,
    RP05_18 = 724,
    RP06 = 725,
    RP06_18 = 726,

    LS120  = 730,
    LS240  = 731,
    FD32MB = 732,
    RDX = 733,
    RDX320 = 734,

    VideoNow = 740,
    VideoNowColor = 741,
    VideoNowXp = 742,

    Bernoulli10 = 750,
    Bernoulli20 = 751,
    BernoulliBox2_20 = 752,

    KodakVerbatim3 = 760,
    KodakVerbatim6 = 761,
    KodakVerbatim12 = 762,

    ProfessionalDisc = 770,
    ProfessionalDiscDual = 771,
    ProfessionalDiscTriple = 772,
    ProfessionalDiscQuad = 773,
    PDD = 774,
    PDD_WORM = 775,
    ArchivalDisc = 776,
    ArchivalDisc2 = 777,
    ArchivalDisc3 = 779,
    ODC300R = 778,
    ODC300RE = 780,
    ODC600R = 781,
    ODC600RE = 782,
    ODC1200RE = 783,
    ODC1500R = 784,
    ODC3300R = 785,
    ODC5500R = 786
};

enum CompressionType : u16
{
    None = 0,
    Lzma = 1,
    Flac = 2
};

enum DataType : u16
{
    NoData                           = 0,
    UserData                         = 1,
    CompactDiscPartialToc            = 2,
    CompactDiscSessionInfo           = 3,
    CompactDiscToc                   = 4,
    CompactDiscPma                   = 5,
    CompactDiscAtip                  = 6,
    CompactDiscLeadInCdText          = 7,
    DvdPfi                           = 8,
    DvdLeadInCmi                     = 9,
    DvdDiscKey                       = 10,
    DvdBca                           = 11,
    DvdDmi                           = 12,
    DvdMediaIdentifier               = 13,
    DvdMediaKeyBlock                 = 14,
    DvdRamDds                        = 15,
    DvdRamMediumStatus               = 16,
    DvdRamSpareArea                  = 17,
    DvdRRmd                          = 18,
    DvdRPrerecordedInfo              = 19,
    DvdRMediaIdentifier              = 20,
    DvdRPfi                          = 21,
    DvdAdip                          = 22,
    HdDvdCpi                         = 23,
    HdDvdMediumStatus                = 24,
    DvdDlLayerCapacity               = 25,
    DvdDlMiddleZoneAddress           = 26,
    DvdDlJumpIntervalSize            = 27,
    DvdDlManualLayerJumpLba          = 28,
    BlurayDi                         = 29,
    BlurayBca                        = 30,
    BlurayDds                        = 31,
    BlurayCartridgeStatus            = 32,
    BluraySpareArea                  = 33,
    AacsVolumeIdentifier             = 34,
    AacsSerialNumber                 = 35,
    AacsMediaIdentifier              = 36,
    AacsMediaKeyBlock                = 37,
    AacsDataKeys                     = 38,
    AacsLbaExtents                   = 39,
    CprmMediaKeyBlock                = 40,
    HybridRecognizedLayers           = 41,
    ScsiMmcWriteProtection           = 42,
    ScsiMmcDiscInformation           = 43,
    ScsiMmcTrackResourcesInformation = 44,
    ScsiMmcPowResourcesInformation   = 45,
    ScsiInquiry                      = 46,
    ScsiModePage2A                   = 47,
    AtaIdentify                      = 48,
    AtapiIdentify                    = 49,
    PcmciaCis                        = 50,
    SecureDigitalCid                 = 51,
    SecureDigitalCsd                 = 52,
    SecureDigitalScr                 = 53,
    SecureDigitalOcr                 = 54,
    MultiMediaCardCid                = 55,
    MultiMediaCardCsd                = 56,
    MultiMediaCardOcr                = 57,
    MultiMediaCardExtendedCsd        = 58,
    XboxSecuritySector               = 59,
    FloppyLeadOut                    = 60,
    DvdDiscControlBlock              = 61,
    CompactDiscFirstTrackPregap      = 62,
    CompactDiscLeadOut               = 63,
    ScsiModeSense6                   = 64,
    ScsiModeSense10                  = 65,
    UsbDescriptors                   = 66,
    XboxDmi                          = 67,
    XboxPfi                          = 68,
    CdSectorPrefix                   = 69,
    CdSectorSuffix                   = 70,
    CdSectorSubchannel               = 71,
    AppleProfileTag                  = 72,
    AppleSonyTag                     = 73,
    PriamDataTowerTag                = 74,
    CompactDiscMediaCatalogueNumber  = 75,
    CdSectorPrefixCorrected          = 76,
    CdSectorSuffixCorrected          = 77,
    CompactDiscMode2Subheader        = 78,
    CompactDiscLeadIn                = 79,
    DecryptedDVDDiscKey              = 80,
    DVD_CPI_MAI                      = 81,
    DecryptedDVDTitleKey             = 82,
    FluxData                         = 83,
    BitstreamData                    = 84
};

enum BlockType : u32
{
    DataBlock                    = 0x4B4C4244,
    DeDuplicationTable           = 0X2A544444,
    DeDuplicationTable2          = 0x32454444,
    Index                        = 0X58444E49,
    Index2                       = 0x32584449,
    IndexContinuation            = 0x43584449,
    GeometryBlock                = 0x4D4F4547,
    MetadataBlock                = 0x4154454D,
    TracksBlock                  = 0x534B5254,
    CicmBlock                    = 0x4D434943,
    ChecksumBlock                = 0x4D534B43,
    DataPositionMeasurementBlock = 0x2A4D5044,
    SnapshotBlock                = 0x50414E53,
    ParentBlock                  = 0x50524E54,
    DumpHardwareBlock            = 0x2A504D44,
    TapeFileBlock                = 0x454C4654,
    TapePartitionBlock			 = 0x54425054,
    TwinSectorTable              = 0x42545754,
    CompactDiscIndexesBlock      = 0x58494443,
    FluxDataBlock                = 0x58554C46,
    BitstreamDataBlock           = 0x53544942,
    TrackLayoutBlock             = 0x594C4B54
};

enum TrackType : u8
{
    Audio = 0,
    Data = 1,
    CdMode1 = 2,
    CdMode2Formless = 3,
    CdMode2Form1 = 4,
    CdMode2Form2 = 5
};

enum ChecksumAlgorithm : u8
{
    Invalid = 0,
    Md5 = 1,
    Sha1 = 2,
    Sha256 = 3,
    SpamSum = 4
};

enum DdtSizeType : u8
{
	Mini,
	Small,
	Medium,
	Big
};

struct DataBlock
{
	BlockType identifier;
	DataType type;
	CompressionType compression;
	u32 sectorSize;
	u32 cmpLength;
	u32 length;
	u64 cmpCrc64;
	u64 crc64;
};

struct DeDuplicationTable
{
	BlockType identifier;
	DataType type;
	CompressionType compression;
	u8 shift;
	u64 entries;
	u32 cmpLength;
	u32 length;
	u64 cmpCrc64;
	u64 crc64;
};

struct DeDuplicationTable2
{
	BlockType identifier;
	DataType type;
	CompressionType compression;
	u8 levels;
	u8 tableLevel;
	u64 previousLevel;
	u16 negative;
	u64 start;
	u8 alignment;
	u8 shift;
	DdtSizeType sizeType;
	u64 entries;
	u32 cmpLength;
	u32 length;
	u64 cmpCrc64;
	u64 crc64;
};

struct TwinSectorTable
{
	BlockType identifier;
	u8 alignment;
	u8 shift;
	u64 entries;
	u32 length;
	u64 crc64;
};

struct GeometryBlock
{
	BlockType identifier;
	u32 cylinders;
	u32 heads;
	u32 sectorsPerTrack;
};

struct MetadataBlock
{
	BlockType identifier;
	u32 blockSize;
	s32 mediaSequence;
	s32 lastMediaSequence;
	u32 creatorOffset;
	u32 creatorLength;
	u32 commentsOffset;
	u32 commentsLength;
	u32 mediaTitleOffset;
	u32 mediaTitleLength;
	u32 mediaManufacturerOffset;
	u32 mediaManufacturerLength;
	u32 mediaModelOffset;
	u32 mediaModelLength;
	u32 mediaSerialNumberOffset;
	u32 mediaSerialNumberLength;
	u32 mediaBarcodeOffset;
	u32 mediaBarcodeLength;
	u32 mediaPartNumberOffset;
	u32 mediaPartNumberLength;
	u32 driveManufacturerOffset;
	u32 driveManufacturerLength;
	u32 driveModelOffset;
	u32 driveModelLength;
	u32 driveSerialNumberOffset;
	u32 driveSerialNumberLength;
	u32 driveFirmwareRevisionOffset;
	u32 driveFirmwareRevisionLength;
	
	if(creatorLength > 0)
		char16 creator[creatorLength];
		
	if(commentsLength > 0)
		char16 comments[commentsLength];
		
	if(mediaTitleLength > 0)
		char16 mediaTitle[mediaTitleLength];
		
	if(mediaManufacturerLength > 0)
		char16 mediaManufacturer[mediaManufacturerLength];
		
	if(mediaModelLength > 0)
		char16 mediaModel[mediaModelLength];
		
	if(mediaSerialNumberLength > 0)
		char16 mediaSerialNumber[mediaSerialNumberLength];
		
	if(mediaBarcodeLength > 0)
		char16 mediaBarcode[mediaBarcodeLength];
		
	if(mediaPartNumberLength > 0)
		char16 mediaPartNumber[mediaPartNumberLength];
		
	if(driveManufacturerLength > 0)
		char16 driveManufacturer[driveManufacturerLength];
		
	if(driveModelLength > 0)
		char16 driveModel[driveModelLength];
		
	if(driveSerialNumberLength > 0)
		char16 driveSerialNumber[driveSerialNumberLength];
		
	if(driveFirmwareRevisionLength > 0)
		char16 driveFirmwareRevision[driveFirmwareRevisionLength];
};		

struct TrackEntry
{
	u8 sequence;
	TrackType type;
	s64 start;
	s64 end;
	s64 pregap;
	u8 session;
	char isrc[13];
	u8 flags;
};

struct TracksBlock
{
	BlockType identifier;
	u16 entries;
	u64 crc64;
	TrackEntry tracks[entries];
};

struct CicmBlock
{
	BlockType identifier;
	u32 length;
	char cicmXml[length];
};

struct ChecksumEntry
{
	ChecksumAlgorithm algorithm;
	u32 length;
	if(algorithm == ChecksumAlgorithm::SpamSum)
		char checksum[length];
	else
		u8 checksum[length];
};

struct ChecksumBlock
{
	BlockType identifier;
	u32 length;
	u8 entries;
	ChecksumEntry checksums[entries];
};

using Index;

struct SnapshotBlock
{
	BlockType identifier;
	u32 length;
	u16 generation;
	s64 creationTime; // TODO: Decode time
	Index *index : u64;
};

struct ParentFileBlock
{
	BlockType identifier;
	u32 length;
	type::GUID parentId;
	u16 parentClueLength;
	char16 parentClue[parentClueLength];
};

struct DumpHardwareExtent
{
	u64 start;
	u64 end;
};

struct DumpHardwareEntry
{
	u32 manufacturerLength;
	u32 modelLength;
	u32 revisionLength;
	u32 firmwareLength;
	u32 serialLength;
	u32 softwareNameLength;
	u32 softwareVersionLength;
	u32 softwareOperatingSystemLength;
	u32 extents;
	
	if(manufacturerLength > 0)
		char manufacturer[manufacturerLength];
	
	if(modelLength > 0)
		char model[modelLength];
		
	if(revisionLength > 0)
		char revision[revisionLength];
	
	if(firmwareLength > 0)
		char firmware[firmwareLength];
	
	if(serialLength > 0)
		char serial[serialLength];
		
	if(softwareNameLength > 0)
		char name[softwareNameLength];

	if(softwareVersionLength > 0)
		char version[softwareVersionLength];

	if(softwareOperatingSystemLength > 0)
		char operatingSystem[softwareOperatingSystemLength];
};

struct DumpHardwareBlock
{
	BlockType identifier;
	u16 entries;
	u32 length;
	u64 crc64;
	DumpHardwareEntry items[entries];
};

struct TapeFileEntry
{
	u32 file;
	u8 partition;
	u64 firstBlock;
	u64 lastBlock;
};

struct TapeFileBlock
{
	BlockType identifier;
	u16 entries;
	u32 length;
	u64 crc64;
	TapeFileEntry files[entries];
};

struct TapePartitionEntry
{
	u8 number;
	u64 firstBlock;
	u64 lastBlock;
};

struct TapePartitionBlock
{
	BlockType identifier;
	u16 entries;
	u32 length;
	u64 crc64;
	TapePartitionEntry partitions[entries];
};

struct CompactDiscIndexEntry
{
	u16 track;
	u16 index;
	s32 lba;
};

struct CompactDiscIndexesBlock
{
	BlockType identifier;
	u16 entries;
	u32 length;
	u64 crc64;
	CompactDiscIndexEntry indexes[entries];
};

struct FluxDataEntry
{
	u32 head;
	u16 track;
	u8 subtrack;
	u64 resolution;
	u64 tableEntry;
};

struct FluxDataBlock
{
	BlockType identifier;
	u16 entries;
	u64 crc64;
	FluxDataEntry fluxes[entries];
};

struct BitstreamDataEntry
{
	u32 head;
	u16 track;
	u8 subtrack;
	u64 tableEntry;
};

struct BitstreamDataBlock
{
	BlockType identifier;
	u16 entries;
	u64 crc64;
	BitstreamDataEntry bits[entries];
};

struct SectorMapping
{
	u16 sector;
	u64 block;
};

struct TrackLayoutBlock
{
	BlockType identifier;
	u64 crc64;
	u32 head;
	u16 track;
	u8 subtrack;
	u16 sectors;
	FluxDataBlock *flux : u64;
	BitstreamDataBlock *bitstream : u64;
	SectorMapping mappings[sectors];
};

struct Block
{
	BlockType blockType;
	
	$ -= 4;
	
	if(blockType == BlockType::DataBlock)
		DataBlock data;
	else if(blockType == BlockType::DeDuplicationTable)
		DeDuplicationTable ddt;
	else if(blockType == BlockType::DeDuplicationTable2)
		DeDuplicationTable2 ddt2;
	else if(blockType == BlockType::TwinSectorTable)
		TwinSectorTable twinSectorTable;
	else if(blockType == BlockType::GeometryBlock)
		GeometryBlock geometry;
	else if(blockType == BlockType::MetadataBlock)
		MetadataBlock metadata;
	else if(blockType == BlockType::TracksBlock)
		TracksBlock tracks;
	else if(blockType == BlockType::CicmBlock)
		CicmBlock cicm;
	else if(blockType == BlockType::ChecksumBlock)
		ChecksumBlock checksums;
	else if(blockType == BlockType::SnapshotBlock)
		SnapshotBlock snapshot;
	else if(blockType == BlockType::ParentBlock)
		ParentFileBlock parentFile;
	else if(blockType == BlockType::DumpHardwareBlock)
		DumpHardwareBlock dumpHardware;
	else if(blockType == BlockType::TapeFileBlock)
		TapeFileBlock tapeFiles;
	else if(blockType == BlockType::TapePartitionBlock)
		TapePartitionBlock tapePartitions;
	else if(blockType == BlockType::CompactDiscIndexesBlock)
		CompactDiscIndexesBlock compactDiscIndexes;
	else if(blockType == BlockType::FluxDataBlock)
		FluxDataBlock fluxes;
	else if(blockType == BlockType::BitstreamDataBlock)
		BitstreamDataBlock bitstreams;
	else if(blockType == BlockType::TrackLayoutBlock)
		TrackLayoutBlock trackLayout;
	else if(blockType == BlockType::IndexContinuation)
		Index continuation;
};

struct IndexEntry
{
	BlockType blockType;
	DataType dataType;
	Block *offset : u64;
};

struct Index
{
    // Not working
	/* BlockType identifier [[format("std::string::to_string")]];*/
	BlockType identifier;
	
	std::assert(identifier == BlockType::Index || identifier == BlockType::Index2 ||
	 identifier == BlockType::IndexContinuation, "Invalid index identifier!");
	
	if(identifier == BlockType::Index2 || identifier == BlockType::IndexContinuation)
		u64 entries;
	else
		u16 entries;
		
	u64 crc64;
	
	IndexEntry items[entries];
};

struct AaruHeader
{
	char identifier[8];
	
	std::assert(identifier == "AARUFRMT" || identifier == "DICMFRMT", "Incorrect signature!");
	
	char16 application[32];
	u8 imageMajorVersion;
	u8 imageMinorVersion;
	u8 applicationMajorVersion;
	u8 applicationMinorVersion;
	MediaType MediaType;
	Index *indexOffset : u64;
	s64 creationTime; // TODO: Parse time
	s64 lastWrittenTime; // TODO: Parse time
	
	if(imageMajorVersion >= 3)
	{
		u32 MediaType2; // TODO: Media type
		type::GUID imageIdentifier;
	}
};

AaruHeader header @ 0x00;
