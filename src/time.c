/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>

uint64_t get_filetime_uint64()
{
    FILETIME   ft;
    SYSTEMTIME st;
    GetSystemTime(&st);  // UTC time
    SystemTimeToFileTime(&st, &ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

#else
#include <sys/time.h>

uint64_t get_filetime_uint64()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);  // seconds + microseconds since 1970

    const uint64_t EPOCH_DIFF = 11644473600ULL;  // seconds between 1601 and 1970
    uint64_t       ft         = (tv.tv_sec + EPOCH_DIFF) * 10000000ULL + tv.tv_usec * 10;
    return ft;
}
#endif
