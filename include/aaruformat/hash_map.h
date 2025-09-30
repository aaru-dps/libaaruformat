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

#ifndef LIBAARUFORMAT_HASH_MAP_H
#define LIBAARUFORMAT_HASH_MAP_H

#include <stdbool.h>
#include <stdlib.h>

typedef struct
{
    uint64_t key;
    uint64_t value;
} kv_pair_t;

typedef struct
{
    kv_pair_t *table;
    size_t     size;
    size_t     count;
} hash_map_t;

hash_map_t *create_map(size_t size);
void free_map(hash_map_t *map);
bool insert_map(hash_map_t *map, uint64_t key, uint64_t value);
bool lookup_map(const hash_map_t *map, uint64_t key, uint64_t *out_value);

#endif  // LIBAARUFORMAT_HASH_MAP_H
