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

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "utf8_path.h"

#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Converts a NUL-terminated UTF-8 string to a newly allocated UTF-16 string.
 *
 * @param utf8 String to convert.
 * @param out  Receives the converted string, to be released with free().
 * @return 0 on success, EILSEQ if the input is not valid UTF-8, ENOMEM if allocation failed.
 */
static int utf8_to_wide(const char *utf8, wchar_t **out)
{
    *out = NULL;

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if(length <= 0) return EILSEQ;

    wchar_t *wide = malloc((size_t)length * sizeof(wchar_t));
    if(wide == NULL) return ENOMEM;

    if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, length) <= 0)
    {
        free(wide);
        return EILSEQ;
    }

    *out = wide;
    return 0;
}
#endif

FILE *aaruf_fopen_utf8(const char *path, const char *mode)
{
    if(path == NULL || mode == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

#if defined(_WIN32) || defined(_WIN64)
    wchar_t *wide_path = NULL;
    wchar_t *wide_mode = NULL;

    int ret = utf8_to_wide(path, &wide_path);

    // Not UTF-8, assume the caller used the ANSI code page
    if(ret == EILSEQ) return fopen(path, mode);

    if(ret == 0) ret = utf8_to_wide(mode, &wide_mode);

    if(ret != 0)
    {
        free(wide_path);
        errno = ret == ENOMEM ? ENOMEM : EINVAL;
        return NULL;
    }

    FILE     *stream    = _wfopen(wide_path, wide_mode);
    const int saved_err = errno;

    free(wide_path);
    free(wide_mode);

    errno = saved_err;
    return stream;
#else
    return fopen(path, mode);
#endif
}
