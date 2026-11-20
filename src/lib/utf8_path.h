/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * UTF-8 aware file opening.
 */

#ifndef LIBAARUFORMAT_UTF8_PATH_H
#define LIBAARUFORMAT_UTF8_PATH_H

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Opens a file whose path is encoded in UTF-8.
     *
     * On POSIX systems this is a plain fopen(), as paths are already byte strings. On Windows the
     * narrow CRT interprets paths in the ANSI code page, so the path and mode are converted from
     * UTF-8 to UTF-16 and opened with _wfopen(). If the path is not valid UTF-8 it is passed to
     * fopen() unchanged, so callers using the ANSI code page keep working.
     *
     * @param path UTF-8 encoded path of the file to open.
     * @param mode fopen() mode string.
     * @return Opened stream, or NULL with errno set on failure.
     */
    FILE *aaruf_fopen_utf8(const char *path, const char *mode);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_UTF8_PATH_H */
