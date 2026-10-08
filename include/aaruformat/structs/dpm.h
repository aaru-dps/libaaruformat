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

#ifndef LIBAARUFORMAT_DPM_H
#define LIBAARUFORMAT_DPM_H

#include <stdint.h> /* Fixed-width integer types for on-disk packed structures */

#pragma pack(push, 1)

/** \file aaruformat/structs/dpm.h
 *  \brief Packed on-disk structures of the Data Position Measurement (DPM) block.
 *
 *  Data Position Measurement records the physical angle of sectors along the spiral track of an optical disc, as
 *  measured by timing reads. The angle is cumulative: it starts at 0 on the first entry and grows by one full
 *  turn every \ref DPM_UNITS_PER_TURN units, so it also counts how many times the disc turned.
 *
 *  Binary layout (little-endian, packed):
 *
 *    DpmHeader (sizeof = 40 bytes)
 *    uint64_t layerEnd[layerEnds]           -> last sector of every layer but the last, increasing
 *    DpmEntry entry[entries]                -> control points, strictly increasing sector
 *    DpmCalibration calibration[calibrations]
 *
 *  length = layerEnds * 8 + entries * sizeof(DpmEntry) + calibrations * sizeof(DpmCalibration)
 *  crc64  = CRC64-ECMA of the length bytes that follow the header
 *
 *  The density, in degrees per sector, of the bin between two consecutive entries is
 *  (angle[i + 1] - angle[i]) * 360 / DPM_UNITS_PER_TURN / (lba[i + 1] - lba[i]).
 *
 *  Alcohol 120% stores the same cumulative angle, in 256 units per turn, on a uniform grid. A value of this block
 *  divided by \ref DPM_UNITS_PER_ALCOHOL_UNIT, rounded, is the Alcohol 120% value.
 *
 *  \warning Structures are packed; never rely on natural alignment when mapping from a byte buffer.
 */

/** Angle units per turn, Alcohol 120% units (256 per turn) multiplied by 10000. */
#define DPM_UNITS_PER_TURN 2560000
/** Angle units per Alcohol 120% unit. */
#define DPM_UNITS_PER_ALCOHOL_UNIT 10000

/** \brief Bit set in DpmHeader::flags when the layers use opposite track path. */
#define DPM_FLAG_OPPOSITE_TRACK_PATH 0x01

/** \enum DpmEntryStatus
 *  \brief Status of the bin of sectors that ends at an entry. Ignored for the first entry.
 */
typedef enum
{
    DpmStatusUnknown      = 0,  ///< Unknown, e.g. imported from a format that does not record it.
    DpmStatusMeasured     = 1,  ///< Measured on the first attempt.
    DpmStatusRemeasured   = 2,  ///< Measured again because the first measurement was an outlier.
    DpmStatusInterpolated = 3,  ///< Measurement failed, interpolated from its neighbours.
    DpmStatusUnreadable   = 4,  ///< Sectors could not be read, interpolated from its neighbours.
    DpmStatusLayerBreak   = 5   ///< Bin crosses a layer boundary or is too short, density copied from the bin before.
} DpmEntryStatus;

/** \struct DpmHeader
 *  \brief Header of the Data Position Measurement block.
 */
typedef struct DpmHeader
{
    uint32_t identifier;      ///< Block identifier, must be BlockType::DataPositionMeasurementBlock.
    uint32_t entries;         ///< Number of DpmEntry records.
    uint32_t calibrations;    ///< Number of DpmCalibration records.
    uint32_t nominalSpacing;  ///< Nominal number of sectors between entries.
    uint16_t timingUnit;      ///< Sectors per timing unit (1 on CD, 16 on DVD, 32 on BD), 0 if unknown.
    uint16_t speed;           ///< Requested speed, as a multiple of the medium base speed. 0xFFFF maximum, 0 unknown.
    uint8_t  layerEnds;       ///< Number of layer boundaries (layers - 1), 0 for single layer media or if unknown.
    uint8_t  flags;           ///< Flags, see DPM_FLAG_OPPOSITE_TRACK_PATH.
    uint16_t reserved;        ///< Reserved, must be 0.
    uint64_t length;          ///< Payload bytes after this header.
    uint64_t crc64;           ///< CRC64-ECMA of the payload.
} DpmHeader;

/** \struct DpmEntry
 *  \brief A DPM control point.
 */
typedef struct DpmEntry
{
    uint64_t lba;     ///< Sector address.
    uint64_t angle;   ///< Cumulative angle since the first entry, in DPM_UNITS_PER_TURN units per turn.
    uint8_t  status;  ///< DpmEntryStatus of the bin that ends at this entry.
} DpmEntry;

/** \struct DpmCalibration
 *  \brief A rotation calibration done while measuring.
 */
typedef struct DpmCalibration
{
    uint64_t lba;                  ///< Sector address where the calibration was done.
    uint64_t rotationPeriod;       ///< Rotation period, in nanoseconds.
    uint64_t sectorsPerTurnX1000;  ///< Sectors per rotation at lba, multiplied by 1000.
} DpmCalibration;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_DPM_H
