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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "hash_map.h"

#define INITIAL_SIZE 1024
#define LOAD_FACTOR  0.75

/**
 * @brief Creates a new hash map with the specified initial size.
 *
 * Allocates and initializes a new hash map structure with the given size. The hash map uses
 * open addressing with linear probing for collision resolution. The table is zero-initialized,
 * making empty slots identifiable by having a key value of 0.
 *
 * @param size Initial size of the hash table. Must be greater than 0.
 *
 * @return Returns a pointer to the newly created hash map, or NULL if allocation fails.
 * @retval hash_map_t* Successfully created hash map with:
 *         - Allocated and zero-initialized table of specified size
 *         - Size set to the requested value
 *         - Count initialized to 0 (empty map)
 * @retval NULL Memory allocation failed
 *
 * @note The caller is responsible for freeing the returned hash map using free_map().
 * @note A key value of 0 is reserved to indicate empty slots and cannot be used as a valid key.
 *
 * @see free_map()
 */
hash_map_t *create_map(size_t size)
{
    // Enforce minimum size to prevent division by zero in modulo operations
    if(size < INITIAL_SIZE) size = INITIAL_SIZE;

    hash_map_t *map = malloc(sizeof(hash_map_t));
    map->table      = calloc(size, sizeof(kv_pair_t));
    map->size       = size;
    map->count      = 0;

    return map;
}

/**
 * @brief Frees all memory associated with a hash map.
 *
 * Deallocates the hash table and the hash map structure itself. After calling this function,
 * the hash map pointer becomes invalid and should not be used.
 *
 * @param map Pointer to the hash map to free. Can be NULL (no operation performed).
 *
 * @note This function does not free any memory pointed to by the values stored in the map.
 *       If the values are dynamically allocated, they must be freed separately before
 *       calling this function.
 *
 * @see create_map()
 */
void free_map(hash_map_t *map)
{
    free(map->table);
    free(map);
}

/**
 * @brief Resizes the hash map to a new size and rehashes all entries.
 *
 * This is an internal function that creates a new hash table with the specified size,
 * rehashes all existing key-value pairs from the old table, and replaces the old table
 * with the new one. This operation is automatically triggered when the load factor
 * exceeds the threshold during insertion.
 *
 * @param map Pointer to the hash map to resize. Must not be NULL.
 * @param new_size New size for the hash table. Should be larger than the current size
 *                 for optimal performance.
 *
 * @note This is a static (internal) function and should not be called directly.
 * @note The function preserves all existing key-value pairs during the resize operation.
 * @note After resizing, the physical positions of entries in the table will change,
 *       but the logical key-value mappings remain intact.
 * @note The old table is automatically freed after successful migration.
 *
 * @warning If memory allocation for the new table fails, the program may terminate.
 *
 * @see insert_map()
 */
static void resize_map(hash_map_t *map, size_t new_size)
{
    kv_pair_t *old_table = map->table;
    size_t     old_size  = map->size;

    map->table = calloc(new_size, sizeof(kv_pair_t));
    map->size  = new_size;
    map->count = 0;

    for(size_t i = 0; i < old_size; i++)
        if(old_table[i].key != 0)
        {
            // Re-insert
            size_t idx = old_table[i].key % new_size;

            while(map->table[idx].key != 0) idx = (idx + 1) % new_size;

            map->table[idx] = old_table[i];
            map->count++;
        }

    free(old_table);
}

/**
 * @brief Inserts a key-value pair into the hash map.
 *
 * Adds a new key-value pair to the hash map using open addressing with linear probing
 * for collision resolution. If the key already exists, the insertion fails and returns
 * false. The function automatically resizes the hash table when the load factor exceeds
 * the threshold (0.75) to maintain optimal performance.
 *
 * @param map Pointer to the hash map. Must not be NULL.
 * @param key The key to insert. Must not be 0 as this value is reserved for empty slots.
 * @param value The value to associate with the key.
 *
 * @return Returns the result of the insertion operation.
 * @retval true Successfully inserted the key-value pair. The map count is incremented.
 * @retval false Key already exists in the map. No changes made to the map.
 *
 * @note If insertion would exceed the load factor threshold, the hash table is
 *       automatically resized to twice its current size before insertion.
 * @note Time complexity: O(1) average case, O(n) worst case with poor hash distribution.
 * @note Space complexity: O(1) unless resizing occurs, in which case it's O(n).
 *
 * @warning Using 0 as a key value will result in undefined behavior as 0 is reserved
 *          for marking empty slots.
 * @warning If memory allocation fails during automatic resizing, the program may terminate.
 *
 * @see lookup_map()
 * @see resize_map()
 */
bool insert_map(hash_map_t *map, uint64_t key, uint64_t value)
{
    if((double)map->count / map->size > LOAD_FACTOR) resize_map(map, map->size * 2);

    size_t idx = key % map->size;

    while(map->table[idx].key != 0 && map->table[idx].key != key) idx = (idx + 1) % map->size;

    if(map->table[idx].key == key) return false;  // Already present

    map->table[idx].key   = key;
    map->table[idx].value = value;
    map->count++;

    return true;
}

/**
 * @brief Looks up a value by key in the hash map.
 *
 * Searches for the specified key in the hash map and retrieves its associated value.
 * Uses linear probing to handle collisions during the search. The function does not
 * modify the hash map in any way.
 *
 * @param map Pointer to the hash map to search. Must not be NULL.
 * @param key The key to search for. Must not be 0.
 * @param out_value Pointer to store the found value. Must not be NULL.
 *                  Only modified if the key is found.
 *
 * @return Returns whether the key was found in the map.
 * @retval true Key found. The associated value is written to *out_value.
 * @retval false Key not found. *out_value is not modified.
 *
 * @note Time complexity: O(1) average case, O(n) worst case with poor hash distribution
 *       or high load factor.
 * @note The function is read-only and does not modify the hash map structure.
 * @note Searching for key value 0 will always return false as 0 indicates empty slots.
 *
 * @warning The out_value parameter must point to valid memory location.
 *          Passing NULL will result in undefined behavior.
 *
 * @see insert_map()
 */
bool lookup_map(const hash_map_t *map, uint64_t key, uint64_t *out_value)
{
    size_t idx = key % map->size;

    while(map->table[idx].key != 0)
    {
        if(map->table[idx].key == key)
        {
            *out_value = map->table[idx].value;
            return true;
        }

        idx = (idx + 1) % map->size;
    }

    return false;
}