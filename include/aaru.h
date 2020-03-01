// /** *************************************************************************
// Aaru Data Preservation Suite
// ----------------------------------------------------------------------------
//
// Filename       : aaru.h
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libaaruformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Contains common media types.
//
// --[ License ] --------------------------------------------------------------
//
//     Permission is hereby granted, free of charge, to any person obtaining a
//     copy of this software and associated documentation files (the
//     "Software"), to deal in the Software without restriction, including
//     without limitation the rights to use, copy, modify, merge, publish,
//     distribute, sublicense, and/or sell copies of the Software, and to
//     permit persons to whom the Software is furnished to do so, subject to
//     the following conditions:
//
//     The above copyright notice and this permission notice shall be included
//     in all copies or substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//     OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//     MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//     IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//     CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//     TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//     SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2020 Natalia Portillo
// ****************************************************************************/

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "OCUnusedGlobalDeclarationInspection"
#ifndef LIBAARUFORMAT_AARU_H
#define LIBAARUFORMAT_AARU_H

#include <stdint.h>

// TODO: Generate automatically from C#

/**
 *     Contains an enumeration of all known types of media.
 */
typedef enum
{
    // Generics, types 0 to 9
    /** Unknown disk type */
    Unknown = 0,
    /** Unknown magneto-optical */
    UnknownMO = 1,
    /** Generic hard disk */
    GENERIC_HDD = 2,
    /** Microdrive type hard disk */
    Microdrive = 3,
    /** Zoned hard disk */
    Zone_HDD = 4,
    /** USB flash drives */
    FlashDrive = 5,
    // Generics, types 0 to 9

    // Somewhat standard Compact Disc formats, types 10 to 39
    /** Any unknown or standard violating CD */
    CD = 10,
    /** CD Digital Audio (Red Book) */
    CDDA = 11,
    /** CD+G (Red Book) */
    CDG = 12,
    /** CD+EG (Red Book) */
    CDEG = 13,
    /** CD-i (Green Book) */
    CDI = 14,
    /** CD-ROM (Yellow Book) */
    CDROM = 15,
    /** CD-ROM XA (Yellow Book) */
    CDROMXA = 16,
    /** CD+ (Blue Book) */
    CDPLUS = 17,
    /** CD-MO (Orange Book) */
    CDMO = 18,
    /** CD-Recordable (Orange Book) */
    CDR = 19,
    /** CD-ReWritable (Orange Book) */
    CDRW = 20,
    /** Mount-Rainier CD-RW */
    CDMRW = 21,
    /** Video CD (White Book) */
    VCD = 22,
    /** Super Video CD (White Book) */
    SVCD = 23,
    /** Photo CD (Beige Book) */
    PCD = 24,
    /** Super Audio CD (Scarlet Book) */
    SACD = 25,
    /** Double-Density CD-ROM (Purple Book) */
    DDCD = 26,
    /** DD CD-R (Purple Book) */
    DDCDR = 27,
    /** DD CD-RW (Purple Book) */
    DDCDRW = 28,
    /** DTS audio CD (non-standard) */
    DTSCD = 29,
    /** CD-MIDI (Red Book) */
    CDMIDI = 30,
    /** CD-Video (ISO/IEC 61104) */
    CDV = 31,
    /** 120mm, Phase-Change, 1298496 sectors, 512 bytes/sector, PD650, ECMA-240, ISO 15485 */
    PD650 = 32,
    /** 120mm, Write-Once, 1281856 sectors, 512 bytes/sector, PD650, ECMA-240, ISO 15485 */
    PD650_WORM = 33, /**
                      *     CD-i Ready, contains a track before the first TOC track, in mode 2, and all TOC tracks are
                      * Audio. Subchannel marks track as audio pause.
                      */
    CDIREADY = 34,
    FMTOWNS  = 35,
    // Somewhat standard Compact Disc formats, types 10 to 39

    // Standard DVD formats, types 40 to 50
    /** DVD-ROM (applies to DVD Video and DVD Audio) */
    DVDROM = 40,
    /** DVD-R */
    DVDR = 41,
    /** DVD-RW */
    DVDRW = 42,
    /** DVD+R */
    DVDPR = 43,
    /** DVD+RW */
    DVDPRW = 44,
    /** DVD+RW DL */
    DVDPRWDL = 45,
    /** DVD-R DL */
    DVDRDL = 46,
    /** DVD+R DL */
    DVDPRDL = 47,
    /** DVD-RAM */
    DVDRAM = 48,
    /** DVD-RW DL */
    DVDRWDL = 49,
    /** DVD-Download */
    DVDDownload = 50,
    // Standard DVD formats, types 40 to 50

    // Standard HD-DVD formats, types 51 to 59
    /** HD DVD-ROM (applies to HD DVD Video) */
    HDDVDROM = 51,
    /** HD DVD-RAM */
    HDDVDRAM = 52,
    /** HD DVD-R */
    HDDVDR = 53,
    /** HD DVD-RW */
    HDDVDRW = 54,
    /** HD DVD-R DL */
    HDDVDRDL = 55,
    /** HD DVD-RW DL */
    HDDVDRWDL = 56,
    // Standard HD-DVD formats, types 51 to 59

    // Standard Blu-ray formats, types 60 to 69
    /** BD-ROM (and BD Video) */
    BDROM = 60,
    /** BD-R */
    BDR = 61,
    /** BD-RE */
    BDRE = 62,
    /** BD-R XL */
    BDRXL = 63,
    /** BD-RE XL */
    BDREXL = 64,
    // Standard Blu-ray formats, types 60 to 69

    // Rare or uncommon optical standards, types 70 to 79
    /** Enhanced Versatile Disc */
    EVD = 70,
    /** Forward Versatile Disc */
    FVD = 71,
    /** Holographic Versatile Disc */
    HVD = 72,
    /** China Blue High Definition */
    CBHD = 73,
    /** High Definition Versatile Multilayer Disc */
    HDVMD = 74,
    /** Versatile Compact Disc High Density */
    VCDHD = 75,
    /** Stacked Volumetric Optical Disc */
    SVOD = 76,
    /** Five Dimensional disc */
    FDDVD = 77,
    // Rare or uncommon optical standards, types 70 to 79

    // LaserDisc based, types 80 to 89
    /** Pioneer LaserDisc */
    LD = 80,
    /** Pioneer LaserDisc data */
    LDROM  = 81,
    LDROM2 = 82,
    LVROM  = 83,
    MegaLD = 84,
    // LaserDisc based, types 80 to 89

    // MiniDisc based, types 90 to 99
    /** Sony Hi-MD */
    HiMD = 90,
    /** Sony MiniDisc */
    MD      = 91,
    MDData  = 92,
    MDData2 = 93,
    // MiniDisc based, types 90 to 99

    // Plasmon UDO, types 100 to 109
    /** 5.25", Phase-Change, 1834348 sectors, 8192 bytes/sector, Ultra Density Optical, ECMA-350, ISO 17345 */
    UDO = 100,
    /** 5.25", Phase-Change, 3669724 sectors, 8192 bytes/sector, Ultra Density Optical 2, ECMA-380, ISO 11976 */
    UDO2 = 101,
    /** 5.25", Write-Once, 3668759 sectors, 8192 bytes/sector, Ultra Density Optical 2, ECMA-380, ISO 11976 */
    UDO2_WORM = 102,
    // Plasmon UDO, types 100 to 109

    // Sony game media, types 110 to 129
    PlayStationMemoryCard  = 110,
    PlayStationMemoryCard2 = 111,
    /** Sony PlayStation game CD */
    PS1CD = 112,
    /** Sony PlayStation 2 game CD */
    PS2CD = 113,
    /** Sony PlayStation 2 game DVD */
    PS2DVD = 114,
    /** Sony PlayStation 3 game DVD */
    PS3DVD = 115,
    /** Sony PlayStation 3 game Blu-ray */
    PS3BD = 116,
    /** Sony PlayStation 4 game Blu-ray */
    PS4BD = 117,
    /** Sony PlayStation Portable Universal Media Disc (ECMA-365) */
    UMD                     = 118,
    PlayStationVitaGameCard = 119,
    // Sony game media, types 110 to 129

    // Microsoft game media, types 130 to 149
    /** Microsoft X-box Game Disc */
    XGD = 130,
    /** Microsoft X-box 360 Game Disc */
    XGD2 = 131,
    /** Microsoft X-box 360 Game Disc */
    XGD3 = 132,
    /** Microsoft X-box One Game Disc */
    XGD4 = 133,
    // Microsoft game media, types 130 to 149

    // Sega game media, types 150 to 169
    /** Sega MegaCD */
    MEGACD = 150,
    /** Sega Saturn disc */
    SATURNCD = 151,
    /** Sega/Yamaha Gigabyte Disc */
    GDROM = 152,
    /** Sega/Yamaha recordable Gigabyte Disc */
    GDR      = 153,
    SegaCard = 154,
    MilCD    = 155,
    // Sega game media, types 150 to 169

    // Other game media, types 170 to 179
    /** PC-Engine / TurboGrafx cartridge */
    HuCard = 170,
    /** PC-Engine / TurboGrafx CD */
    SuperCDROM2 = 171,
    /** Atari Jaguar CD */
    JaguarCD = 172,
    /** 3DO CD */
    ThreeDO = 173,
    /** NEC PC-FX */
    PCFX = 174,
    /** NEO-GEO CD */
    NeoGeoCD = 175,
    /** Commodore CDTV */
    CDTV = 176,
    /** Amiga CD32 */
    CD32 = 177,
    /** Nuon (DVD based videogame console) */
    Nuon = 178,
    /** Bandai Playdia */
    Playdia = 179,
    // Other game media, types 170 to 179

    // Apple standard floppy format, types 180 to 189
    /** 5.25", SS, DD, 35 tracks, 13 spt, 256 bytes/sector, GCR */
    Apple32SS = 180,
    /** 5.25", DS, DD, 35 tracks, 13 spt, 256 bytes/sector, GCR */
    Apple32DS = 181,
    /** 5.25", SS, DD, 35 tracks, 16 spt, 256 bytes/sector, GCR */
    Apple33SS = 182,
    /** 5.25", DS, DD, 35 tracks, 16 spt, 256 bytes/sector, GCR */
    Apple33DS = 183,
    /** 3.5", SS, DD, 80 tracks, 8 to 12 spt, 512 bytes/sector, GCR */
    AppleSonySS = 184,
    /** 3.5", DS, DD, 80 tracks, 8 to 12 spt, 512 bytes/sector, GCR */
    AppleSonyDS = 185,
    /** 5.25", DS, ?D, ?? tracks, ?? spt, 512 bytes/sector, GCR, opposite side heads, aka Twiggy */
    AppleFileWare = 186,
    // Apple standard floppy format

    // IBM/Microsoft PC floppy formats, types 190 to 209
    /** 5.25", SS, DD, 40 tracks, 8 spt, 512 bytes/sector, MFM */
    DOS_525_SS_DD_8 = 190,
    /** 5.25", SS, DD, 40 tracks, 9 spt, 512 bytes/sector, MFM */
    DOS_525_SS_DD_9 = 191,
    /** 5.25", DS, DD, 40 tracks, 8 spt, 512 bytes/sector, MFM */
    DOS_525_DS_DD_8 = 192,
    /** 5.25", DS, DD, 40 tracks, 9 spt, 512 bytes/sector, MFM */
    DOS_525_DS_DD_9 = 193,
    /** 5.25", DS, HD, 80 tracks, 15 spt, 512 bytes/sector, MFM */
    DOS_525_HD = 194,
    /** 3.5", SS, DD, 80 tracks, 8 spt, 512 bytes/sector, MFM */
    DOS_35_SS_DD_8 = 195,
    /** 3.5", SS, DD, 80 tracks, 9 spt, 512 bytes/sector, MFM */
    DOS_35_SS_DD_9 = 196,
    /** 3.5", DS, DD, 80 tracks, 8 spt, 512 bytes/sector, MFM */
    DOS_35_DS_DD_8 = 197,
    /** 3.5", DS, DD, 80 tracks, 9 spt, 512 bytes/sector, MFM */
    DOS_35_DS_DD_9 = 198,
    /** 3.5", DS, HD, 80 tracks, 18 spt, 512 bytes/sector, MFM */
    DOS_35_HD = 199,
    /** 3.5", DS, ED, 80 tracks, 36 spt, 512 bytes/sector, MFM */
    DOS_35_ED = 200,
    /** 3.5", DS, HD, 80 tracks, 21 spt, 512 bytes/sector, MFM */
    DMF = 201,
    /** 3.5", DS, HD, 82 tracks, 21 spt, 512 bytes/sector, MFM */
    DMF_82 = 202,  /**
                    *     5.25", DS, HD, 80 tracks, ? spt, ??? + ??? + ??? bytes/sector, MFM track 0 = ??15 sectors, 512
                    *     bytes/sector, falsified to DOS as 19 spt, 512 bps
                    */
    XDF_525 = 203, /**
                    *     3.5", DS, HD, 80 tracks, 4 spt, 8192 + 2048 + 1024 + 512 bytes/sector, MFM track 0 = 19
                    * sectors, 512 bytes/sector, falsified to DOS as 23 spt, 512 bps
                    */
    XDF_35 = 204,
    // IBM/Microsoft PC standard floppy formats, types 190 to 209

    // IBM standard floppy formats, types 210 to 219
    /** 8", SS, SD, 32 tracks, 8 spt, 319 bytes/sector, FM */
    IBM23FD = 210,
    /** 8", SS, SD, 73 tracks, 26 spt, 128 bytes/sector, FM */
    IBM33FD_128 = 211,
    /** 8", SS, SD, 74 tracks, 15 spt, 256 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector */
    IBM33FD_256 = 212,
    /** 8", SS, SD, 74 tracks, 8 spt, 512 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector */
    IBM33FD_512 = 213,
    /** 8", DS, SD, 74 tracks, 26 spt, 128 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector */
    IBM43FD_128 = 214,
    /** 8", DS, SD, 74 tracks, 26 spt, 256 bytes/sector, FM, track 0 = 26 sectors, 128 bytes/sector */
    IBM43FD_256 = 215, /**
                        *     8", DS, DD, 74 tracks, 26 spt, 256 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                        * bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
                        */
    IBM53FD_256 = 216, /**
                        *     8", DS, DD, 74 tracks, 15 spt, 512 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                        * bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
                        */
    IBM53FD_512 = 217, /**
                        *     8", DS, DD, 74 tracks, 8 spt, 1024 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                        * bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
                        */
    IBM53FD_1024 = 218,
    // IBM standard floppy formats, types 210 to 219

    // DEC standard floppy formats, types 220 to 229
    /** 8", SS, DD, 77 tracks, 26 spt, 128 bytes/sector, FM */
    RX01 = 220,
    /** 8", SS, DD, 77 tracks, 26 spt, 256 bytes/sector, FM/MFM */
    RX02 = 221,
    /** 8", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, FM/MFM */
    RX03 = 222,
    /** 5.25", SS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM */
    RX50 = 223,
    // DEC standard floppy formats, types 220 to 229

    // Acorn standard floppy formats, types 230 to 239
    /** 5,25", SS, SD, 40 tracks, 10 spt, 256 bytes/sector, FM */
    ACORN_525_SS_SD_40 = 230,
    /** 5,25", SS, SD, 80 tracks, 10 spt, 256 bytes/sector, FM */
    ACORN_525_SS_SD_80 = 231,
    /** 5,25", SS, DD, 40 tracks, 16 spt, 256 bytes/sector, MFM */
    ACORN_525_SS_DD_40 = 232,
    /** 5,25", SS, DD, 80 tracks, 16 spt, 256 bytes/sector, MFM */
    ACORN_525_SS_DD_80 = 233,
    /** 5,25", DS, DD, 80 tracks, 16 spt, 256 bytes/sector, MFM */
    ACORN_525_DS_DD = 234,
    /** 3,5", DS, DD, 80 tracks, 5 spt, 1024 bytes/sector, MFM */
    ACORN_35_DS_DD = 235,
    /** 3,5", DS, HD, 80 tracks, 10 spt, 1024 bytes/sector, MFM */
    ACORN_35_DS_HD = 236,
    // Acorn standard floppy formats, types 230 to 239

    // Atari standard floppy formats, types 240 to 249
    /** 5,25", SS, SD, 40 tracks, 18 spt, 128 bytes/sector, FM */
    ATARI_525_SD = 240,
    /** 5,25", SS, ED, 40 tracks, 26 spt, 128 bytes/sector, MFM */
    ATARI_525_ED = 241,
    /** 5,25", SS, DD, 40 tracks, 18 spt, 256 bytes/sector, MFM */
    ATARI_525_DD = 242,
    /** 3,5", SS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM */
    ATARI_35_SS_DD = 243,
    /** 3,5", DS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM */
    ATARI_35_DS_DD = 244,
    /** 3,5", SS, DD, 80 tracks, 11 spt, 512 bytes/sector, MFM */
    ATARI_35_SS_DD_11 = 245,
    /** 3,5", DS, DD, 80 tracks, 11 spt, 512 bytes/sector, MFM */
    ATARI_35_DS_DD_11 = 246,
    // Atari standard floppy formats, types 240 to 249

    // Commodore standard floppy formats, types 250 to 259
    /** 3,5", DS, DD, 80 tracks, 10 spt, 512 bytes/sector, MFM (1581) */
    CBM_35_DD = 250,
    /** 3,5", DS, DD, 80 tracks, 11 spt, 512 bytes/sector, MFM (Amiga) */
    CBM_AMIGA_35_DD = 251,
    /** 3,5", DS, HD, 80 tracks, 22 spt, 512 bytes/sector, MFM (Amiga) */
    CBM_AMIGA_35_HD = 252,
    /** 5,25", SS, DD, 35 tracks, GCR */
    CBM_1540 = 253,
    /** 5,25", SS, DD, 40 tracks, GCR */
    CBM_1540_Ext = 254,
    /** 5,25", DS, DD, 35 tracks, GCR */
    CBM_1571 = 255,
    // Commodore standard floppy formats, types 250 to 259

    // NEC/SHARP standard floppy formats, types 260 to 269
    /** 8", DS, SD, 77 tracks, 26 spt, 128 bytes/sector, FM */
    NEC_8_SD = 260,
    /** 8", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, MFM */
    NEC_8_DD = 261,
    /** 5.25", SS, SD, 80 tracks, 16 spt, 256 bytes/sector, FM */
    NEC_525_SS = 262,
    /** 5.25", DS, SD, 80 tracks, 16 spt, 256 bytes/sector, MFM */
    NEC_525_DS = 263,
    /** 5,25", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM */
    NEC_525_HD = 264,
    /** 3,5", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM, aka mode 3 */
    NEC_35_HD_8 = 265,
    /** 3,5", DS, HD, 80 tracks, 15 spt, 512 bytes/sector, MFM */
    NEC_35_HD_15 = 266,
    /** 3,5", DS, TD, 240 tracks, 38 spt, 512 bytes/sector, MFM */
    NEC_35_TD = 267,
    /** 5,25", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM */
    SHARP_525 = NEC_525_HD,
    /** 3,5", DS, HD, 80 tracks, 9 spt, 1024 bytes/sector, MFM */
    SHARP_525_9 = 268,
    /** 3,5", DS, HD, 77 tracks, 8 spt, 1024 bytes/sector, MFM */
    SHARP_35 = NEC_35_HD_8,
    /** 3,5", DS, HD, 80 tracks, 9 spt, 1024 bytes/sector, MFM */
    SHARP_35_9 = 269,
    // NEC/SHARP standard floppy formats, types 260 to 269

    // ECMA floppy standards, types 270 to 289
    /**
     *     5,25", DS, DD, 80 tracks, 8 spt, 1024 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128 bytes/sector, track
     *     0 side 1 = 26 sectors, 256 bytes/sector
     */
    ECMA_99_8 = 270,  /**
                       *     5,25", DS, DD, 77 tracks, 15 spt, 512 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                       * bytes/sector, track  0 side 1 = 26 sectors, 256 bytes/sector
                       */
    ECMA_99_15 = 271, /**
                       *     5,25", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, MFM, track 0 side 0 = 26 sectors, 128
                       * bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
                       */
    ECMA_99_26 = 272,
    /** 3,5", DS, DD, 80 tracks, 9 spt, 512 bytes/sector, MFM */
    ECMA_100 = DOS_35_DS_DD_9,
    /** 3,5", DS, HD, 80 tracks, 18 spt, 512 bytes/sector, MFM */
    ECMA_125 = DOS_35_HD,
    /** 3,5", DS, ED, 80 tracks, 36 spt, 512 bytes/sector, MFM */
    ECMA_147 = DOS_35_ED,
    /** 8", SS, SD, 77 tracks, 26 spt, 128 bytes/sector, FM */
    ECMA_54 = 273,
    /** 8", DS, SD, 77 tracks, 26 spt, 128 bytes/sector, FM */
    ECMA_59 = 274,
    /** 5,25", SS, DD, 35 tracks, 9 spt, 256 bytes/sector, FM, track 0 side 0 = 16 sectors, 128 bytes/sector */
    ECMA_66 = 275,    /**
                       *     8", DS, DD, 77 tracks, 8 spt, 1024 bytes/sector, FM, track 0 side 0 = 26 sectors, 128
                       * bytes/sector, track 0    side 1 = 26 sectors, 256 bytes/sector
                       */
    ECMA_69_8 = 276,  /**
                       *     8", DS, DD, 77 tracks, 15 spt, 512 bytes/sector, FM, track 0 side 0 = 26 sectors, 128
                       * bytes/sector, track 0  side 1 = 26 sectors, 256 bytes/sector
                       */
    ECMA_69_15 = 277, /**
                       *     8", DS, DD, 77 tracks, 26 spt, 256 bytes/sector, FM, track 0 side 0 = 26 sectors, 128
                       * bytes/sector, track 0 side 1 = 26 sectors, 256 bytes/sector
                       */
    ECMA_69_26 = 278, /**
                       *     5,25", DS, DD, 40 tracks, 16 spt, 256 bytes/sector, FM, track 0 side 0 = 16 sectors, 128
                       * bytes/sector, track 0 side 1 = 16 sectors, 256 bytes/sector
                       */
    ECMA_70 = 279,    /**
                       *     5,25", DS, DD, 80 tracks, 16 spt, 256 bytes/sector, FM, track 0 side 0 = 16 sectors, 128
                       * bytes/sector, track 0    side 1 = 16 sectors, 256 bytes/sector
                       */
    ECMA_78 = 280,
    /** 5,25", DS, DD, 80 tracks, 9 spt, 512 bytes/sector, FM */
    ECMA_78_2 = 281,
    // ECMA floppy standards, types 270 to 289

    // Non-standard PC formats (FDFORMAT, 2M, etc), types 290 to 308
    /** 5,25", DS, DD, 82 tracks, 10 spt, 512 bytes/sector, MFM */
    FDFORMAT_525_DD = 290,
    /** 5,25", DS, HD, 82 tracks, 17 spt, 512 bytes/sector, MFM */
    FDFORMAT_525_HD = 291,
    /** 3,5", DS, DD, 82 tracks, 10 spt, 512 bytes/sector, MFM */
    FDFORMAT_35_DD = 292,
    /** 3,5", DS, HD, 82 tracks, 21 spt, 512 bytes/sector, MFM */
    FDFORMAT_35_HD = 293,
    // Non-standard PC formats (FDFORMAT, 2M, etc), types 290 to 308

    // Apricot ACT standard floppy formats, type 309
    /** 3.5", DS, DD, 70 tracks, 9 spt, 512 bytes/sector, MFM */
    Apricot_35 = 309,
    // Apricot ACT standard floppy formats, type 309

    // OnStream ADR, types 310 to 319
    ADR2120 = 310,
    ADR260  = 311,
    ADR30   = 312,
    ADR50   = 313,
    // OnStream ADR, types 310 to 319

    // Advanced Intelligent Tape, types 320 to 339
    AIT1      = 320,
    AIT1Turbo = 321,
    AIT2      = 322,
    AIT2Turbo = 323,
    AIT3      = 324,
    AIT3Ex    = 325,
    AIT3Turbo = 326,
    AIT4      = 327,
    AIT5      = 328,
    AITETurbo = 329,
    SAIT1     = 330,
    SAIT2     = 331,
    // Advanced Intelligent Tape, types 320 to 339

    // Iomega, types 340 to 359
    Bernoulli  = 340,
    Bernoulli2 = 341,
    Ditto      = 342,
    DittoMax   = 343,
    Jaz        = 344,
    Jaz2       = 345,
    PocketZip  = 346,
    REV120     = 347,
    REV35      = 348,
    REV70      = 349,
    ZIP100     = 350,
    ZIP250     = 351,
    ZIP750     = 352,
    // Iomega, types 340 to 359

    // Audio or video media, types 360 to 369
    CompactCassette = 360,
    Data8           = 361,
    MiniDV          = 362,
    /** D/CAS-25: Digital data on Compact Cassette form factor, special magnetic media, 9-track */
    Dcas25 = 363,
    /** D/CAS-85: Digital data on Compact Cassette form factor, special magnetic media, 17-track */
    Dcas85 = 364,
    /** D/CAS-103: Digital data on Compact Cassette form factor, special magnetic media, 21-track */
    Dcas103 = 365,
    // Audio media, types 360 to 369

    // CompactFlash Association, types 370 to 379
    CFast             = 370,
    CompactFlash      = 371,
    CompactFlashType2 = 372,
    // CompactFlash Association, types 370 to 379

    // Digital Audio Tape / Digital Data Storage, types 380 to 389
    DigitalAudioTape = 380,
    DAT160           = 381,
    DAT320           = 382,
    DAT72            = 383,
    DDS1             = 384,
    DDS2             = 385,
    DDS3             = 386,
    DDS4             = 387,
    // Digital Audio Tape / Digital Data Storage, types 380 to 389

    // DEC, types 390 to 399
    CompactTapeI  = 390,
    CompactTapeII = 391,
    DECtapeII     = 392,
    DLTtapeIII    = 393,
    DLTtapeIIIxt  = 394,
    DLTtapeIV     = 395,
    DLTtapeS4     = 396,
    SDLT1         = 397,
    SDLT2         = 398,
    VStapeI       = 399,
    // DEC, types 390 to 399

    // Exatape, types 400 to 419
    Exatape15m    = 400,
    Exatape22m    = 401,
    Exatape22mAME = 402,
    Exatape28m    = 403,
    Exatape40m    = 404,
    Exatape45m    = 405,
    Exatape54m    = 406,
    Exatape75m    = 407,
    Exatape76m    = 408,
    Exatape80m    = 409,
    Exatape106m   = 410,
    Exatape160mXL = 411,
    Exatape112m   = 412,
    Exatape125m   = 413,
    Exatape150m   = 414,
    Exatape170m   = 415,
    Exatape225m   = 416,
    // Exatape, types 400 to 419

    // PCMCIA / ExpressCard, types 420 to 429
    ExpressCard34 = 420,
    ExpressCard54 = 421,
    PCCardTypeI   = 422,
    PCCardTypeII  = 423,
    PCCardTypeIII = 424,
    PCCardTypeIV  = 425,
    // PCMCIA / ExpressCard, types 420 to 429

    // SyQuest, types 430 to 449
    EZ135  = 430,
    EZ230  = 431,
    Quest  = 432,
    SparQ  = 433,
    SQ100  = 434,
    SQ200  = 435,
    SQ300  = 436,
    SQ310  = 437,
    SQ327  = 438,
    SQ400  = 439,
    SQ800  = 440,
    SQ1500 = 441,
    SQ2000 = 442,
    SyJet  = 443,
    // SyQuest, types 430 to 449

    // Nintendo, types 450 to 469
    FamicomGamePak        = 450,
    GameBoyAdvanceGamePak = 451,
    GameBoyGamePak        = 452,
    /** Nintendo GameCube Optical Disc */
    GOD                 = 453,
    N64DD               = 454,
    N64GamePak          = 455,
    NESGamePak          = 456,
    Nintendo3DSGameCard = 457,
    NintendoDiskCard    = 458,
    NintendoDSGameCard  = 459,
    NintendoDSiGameCard = 460,
    SNESGamePak         = 461,
    SNESGamePakUS       = 462,
    /** Nintendo Wii Optical Disc */
    WOD = 463,
    /** Nintendo Wii U Optical Disc */
    WUOD           = 464,
    SwitchGameCard = 465,
    // Nintendo, types 450 to 469

    // IBM Tapes, types 470 to 479
    IBM3470  = 470,
    IBM3480  = 471,
    IBM3490  = 472,
    IBM3490E = 473,
    IBM3592  = 474,
    // IBM Tapes, types 470 to 479

    // LTO Ultrium, types 480 to 509
    LTO      = 480,
    LTO2     = 481,
    LTO3     = 482,
    LTO3WORM = 483,
    LTO4     = 484,
    LTO4WORM = 485,
    LTO5     = 486,
    LTO5WORM = 487,
    LTO6     = 488,
    LTO6WORM = 489,
    LTO7     = 490,
    LTO7WORM = 491,
    // LTO Ultrium, types 480 to 509

    // MemoryStick, types 510 to 519
    MemoryStick       = 510,
    MemoryStickDuo    = 511,
    MemoryStickMicro  = 512,
    MemoryStickPro    = 513,
    MemoryStickProDuo = 514,
    // MemoryStick, types 510 to 519

    // SecureDigital, types 520 to 529
    microSD       = 520,
    miniSD        = 521,
    SecureDigital = 522,
    // SecureDigital, types 520 to 529

    // MultiMediaCard, types 530 to 539
    MMC       = 530,
    MMCmicro  = 531,
    RSMMC     = 532,
    MMCplus   = 533,
    MMCmobile = 534,
    // MultiMediaCard, types 530 to 539

    // SLR, types 540 to 569
    MLR1        = 540,
    MLR1SL      = 541,
    MLR3        = 542,
    SLR1        = 543,
    SLR2        = 544,
    SLR3        = 545,
    SLR32       = 546,
    SLR32SL     = 547,
    SLR4        = 548,
    SLR5        = 549,
    SLR5SL      = 550,
    SLR6        = 551,
    SLRtape7    = 552,
    SLRtape7SL  = 553,
    SLRtape24   = 554,
    SLRtape24SL = 555,
    SLRtape40   = 556,
    SLRtape50   = 557,
    SLRtape60   = 558,
    SLRtape75   = 559,
    SLRtape100  = 560,
    SLRtape140  = 561,
    // SLR, types 540 to 569

    // QIC, types 570 to 589
    QIC11   = 570,
    QIC120  = 571,
    QIC1350 = 572,
    QIC150  = 573,
    QIC24   = 574,
    QIC3010 = 575,
    QIC3020 = 576,
    QIC3080 = 577,
    QIC3095 = 578,
    QIC320  = 579,
    QIC40   = 580,
    QIC525  = 581,
    QIC80   = 582,
    // QIC, types 570 to 589

    // StorageTek tapes, types 590 to 609
    STK4480 = 590,
    STK4490 = 591,
    STK9490 = 592,
    T9840A  = 593,
    T9840B  = 594,
    T9840C  = 595,
    T9840D  = 596,
    T9940A  = 597,
    T9940B  = 598,
    T10000A = 599,
    T10000B = 600,
    T10000C = 601,
    T10000D = 602,
    // StorageTek tapes, types 590 to 609

    // Travan, types 610 to 619
    Travan    = 610,
    Travan1Ex = 611,
    Travan3   = 612,
    Travan3Ex = 613,
    Travan4   = 614,
    Travan5   = 615,
    Travan7   = 616,
    // Travan, types 610 to 619

    // VXA, types 620 to 629
    VXA1 = 620,
    VXA2 = 621,
    VXA3 = 622,
    // VXA, types 620 to 629

    // Magneto-optical, types 630 to 659
    /** 5,25", M.O., ??? sectors, 1024 bytes/sector, ECMA-153, ISO 11560 */
    ECMA_153 = 630,
    /** 5,25", M.O., ??? sectors, 512 bytes/sector, ECMA-153, ISO 11560 */
    ECMA_153_512 = 631,
    /** 3,5", M.O., 249850 sectors, 512 bytes/sector, ECMA-154, ISO 10090 */
    ECMA_154 = 632,
    /** 5,25", M.O., 904995 sectors, 512 bytes/sector, ECMA-183, ISO 13481 */
    ECMA_183_512 = 633,
    /** 5,25", M.O., 498526 sectors, 1024 bytes/sector, ECMA-183, ISO 13481 */
    ECMA_183 = 634,
    /** 5,25", M.O., 1128772 or 1163337 sectors, 512 bytes/sector, ECMA-183, ISO 13549 */
    ECMA_184_512 = 635,
    /** 5,25", M.O., 603466 or 637041 sectors, 1024 bytes/sector, ECMA-183, ISO 13549 */
    ECMA_184 = 636,
    /** 300mm, M.O., ??? sectors, 1024 bytes/sector, ECMA-189, ISO 13614 */
    ECMA_189 = 637,
    /** 300mm, M.O., ??? sectors, 1024 bytes/sector, ECMA-190, ISO 13403 */
    ECMA_190 = 638,
    /** 5,25", M.O., 936921 or 948770 sectors, 1024 bytes/sector, ECMA-195, ISO 13842 */
    ECMA_195 = 639,
    /** 5,25", M.O., 1644581 or 1647371 sectors, 512 bytes/sector, ECMA-195, ISO 13842 */
    ECMA_195_512 = 640,
    /** 3,5", M.O., 446325 sectors, 512 bytes/sector, ECMA-201, ISO 13963 */
    ECMA_201 = 641,
    /** 3,5", M.O., 429975 sectors, 512 bytes/sector, embossed, ISO 13963 */
    ECMA_201_ROM = 642,
    /** 3,5", M.O., 371371 sectors, 1024 bytes/sector, ECMA-223 */
    ECMA_223 = 643,
    /** 3,5", M.O., 694929 sectors, 512 bytes/sector, ECMA-223 */
    ECMA_223_512 = 644,
    /** 5,25", M.O., 1244621 sectors, 1024 bytes/sector, ECMA-238, ISO 15486 */
    ECMA_238 = 645,
    /** 3,5", M.O., 318988, 320332 or 321100 sectors, 2048 bytes/sector, ECMA-239, ISO 15498 */
    ECMA_239 = 646,
    /** 356mm, M.O., 14476734 sectors, 1024 bytes/sector, ECMA-260, ISO 15898 */
    ECMA_260 = 647,
    /** 356mm, M.O., 24445990 sectors, 1024 bytes/sector, ECMA-260, ISO 15898 */
    ECMA_260_Double = 648,
    /** 5,25", M.O., 1128134 sectors, 2048 bytes/sector, ECMA-280, ISO 18093 */
    ECMA_280 = 649,
    /** 300mm, M.O., 7355716 sectors, 2048 bytes/sector, ECMA-317, ISO 20162 */
    ECMA_317 = 650,
    /** 5,25", M.O., 1095840 sectors, 4096 bytes/sector, ECMA-322, ISO 22092 */
    ECMA_322 = 651,
    /** 5,25", M.O., 2043664 sectors, 2048 bytes/sector, ECMA-322, ISO 22092 */
    ECMA_322_2k = 652,
    /** 3,5", M.O., 605846 sectors, 2048 bytes/sector, Cherry Book, GigaMo, ECMA-351, ISO 17346 */
    GigaMo = 653,
    /** 3,5", M.O., 1063146 sectors, 2048 bytes/sector, Cherry Book 2, GigaMo 2, ECMA-353, ISO 22533 */
    GigaMo2 = 654,
    // Magneto-optical, types 630 to 659

    // Other floppy standards, types 660 to 689
    CompactFloppy = 660,
    DemiDiskette  = 661,
    /** 3.5", 652 tracks, 2 sides, 512 bytes/sector, Floptical, ECMA-207, ISO 14169 */
    Floptical    = 662,
    HiFD         = 663,
    QuickDisk    = 664,
    UHD144       = 665,
    VideoFloppy  = 666,
    Wafer        = 667,
    ZXMicrodrive = 668,
    // Other floppy standards, types 660 to 669

    // Miscellaneous, types 670 to 689
    BeeCard     = 670,
    Borsu       = 671,
    DataStore   = 672,
    DIR         = 673,
    DST         = 674,
    DTF         = 675,
    DTF2        = 676,
    Flextra3020 = 677,
    Flextra3225 = 678,
    HiTC1       = 679,
    HiTC2       = 680,
    LT1         = 681,
    MiniCard    = 872,
    Orb         = 683,
    Orb5        = 684,
    SmartMedia  = 685,
    xD          = 686,
    XQD         = 687,
    DataPlay    = 688,
    // Miscellaneous, types 670 to 689

    // Apple specific media, types 690 to 699
    AppleProfile   = 690,
    AppleWidget    = 691,
    AppleHD20      = 692,
    PriamDataTower = 693,
    Pippin         = 694,
    // Apple specific media, types 690 to 699

    // DEC hard disks, types 700 to 729
    /**
     *     2382 cylinders, 4 tracks/cylinder, 42 sectors/track, 128 words/sector, 32 bits/word, 512 bytes/sector,
     *     204890112 bytes
     */
    RA60 = 700,    /**
                    *     546 cylinders, 14 tracks/cylinder, 31 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector,    121325568 bytes
                    */
    RA80 = 701,    /**
                    *     1248 cylinders, 14 tracks/cylinder, 51 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector,    456228864 bytes
                    */
    RA81 = 702,    /**
                    *     302 cylinders, 4 tracks/cylinder, 42 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 25976832    bytes
                    */
    RC25 = 703,    /**
                    *     615 cylinders, 4 tracks/cylinder, 17 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 21411840    bytes
                    */
    RD31 = 704,    /**
                    *     820 cylinders, 6 tracks/cylinder, 17 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 42823680    bytes
                    */
    RD32 = 705,    /**
                    *     306 cylinders, 4 tracks/cylinder, 17 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 10653696    bytes
                    */
    RD51 = 706,    /**
                    *     480 cylinders, 7 tracks/cylinder, 18 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 30965760    bytes
                    */
    RD52 = 707,    /**
                    *     1024 cylinders, 7 tracks/cylinder, 18 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector,    75497472 bytes
                    */
    RD53 = 708,    /**
                    *     1225 cylinders, 8 tracks/cylinder, 18 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector,    159936000 bytes
                    */
    RD54 = 709,    /**
                    *     411 cylinders, 3 tracks/cylinder, 22 sectors/track, 256 words/sector, 16 bits/word, 512
                    * bytes/sector, 13888512    bytes
                    */
    RK06 = 710,    /**
                    *     411 cylinders, 3 tracks/cylinder, 20 sectors/track, 256 words/sector, 18 bits/word, 576
                    * bytes/sector, 14204160    bytes
                    */
    RK06_18 = 711, /**
                    *     815 cylinders, 3 tracks/cylinder, 22 sectors/track, 256 words/sector, 16 bits/word, 512
                    * bytes/sector, 27540480 bytes
                    */
    RK07 = 712,    /**
                    *     815 cylinders, 3 tracks/cylinder, 20 sectors/track, 256 words/sector, 18 bits/word, 576
                    * bytes/sector, 28166400    bytes
                    */
    RK07_18 = 713, /**
                    *     823 cylinders, 5 tracks/cylinder, 32 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 67420160 bytes
                    */
    RM02 = 714,    /**
                    *     823 cylinders, 5 tracks/cylinder, 32 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 67420160    bytes
                    */
    RM03 = 715,    /**
                    *     823 cylinders, 19 tracks/cylinder, 32 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector,    256196608 bytes
                    */
    RM05 = 716,    /**
                    *     203 cylinders, 10 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector,    22865920 bytes
                    */
    RP02 = 717,    /**
                    *     203 cylinders, 10 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    * bytes/sector,    23385600 bytes
                    */
    RP02_18 = 718, /**
                    *     400 cylinders, 10 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 45056000 bytes
                    */
    RP03 = 719,    /**
                    *     400 cylinders, 10 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    * bytes/sector,    46080000 bytes
                    */
    RP03_18 = 720, /**
                    *     411 cylinders, 19 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 87960576 bytes
                    */
    RP04 = 721,    /**
                    *     411 cylinders, 19 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    * bytes/sector,    89959680 bytes
                    */
    RP04_18 = 722, /**
                    *     411 cylinders, 19 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 87960576 bytes
                    */
    RP05 = 723,    /**
                    *     411 cylinders, 19 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    * bytes/sector,    89959680 bytes
                    */
    RP05_18 = 724, /**
                    *     815 cylinders, 19 tracks/cylinder, 22 sectors/track, 128 words/sector, 32 bits/word, 512
                    * bytes/sector, 174423040 bytes
                    */
    RP06 = 725,    /**
                    *     815 cylinders, 19 tracks/cylinder, 20 sectors/track, 128 words/sector, 36 bits/word, 576
                    * bytes/sector,    178387200 bytes
                    */
    RP06_18 = 726,
    // DEC hard disks, types 700 to 729

    // Imation, types 730 to 739
    LS120  = 730,
    LS240  = 731,
    FD32MB = 732,
    RDX    = 733,
    /** Imation 320Gb RDX */
    RDX320 = 734,
    // Imation, types 730 to 739

    // VideoNow, types 740 to 749
    VideoNow      = 740,
    VideoNowColor = 741,
    VideoNowXp    = 742
    //
} MediaType;

/**
 *      Contains information about a dump image and its contents
 */
typedef struct ImageInfo
{
    /** Image contains partitions (or tracks for optical media) */
    uint8_t HasPartitions;
    /** Image contains sessions (optical media only) */
    uint8_t HasSessions;
    /** Size of the image without headers */
    uint64_t ImageSize;
    /** Sectors contained in the image */
    uint64_t Sectors;
    /** Size of sectors contained in the image */
    uint32_t SectorSize;
    /** Media tags contained by the image */
    // List<MediaTagType> ReadableMediaTags;
    /** Sector tags contained by the image */
    // List<SectorTagType> ReadableSectorTags;
    /** Image version */
    uint8_t* Version;
    /** Application that created the image */
    uint8_t* Application;
    /** Version of the application that created the image */
    uint8_t* ApplicationVersion;
    /** Who (person) created the image? */
    uint8_t* Creator;
    /** Image creation time */
    int64_t CreationTime;
    /** Image last modification time */
    int64_t LastModificationTime;
    /** Title of the media represented by the image */
    uint8_t* MediaTitle;
    /** Image comments */
    uint8_t* Comments;
    /** Manufacturer of the media represented by the image */
    uint8_t* MediaManufacturer;
    /** Model of the media represented by the image */
    uint8_t* MediaModel;
    /** Serial number of the media represented by the image */
    uint8_t* MediaSerialNumber;
    /** Barcode of the media represented by the image */
    uint8_t* MediaBarcode;
    /** Part number of the media represented by the image */
    uint8_t* MediaPartNumber;
    /** Media type represented by the image */
    uint32_t MediaType;
    /** Number in sequence for the media represented by the image */
    int32_t MediaSequence;
    /** Last media of the sequence the media represented by the image corresponds to */
    int32_t LastMediaSequence;
    /** Manufacturer of the drive used to read the media represented by the image */
    uint8_t* DriveManufacturer;
    /** Model of the drive used to read the media represented by the image */
    uint8_t* DriveModel;
    /** Serial number of the drive used to read the media represented by the image */
    uint8_t* DriveSerialNumber;
    /** Firmware revision of the drive used to read the media represented by the image */
    uint8_t* DriveFirmwareRevision;
    /** Type of the media represented by the image to use in XML sidecars */
    uint8_t XmlMediaType;
    // CHS geometry...
    /** Cylinders of the media represented by the image */
    uint32_t Cylinders;
    /** Heads of the media represented by the image */
    uint32_t Heads;
    /** Sectors per track of the media represented by the image (for variable image, the smallest) */
    uint32_t SectorsPerTrack;
} ImageInfo;

/**
 *     Metadata present for each sector (aka, "tag").
 */
typedef enum
{
    AppleSectorTag        = 0,  /** Apple's GCR sector tags, 12 bytes */
    CdSectorSync          = 1,  /** Sync frame from CD sector, 12 bytes */
    CdSectorHeader        = 2,  /** CD sector header, 4 bytes */
    CdSectorSubHeader     = 3,  /** CD mode 2 sector subheader */
    CdSectorEdc           = 4,  /** CD sector EDC, 4 bytes */
    CdSectorEccP          = 5,  /** CD sector ECC P, 172 bytes */
    CdSectorEccQ          = 6,  /** CD sector ECC Q, 104 bytes */
    CdSectorEcc           = 7,  /** CD sector ECC (P and Q), 276 bytes */
    CdSectorSubchannelDic = 8,  /** CD sector subchannel, 96 bytes */
    CdTrackIsrc           = 9,  /** CD track ISRC, string, 12 bytes */
    CdTrackText           = 10, /** CD track text, string, 13 bytes */
    CdTrackFlags          = 11, /** CD track flags, 1 byte */
    DvdCmi                = 12, /** DVD sector copyright information */
    FloppyAddressMark     = 13, /** Floppy address mark (contents depend on underlying floppy format) */
    MaxSectorTag          = FloppyAddressMark
} SectorTagType;

/*
 *     Metadata present for each media.
 */
typedef enum
{
    /* CD table of contents */
    CD_TOC                        = 0,  /* CD session information */
    CD_SessionInfo                = 1,  /* CD full table of contents */
    CD_FullTOC                    = 2,  /* CD PMA */
    CD_PMA                        = 3,  /* CD Adress-Time-In-Pregroove */
    CD_ATIP                       = 4,  /* CD-Text */
    CD_TEXT                       = 5,  /* CD Media Catalogue Number */
    CD_MCN                        = 6,  /* DVD/HD DVD Physical Format Information */
    DVD_PFI                       = 7,  /* DVD Lead-in Copyright Management Information */
    DVD_CMI                       = 8,  /* DVD disc key */
    DVD_DiscKey                   = 9,  /* DVD/HD DVD Burst Cutting Area */
    DVD_BCA                       = 10, /* DVD/HD DVD Lead-in Disc Manufacturer Information */
    DVD_DMI                       = 11, /* Media identifier */
    DVD_MediaIdentifier           = 12, /* Media key block */
    DVD_MKB                       = 13, /* DVD-RAM/HD DVD-RAM DDS information */
    DVDRAM_DDS                    = 14, /* DVD-RAM/HD DVD-RAM Medium status */
    DVDRAM_MediumStatus           = 15, /* DVD-RAM/HD DVD-RAM Spare area information */
    DVDRAM_SpareArea              = 16, /* DVD-R/-RW/HD DVD-R RMD in last border-out */
    DVDR_RMD                      = 17, /* Pre-recorded information from DVD-R/-RW lead-in */
    DVDR_PreRecordedInfo          = 18, /* DVD-R/-RW/HD DVD-R media identifier */
    DVDR_MediaIdentifier          = 19, /* DVD-R/-RW/HD DVD-R physical format information */
    DVDR_PFI                      = 20, /* ADIP information */
    DVD_ADIP                      = 21, /* HD DVD Lead-in copyright protection information */
    HDDVD_CPI                     = 22, /* HD DVD-R Medium Status */
    HDDVD_MediumStatus            = 23, /* DVD+/-R DL Layer capacity */
    DVDDL_LayerCapacity           = 24, /* DVD-R DL Middle Zone start address */
    DVDDL_MiddleZoneAddress       = 25, /* DVD-R DL Jump Interval Size */
    DVDDL_JumpIntervalSize        = 26, /* DVD-R DL Start LBA of the manual layer jump */
    DVDDL_ManualLayerJumpLBA      = 27, /* Blu-ray Disc Information */
    BD_DI                         = 28, /* Blu-ray Burst Cutting Area */
    BD_BCA                        = 29, /* Blu-ray Disc Definition Structure */
    BD_DDS                        = 30, /* Blu-ray Cartridge Status */
    BD_CartridgeStatus            = 31, /* Blu-ray Status of Spare Area */
    BD_SpareArea                  = 32, /* AACS volume identifier */
    AACS_VolumeIdentifier         = 33, /* AACS pre-recorded media serial number */
    AACS_SerialNumber             = 34, /* AACS media identifier */
    AACS_MediaIdentifier          = 35, /* Lead-in AACS media key block */
    AACS_MKB                      = 36, /* AACS data keys */
    AACS_DataKeys                 = 37, /* LBA extents flagged for bus encryption by AACS */
    AACS_LBAExtents               = 38, /* CPRM media key block in Lead-in */
    AACS_CPRM_MKB                 = 39, /* Recognized layer formats in hybrid discs */
    Hybrid_RecognizedLayers       = 40, /* Disc write protection status */
    MMC_WriteProtection           = 41, /* Disc standard information */
    MMC_DiscInformation           = 42, /* Disc track resources information */
    MMC_TrackResourcesInformation = 43, /* BD-R Pseudo-overwrite information */
    MMC_POWResourcesInformation   = 44, /* SCSI INQUIRY response */
    SCSI_INQUIRY                  = 45, /* SCSI MODE PAGE 2Ah */
    SCSI_MODEPAGE_2A              = 46, /* ATA IDENTIFY DEVICE response */
    ATA_IDENTIFY                  = 47, /* ATA IDENTIFY PACKET DEVICE response */
    ATAPI_IDENTIFY                = 48, /* PCMCIA/CardBus Card Information Structure */
    PCMCIA_CIS                    = 49, /* SecureDigital CID */
    SD_CID                        = 50, /* SecureDigital CSD */
    SD_CSD                        = 51, /* SecureDigital SCR */
    SD_SCR                        = 52, /* SecureDigital OCR */
    SD_OCR                        = 53, /* MultiMediaCard CID */
    MMC_CID                       = 54, /* MultiMediaCard CSD */
    MMC_CSD                       = 55, /* MultiMediaCard OCR */
    MMC_OCR                       = 56, /* MultiMediaCard Extended CSD */
    MMC_ExtendedCSD               = 57, /* Xbox Security Sector */
    Xbox_SecuritySector           = 58, /*
                                         *     On floppy disks, data in last cylinder usually in a different format that contains
                                         * duplication or           manufacturing information
                                         */
    Floppy_LeadOut      = 59,           /* DVD Disc Control Blocks */
    DCB                 = 60,           /* Compact Disc First Track Pregap */
    CD_FirstTrackPregap = 61,           /* Compact Disc Lead-out */
    CD_LeadOut          = 62,           /* SCSI MODE SENSE (6) */
    SCSI_MODESENSE_6    = 63,           /* SCSI MODE SENSE (10) */
    SCSI_MODESENSE_10   = 64,           /* USB descriptors */
    USB_Descriptors     = 65,           /* XGD unlocked DMI */
    Xbox_DMI            = 66,           /* XDG unlocked PFI */
    Xbox_PFI            = 67,           /* Compact Disc Lead-in */
    CD_LeadIn           = 68
} MediaTagType;

#endif // LIBAARUFORMAT_AARU_H

#pragma clang diagnostic pop