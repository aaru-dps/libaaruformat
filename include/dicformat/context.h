//
// Created by claunia on 17/03/19.
//

#ifndef LIBDICFORMAT_CONTEXT_H
#define LIBDICFORMAT_CONTEXT_H

#include <stdint.h>
#include <stdio.h>

typedef struct dicformatContext
{
    uint64_t              magic;
    uint8_t               libraryMajorVersion;
    uint8_t               libraryMinorVersion;
    FILE                  *imageStream;
    DicHeader             header;
    struct dataLinkedList *mediaTagsHead;
    struct dataLinkedList *mediaTagsTail;
} dicformatContext;

typedef struct dataLinkedList
{
    struct dataLinkedList *previous;
    struct dataLinkedList *next;
    unsigned char         *data;
    int                   type;
} dataLinkedList;

#endif //LIBDICFORMAT_CONTEXT_H
