/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <errno.h>
#include <locale.h>
#include <unicode/ucnv.h>

#include <aaruformat.h>

int info(char* path)
{
    aaruformatContext* ctx;
    char*              strBuffer;
    UErrorCode         u_error_code;
    uint               i, j;

    ctx = aaruf_open(path);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.", errno);
        return errno;
    }

    printf("AaruFormat context information:\n");
    printf("Magic number: %8.8s\n", (char*)&ctx->magic);
    printf("Library version: %d.%d\n", ctx->libraryMajorVersion, ctx->libraryMinorVersion);
    printf("AaruFormat header:\n");
    printf("\tIdentifier: %8.8s\n", (char*)&ctx->header.identifier);

    strBuffer = malloc(65);
    memset(strBuffer, 0, 65);
    ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, (const char*)ctx->header.application, 64, &u_error_code);
    if(u_error_code == U_ZERO_ERROR) printf("\tApplication: %s\n", strBuffer);
    free(strBuffer);

    printf("\tApplication version: %d.%d\n", ctx->header.applicationMinorVersion, ctx->header.applicationMajorVersion);
    printf("\tImage format version: %d.%d\n", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    printf("\tMedia type: %d\n", ctx->header.mediaType);
    printf("\tIndex offset: %lu\n", ctx->header.indexOffset);
    printf("\tCreation time: %ld\n", ctx->header.creationTime);
    printf("\tLast written time: %ld\n", ctx->header.lastWrittenTime);

    // TODO: Traverse media tags

    if(ctx->sectorPrefix != NULL) printf("Sector prefix array has been read.\n");

    if(ctx->sectorPrefixCorrected != NULL) printf("Sector prefix corrected array has been read.\n");

    if(ctx->sectorSuffix != NULL) printf("Sector suffix array has been read.\n");

    if(ctx->sectorSuffixCorrected != NULL) printf("Sector suffix corrected array has been read.\n");

    if(ctx->sectorSubchannel != NULL) printf("Sector subchannel array has been read.\n");

    if(ctx->mode2Subheaders != NULL) printf("Sector mode 2 subheaders array has been read.\n");

    printf("Shift is %d (%d bytes).\n", ctx->shift, 1 << ctx->shift);

    if(ctx->inMemoryDdt) printf("User-data DDT resides in memory.\n");

    if(ctx->userDataDdt != NULL) printf("User-data DDT has been read to memory.\n");

    if(ctx->mappedMemoryDdtSize > 0) printf("Mapped memory DDT has %zu bytes", ctx->mappedMemoryDdtSize);

    if(ctx->sectorPrefixDdt != NULL) printf("Sector prefix DDT has been read to memory.\n");

    if(ctx->sectorPrefixDdt != NULL) printf("Sector suffix DDT has been read to memory.\n");

    if(ctx->geometryBlock.identifier == GeometryBlock)
        printf("Media has %d cylinders, %d heads and %d sectors per track.\n",
               ctx->geometryBlock.cylinders,
               ctx->geometryBlock.heads,
               ctx->geometryBlock.sectorsPerTrack);

    if(ctx->metadataBlockHeader.identifier == MetadataBlock)
    {
        printf("Metadata block:\n");
        if(ctx->metadataBlockHeader.mediaSequence > 0)
            printf("\tMedia is no. %d in a set of %d media\n",
                   ctx->metadataBlockHeader.mediaSequence,
                   ctx->metadataBlockHeader.lastMediaSequence);

        if(ctx->metadataBlockHeader.creatorLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.creatorLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.creatorLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.creatorLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.creatorOffset),
                         (int)ctx->metadataBlockHeader.creatorLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.commentsLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.commentsLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.commentsLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.commentsLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.commentsOffset),
                         (int)ctx->metadataBlockHeader.commentsLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.mediaTitleLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.mediaTitleLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.mediaTitleLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.mediaTitleLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.mediaTitleOffset),
                         (int)ctx->metadataBlockHeader.mediaTitleLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.mediaManufacturerLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.mediaManufacturerLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.mediaManufacturerLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.mediaManufacturerLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.mediaManufacturerOffset),
                         (int)ctx->metadataBlockHeader.mediaManufacturerLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.mediaModelLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.mediaModelLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.mediaModelLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.mediaModelLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.mediaModelOffset),
                         (int)ctx->metadataBlockHeader.mediaModelLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.mediaSerialNumberLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.mediaSerialNumberLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.mediaSerialNumberLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.mediaSerialNumberLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.mediaSerialNumberOffset),
                         (int)ctx->metadataBlockHeader.mediaSerialNumberLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.mediaBarcodeLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.mediaBarcodeLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.mediaBarcodeLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.mediaBarcodeLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.mediaBarcodeOffset),
                         (int)ctx->metadataBlockHeader.mediaBarcodeLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.mediaPartNumberLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.mediaPartNumberLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.mediaPartNumberLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.mediaPartNumberLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.mediaPartNumberOffset),
                         (int)ctx->metadataBlockHeader.mediaPartNumberLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.driveManufacturerLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.driveManufacturerLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.driveManufacturerLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.driveManufacturerLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.driveManufacturerOffset),
                         (int)ctx->metadataBlockHeader.driveManufacturerLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.driveModelLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.driveModelLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.driveModelLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.driveModelLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.driveModelOffset),
                         (int)ctx->metadataBlockHeader.driveModelLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.driveSerialNumberLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.driveSerialNumberLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.driveSerialNumberLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.driveSerialNumberLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.driveSerialNumberOffset),
                         (int)ctx->metadataBlockHeader.driveSerialNumberLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }

        if(ctx->metadataBlockHeader.driveFirmwareRevisionLength > 0)
        {
            strBuffer = malloc(ctx->metadataBlockHeader.driveFirmwareRevisionLength + 1);
            memset(strBuffer, 0, ctx->metadataBlockHeader.driveFirmwareRevisionLength + 1);
            ucnv_convert(NULL,
                         "UTF-16LE",
                         strBuffer,
                         (int)ctx->metadataBlockHeader.driveFirmwareRevisionLength,
                         (char*)(ctx->metadataBlock + ctx->metadataBlockHeader.driveFirmwareRevisionOffset),
                         (int)ctx->metadataBlockHeader.driveFirmwareRevisionLength,
                         &u_error_code);
            printf("\tCreator: %s\n", strBuffer);
            free(strBuffer);
        }
    }

    // TODO: Table format?
    if(ctx->tracksHeader.identifier == TracksBlock)
    {
        printf("Tracks block:\n");
        for(i = 0; i < ctx->tracksHeader.entries; i++)
        {
            printf("\tTrack entry %d:\n", i);
            printf("\t\tSequence: %d\n", ctx->trackEntries[i].sequence);
            printf("\t\tType: %d\n", ctx->trackEntries[i].type);
            printf("\t\tStart: %d\n", ctx->trackEntries[i].start);
            printf("\t\tEnd: %d\n", ctx->trackEntries[i].end);
            printf("\t\tPregap: %d\n", ctx->trackEntries[i].pregap);
            printf("\t\tSession: %d\n", ctx->trackEntries[i].session);
            printf("\t\tISRC: %0.13s\n", ctx->trackEntries[i].isrc);
            printf("\t\tFlags: %d\n", ctx->trackEntries[i].flags);
        }
    }

    if(ctx->cicmBlockHeader.identifier == CicmBlock)
    {
        printf("CICM block:\n");
        printf("%s", ctx->cicmBlock);
    }

    // TODO: Table format?
    if(ctx->dumpHardwareHeader.identifier == DumpHardwareBlock)
    {
        printf("Dump hardware block:\n");

        for(i = 0; i < ctx->dumpHardwareHeader.entries; i++)
        {
            printf("\tDump hardware entry %d\n", i);

            if(ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].manufacturer),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength,
                             &u_error_code);
                printf("\t\tManufacturer: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.modelLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.modelLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.modelLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.modelLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].model),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.modelLength,
                             &u_error_code);
                printf("\t\tModel: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.revisionLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.revisionLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.revisionLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.revisionLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].revision),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.revisionLength,
                             &u_error_code);
                printf("\t\tRevision: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].firmware),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength,
                             &u_error_code);
                printf("\t\tFirmware version: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.serialLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.serialLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.serialLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.serialLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].serial),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.serialLength,
                             &u_error_code);
                printf("\t\tSerial number: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].softwareName),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength,
                             &u_error_code);
                printf("\t\tSoftware name: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].softwareVersion),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength,
                             &u_error_code);
                printf("\t\tSoftware version: %s\n", strBuffer);
                free(strBuffer);
            }

            if(ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength > 0)
            {
                strBuffer = malloc(ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength + 1);
                memset(strBuffer, 0, ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength + 1);
                ucnv_convert(NULL,
                             "UTF-8",
                             strBuffer,
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength,
                             (char*)(ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem),
                             (int)ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength,
                             &u_error_code);
                printf("\t\tSoftware operating system: %s\n", strBuffer);
                free(strBuffer);
            }

            for(j = 0; j < ctx->dumpHardwareEntriesWithData[i].entry.extents; j++)
            {
                printf("\t\tExtent %d:\n", j);
                printf("\t\t\tStart: %lu\n", ctx->dumpHardwareEntriesWithData[i].extents[j].start);
                printf("\t\t\tEnd: %lu\n", ctx->dumpHardwareEntriesWithData[i].extents[j].end);
            }
        }
    }

    if(ctx->eccCdContext != NULL) printf("CD ECC has been initialized.\n");

    printf("There are %d data tracks.\n", ctx->numberOfDataTracks);

    // TODO: ctx->readableSectorTags;

    if(ctx->blockHeaderCache.max_items > 0)
    {
        if(ctx->blockHeaderCache.cache != NULL) printf("Block header cache has been initialized.\n");
        printf("Block header cache can contain a maximum of %lu items.\n", ctx->blockHeaderCache.max_items);
    }

    if(ctx->blockCache.max_items > 0)
    {
        if(ctx->blockCache.cache != NULL) printf("Block cache has been initialized.\n");
        printf("Block cache can contain a maximum of %lu items.\n", ctx->blockCache.max_items);
    }

    printf("Aaru's ImageInfo:\n");
    printf("\tHas partitions?: %s\n", ctx->imageInfo.HasPartitions ? "yes" : "no");
    printf("\tHas sessions?: %s\n", ctx->imageInfo.HasSessions ? "yes" : "no");
    printf("\tImage size without headers: %lu bytes\n", ctx->imageInfo.ImageSize);
    printf("\tImage contains %lu sectors\n", ctx->imageInfo.Sectors);
    printf("\tBiggest sector is %d bytes\n", ctx->imageInfo.SectorSize);
    printf("\tImage version: %s\n", ctx->imageInfo.Version);

    if(ctx->imageInfo.Application != NULL)
    {
        strBuffer = malloc(65);
        memset(strBuffer, 0, 65);
        ucnv_convert(NULL, "UTF-16LE", strBuffer, 64, (const char*)ctx->imageInfo.Application, 64, &u_error_code);
        if(u_error_code == U_ZERO_ERROR) printf("\tApplication: %s\n", strBuffer);
        free(strBuffer);
    }

    if(ctx->imageInfo.ApplicationVersion != NULL)
        printf("\tApplication version: %s\n", ctx->imageInfo.ApplicationVersion);
    if(ctx->imageInfo.Creator != NULL) printf("\tCreator: %s\n", ctx->imageInfo.Creator);
    printf("\tCreation time: %ld\n", ctx->imageInfo.CreationTime);
    printf("\tLast written time: %ld\n", ctx->imageInfo.LastModificationTime);
    if(ctx->imageInfo.Comments != NULL) printf("\tComments: %s\n", ctx->imageInfo.Comments);
    if(ctx->imageInfo.MediaTitle != NULL) printf("\tMedia title: %s\n", ctx->imageInfo.MediaTitle);
    if(ctx->imageInfo.MediaManufacturer != NULL) printf("\tMedia manufacturer: %s\n", ctx->imageInfo.MediaManufacturer);
    if(ctx->imageInfo.MediaSerialNumber != NULL)
        printf("\tMedia serial number: %s\n", ctx->imageInfo.MediaSerialNumber);
    if(ctx->imageInfo.MediaBarcode != NULL) printf("\tMedia barcode: %s\n", ctx->imageInfo.MediaBarcode);
    if(ctx->imageInfo.MediaPartNumber != NULL) printf("\tMedia part number: %s\n", ctx->imageInfo.MediaPartNumber);
    printf("\tMedia type: %u\n", ctx->imageInfo.MediaType);

    if(ctx->imageInfo.MediaSequence > 0 || ctx->imageInfo.LastMediaSequence > 0)
        printf("\tMedia is number %d in a set of %d media\n",
               ctx->imageInfo.MediaSequence,
               ctx->imageInfo.LastMediaSequence);

    if(ctx->imageInfo.DriveManufacturer != NULL) printf("\tDrive manufacturer: %s\n", ctx->imageInfo.DriveManufacturer);
    if(ctx->imageInfo.DriveModel != NULL) printf("\tDrive model: %s\n", ctx->imageInfo.DriveModel);
    if(ctx->imageInfo.DriveSerialNumber != NULL)
        printf("\tDrive serial number: %s\n", ctx->imageInfo.DriveSerialNumber);
    if(ctx->imageInfo.DriveFirmwareRevision != NULL)
        printf("\tDrive firmware revision: %s\n", ctx->imageInfo.DriveFirmwareRevision);
    printf("\tXML media type: %d\n", ctx->imageInfo.XmlMediaType);
    if(ctx->imageInfo.Cylinders > 0 || ctx->imageInfo.Heads > 0 || ctx->imageInfo.SectorsPerTrack > 0)
        printf("\tMedia has %d cylinders, %d heads and %d sectors per track\n",
               ctx->imageInfo.Cylinders,
               ctx->imageInfo.Heads,
               ctx->imageInfo.SectorsPerTrack);

    aaruf_close(ctx);

    return 0;
}