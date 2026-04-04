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

#include <errno.h>
#include <stdio.h>

#include <aaruformat.h>

#include "internal.h"

/**
 * @brief Identifies a file as an AaruFormat image using a file path.
 *
 * Opens the file at the given path and determines if it is an AaruFormat image by examining
 * the file header for valid AaruFormat signatures and version information. This function
 * provides a simple file-based interface that handles file opening, identification, and
 * cleanup automatically. It delegates the actual identification logic to aaruf_identify_stream().
 *
 * @param filename Path to the file to identify (must be accessible and readable).
 *
 * @return Returns one of the following values:
 * @retval 100 Maximum confidence - Definitive AaruFormat image. This is returned when:
 *         - The file header contains a valid AaruFormat signature (DIC_MAGIC or AARU_MAGIC)
 *         - The image major version is less than or equal to the supported version (AARUF_VERSION)
 *         - The file structure passes all header validation checks
 *         - This indicates the file is definitely a supported AaruFormat image
 *
 * @retval 0 Not recognized - File is not an AaruFormat image. This is returned when:
 *         - The file header doesn't contain a recognized AaruFormat signature
 *         - The image major version exceeds the maximum supported version
 *         - The file header cannot be read completely (file too small or corrupted)
 *         - The file format doesn't match AaruFormat specifications
 *
 * @retval Positive errno values - File access errors from system calls. Common values include:
 *         - ENOENT (2) - File not found or path doesn't exist
 *         - EACCES (13) - Permission denied, file not readable
 *         - EISDIR (21) - Path refers to a directory, not a file
 *         - EMFILE (24) - Too many open files (process limit reached)
 *         - ENFILE (23) - System limit on open files reached
 *         - ENOMEM (12) - Insufficient memory to open file
 *         - EIO (5) - I/O error occurred during file access
 *         - Other platform-specific errno values from fopen()
 *
 * @note Identification Process:
 *       - Opens the file in binary read mode ("rb")
 *       - Delegates identification to aaruf_identify_stream() for actual header analysis
 *       - Automatically closes the file stream regardless of identification result
 *       - Returns system errno values directly if file opening fails
 *
 * @note Confidence Scoring:
 *       - Uses binary scoring: 100 (definitive match) or 0 (no match)
 *       - No intermediate confidence levels are returned
 *       - Designed for simple yes/no identification rather than probabilistic matching
 *
 * @note Version Compatibility:
 *       - Only recognizes AaruFormat versions up to AARUF_VERSION
 *       - Future versions beyond library support are treated as unrecognized
 *       - Backwards compatible with older DIC_MAGIC identifiers
 *
 * @warning The function opens and closes the file for each identification.
 *          For repeated operations on the same file, consider using aaruf_identify_stream()
 *          with a pre-opened stream for better performance.
 *
 * @warning File access permissions and availability are checked at runtime.
 *          Network files or files on removable media may cause variable access times.
 *
 * @warning The function performs minimal file content validation. A positive result
 *          indicates the file appears to be AaruFormat but doesn't guarantee the
 *          entire file is valid or uncorrupted.
 */
AARU_EXPORT int AARU_CALL aaruf_identify(const char *filename)
{
    if(filename == NULL) return EINVAL;

    FILE *stream = NULL;

    stream = fopen(filename, "rb");

    if(stream == NULL) return errno;

    const int ret = aaruf_identify_stream(stream);

    fclose(stream);

    return ret;
}

/**
 * @brief Identifies a file as an AaruFormat image using an open stream.
 *
 * Determines if the provided stream is an AaruFormat image by reading and validating
 * the file header at the beginning of the stream. This function performs the core
 * identification logic by checking for valid AaruFormat signatures and version
 * compatibility. It's designed to work with any FILE stream, making it suitable
 * for integration with existing file handling code.
 *
 * @param image_stream Open FILE stream positioned at any location (will be repositioned to start).
 *
 * @return Returns one of the following values:
 * @retval 100 Maximum confidence - Definitive AaruFormat image. This is returned when:
 *         - The stream is successfully repositioned to the beginning
 *         - The AaruFormat header is successfully read (AaruHeader structure)
 *         - The header identifier matches either DIC_MAGIC or AARU_MAGIC (valid signatures)
 *         - The image major version is less than or equal to AARUF_VERSION (supported version)
 *         - All validation checks pass indicating a compatible AaruFormat image
 *
 * @retval 0 Not recognized - Stream is not an AaruFormat image. This is returned when:
 *         - The stream parameter is NULL
 *         - Cannot read a complete AaruHeader structure from the stream (file too small)
 *         - The header identifier doesn't match DIC_MAGIC or AARU_MAGIC (wrong format)
 *         - The image major version exceeds AARUF_VERSION (unsupported future version)
 *         - Any validation check fails indicating the stream is not a valid AaruFormat image
 *
 * @note Stream Handling:
 *       - Always seeks to position 0 at the beginning of the function
 *       - Reads exactly one AaruHeader structure (size depends on format version)
 *       - Does not restore the original stream position after identification
 *       - Stream remains open and positioned after the header on function return
 *
 * @note Signature Recognition:
 *       - DIC_MAGIC: Legacy identifier from original DiscImageChef format
 *       - AARU_MAGIC: Current AaruFormat identifier
 *       - Both signatures are accepted for backwards compatibility
 *       - Signature validation is performed using exact byte matching
 *
 * @note Version Validation:
 *       - Only checks the major version number for compatibility
 *       - Minor version differences are ignored (assumed backwards compatible)
 *       - Future major versions are rejected to prevent compatibility issues
 *       - Version check prevents attempting to read unsupported format variants
 *
 * @note Confidence Scoring:
 *       - Binary result: 100 (definitive) or 0 (not recognized)
 *       - No probabilistic or partial matching
 *       - Designed for definitive identification rather than format detection
 *
 * @warning The function modifies the stream position by seeking to the beginning
 *          and reading the header. The stream position is not restored.
 *
 * @warning This function performs only header-level validation. A positive result
 *          indicates the file appears to have a valid AaruFormat header but doesn't
 *          guarantee the entire image structure is valid or uncorrupted.
 *
 * @warning The stream must support seeking operations. Non-seekable streams
 *          (like pipes or network streams) may cause undefined behavior.
 *
 * @warning No error codes are returned for I/O failures during header reading.
 *          Such failures result in a return value of 0 (not recognized).
 */
AARU_EXPORT int AARU_CALL aaruf_identify_stream(FILE *image_stream)
{
    if(image_stream == NULL) return 0;

    if(aaruf_fseek(image_stream, 0, SEEK_SET) != 0) return 0;

    AaruHeader header;

    const size_t ret = fread(&header, sizeof(AaruHeader), 1, image_stream);

    if(ret != 1) return 0;

    if((header.identifier == DIC_MAGIC || header.identifier == AARU_MAGIC) && header.imageMajorVersion <= AARUF_VERSION)
        return 100;

    return 0;
}