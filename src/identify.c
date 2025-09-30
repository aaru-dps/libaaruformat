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

#include <errno.h>
#include <stdio.h>

#include <aaruformat.h>

/**
 * @brief Identifies a file as an AaruFormat image using a file path.
 *
 * Opens the file at the given path and determines if it is an AaruFormat image.
 *
 * @param filename Path to the file to identify.
 * @return If positive, confidence value (100 = maximum confidence, 0 = not recognized). If negative, error value.
 */
int aaruf_identify(const char *filename)
{
    FILE *stream = NULL;

    stream = fopen(filename, "rb");

    if(stream == NULL) return errno;

    int ret = aaruf_identify_stream(stream);

    fclose(stream);

    return ret;
}

/**
 * @brief Identifies a file as an AaruFormat image using an open stream.
 *
 * Determines if the provided stream is an AaruFormat image.
 *
 * @param imageStream Stream of the file to identify.
 * @return If positive, confidence value (100 = maximum confidence, 0 = not recognized). If negative, error value.
 */
int aaruf_identify_stream(FILE *imageStream)
{
    fseek(imageStream, 0, SEEK_SET);

    AaruHeader header;

    size_t ret = fread(&header, sizeof(AaruHeader), 1, imageStream);

    if(ret != 1) return 0;

    if((header.identifier == DIC_MAGIC || header.identifier == AARU_MAGIC) && header.imageMajorVersion <= AARUF_VERSION)
        return 100;

    return 0;
}