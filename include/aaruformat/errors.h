/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
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

#ifndef LIBAARUFORMAT_ERRORS_H
#define LIBAARUFORMAT_ERRORS_H

#define AARUF_ERROR_NOT_AARUFORMAT -1
#define AARUF_ERROR_FILE_TOO_SMALL -2
#define AARUF_ERROR_INCOMPATIBLE_VERSION -3
#define AARUF_ERROR_CANNOT_READ_INDEX -4
#define AARUF_ERROR_SECTOR_OUT_OF_BOUNDS -5
#define AARUF_ERROR_CANNOT_READ_HEADER -6
#define AARUF_ERROR_CANNOT_READ_BLOCK -7
#define AARUF_ERROR_UNSUPPORTED_COMPRESSION -8
#define AARUF_ERROR_NOT_ENOUGH_MEMORY -9
#define AARUF_ERROR_BUFFER_TOO_SMALL -10
#define AARUF_ERROR_MEDIA_TAG_NOT_PRESENT -11
#define AARUF_ERROR_INCORRECT_MEDIA_TYPE -12
#define AARUF_ERROR_TRACK_NOT_FOUND -13
#define AARUF_ERROR_REACHED_UNREACHABLE_CODE -14
#define AARUF_ERROR_INVALID_TRACK_FORMAT -15
#define AARUF_ERROR_SECTOR_TAG_NOT_PRESENT -11

#define AARUF_STATUS_OK 0
#define AARUF_STATUS_SECTOR_NOT_DUMPED 1
#define AARUF_STATUS_SECTOR_WITH_ERRORS 2
#define AARUF_STATUS_SECTOR_DELETED 3

#endif // LIBAARUFORMAT_ERRORS_H
