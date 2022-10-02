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

#include <errno.h>
#include <stdio.h>

#include <aaruformat.h>

//! Identifies a file as aaruformat, using path
/*!
 *
 * @param filename path to the file to aaruf_identify
 * @return If positive, confidence value, with 100 being maximum confidentiality, and 0 not recognizing the file.
 * If negative, error value
 */
int aaruf_identify(const char* filename)
{
    FILE* stream;

    stream = fopen(filename, "rb");

    if(stream == NULL) return errno;

    int ret = aaruf_identify_stream(stream);

    fclose(stream);

    return ret;
}

//! Identifies a file as aaruformat, using an already existing stream
/*!
 *
 * @param imageStream stream of the file to aaruf_identify
 * @return If positive, confidence value, with 100 being maximum confidentiality, and 0 not recognizing the file.
 * If negative, error value
 */
int aaruf_identify_stream(FILE* imageStream)
{
    fseek(imageStream, 0, SEEK_SET);

    AaruHeader header;

    size_t ret = fread(&header, sizeof(AaruHeader), 1, imageStream);

    if(ret != 1) return 0;

    if((header.identifier == DIC_MAGIC || header.identifier == AARU_MAGIC) && header.imageMajorVersion <= AARUF_VERSION)
        return 100;

    return 0;
}