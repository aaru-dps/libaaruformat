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

/**
 * @file static_lru_hash_map.c
 * @brief Implementation of static-memory hash map with LRU-like eviction.
 *
 * This implementation provides a hash map with:
 * - Fixed memory allocation (size set at creation, never grows)
 * - Automatic eviction of least-frequently-used entries when approaching capacity
 * - Access frequency tracking with periodic aging to prevent stale hot entries
 * - Same API style as hash_map.c for easy migration
 *
 * Eviction Strategy: Approximate LFU (Least Frequently Used) with Aging
 * - Each entry has an 8-bit access_count (0-255, saturates)
 * - Count is incremented on each lookup or update
 * - Counts are halved periodically (aging) to favor recent accesses
 * - Eviction removes entries with lowest access_count first
 *
 * Collision Resolution: Open addressing with linear probing (same as hash_map.c)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat/static_lru_hash_map.h"

/* ============================================================================
 * Internal Constants
 * ============================================================================ */

#define MIN_ACCESS_COUNT 1  ///< New entries start with this access count
#define TOMBSTONE_KEY    0  ///< Marker for empty/deleted slots (key=0 reserved)

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Ages all access counts by halving them.
 *
 * This prevents old "hot" entries from permanently occupying the cache.
 * Called automatically every STATIC_LRU_AGING_INTERVAL operations.
 *
 * @param map Pointer to the hash map.
 */
static void age_access_counts(static_lru_hash_map_t *map)
{
    for(size_t i = 0; i < map->size; i++)
    {
        if(map->table[i].key != TOMBSTONE_KEY && map->table[i].access_count > 1)
        {
            // Halve with rounding up to avoid zeroing out entries too quickly
            map->table[i].access_count = (map->table[i].access_count + 1) / 2;
        }
    }
}

/**
 * @brief Rehashes all entries in place after eviction.
 *
 * After evicting entries, the linear probing chains are broken.
 * This function rebuilds the table to restore proper probe sequences.
 *
 * Note: This temporarily allocates a second table. For truly static memory,
 * a more complex in-place algorithm would be needed.
 *
 * @param map Pointer to the hash map.
 */
static void rehash_in_place(static_lru_hash_map_t *map)
{
    lru_kv_pair_t *old_table = map->table;
    size_t         old_count = map->count;

    // Allocate new table (temporary allocation during rehash)
    map->table = calloc(map->size, sizeof(lru_kv_pair_t));
    if(!map->table)
    {
        // Allocation failed, restore old table
        map->table = old_table;
        return;
    }

    map->count = 0;

    // Re-insert all non-empty entries
    for(size_t i = 0; i < map->size; i++)
    {
        if(old_table[i].key != TOMBSTONE_KEY)
        {
            size_t idx = old_table[i].key % map->size;

            while(map->table[idx].key != TOMBSTONE_KEY) idx = (idx + 1) % map->size;

            map->table[idx] = old_table[i];
            map->count++;
        }
    }

    free(old_table);

    // Sanity check - count should match what we had before eviction
    (void)old_count;  // Avoid unused variable warning in release builds
}

/**
 * @brief Evicts least-frequently-used entries to make room for new insertions.
 *
 * Algorithm:
 * 1. Build a histogram of access counts (256 buckets for uint8_t)
 * 2. Find the cutoff access_count that covers enough entries to evict
 * 3. Mark entries at or below the cutoff as deleted
 * 4. Rehash remaining entries to fix probe chains
 *
 * Time complexity: O(n) where n = map size
 *
 * @param map              Pointer to the hash map.
 * @param target_count_out Target count after eviction. If 0, uses map->target_count.
 *
 * @return Number of entries evicted.
 */
static size_t evict_lru_entries(static_lru_hash_map_t *map, size_t target_count_out)
{
    if(target_count_out == 0) target_count_out = map->target_count;

    // Nothing to evict if we're already below target
    if(map->count <= target_count_out) return 0;

    size_t entries_to_remove = map->count - target_count_out;

    // Step 1: Build histogram of access counts
    // histogram[i] = number of entries with access_count == i
    size_t histogram[256] = {0};

    for(size_t i = 0; i < map->size; i++)
    {
        if(map->table[i].key != TOMBSTONE_KEY) histogram[map->table[i].access_count]++;
    }

    // Step 2: Find cutoff access_count
    // We want to evict entries with the lowest access counts
    // Find the threshold such that entries with count <= threshold covers entries_to_remove
    uint8_t cutoff     = 0;
    size_t  cumulative = 0;

    for(int i = 0; i < 256; i++)
    {
        cumulative += histogram[i];
        if(cumulative >= entries_to_remove)
        {
            cutoff = (uint8_t)i;
            break;
        }
    }

    // Step 3: Evict entries with access_count <= cutoff
    size_t removed = 0;

    for(size_t i = 0; i < map->size && removed < entries_to_remove; i++)
    {
        if(map->table[i].key != TOMBSTONE_KEY && map->table[i].access_count <= cutoff)
        {
            map->table[i].key          = TOMBSTONE_KEY;
            map->table[i].value        = 0;
            map->table[i].access_count = 0;
            removed++;
        }
    }

    map->count -= removed;

#ifdef STATIC_LRU_ENABLE_STATS
    map->eviction_count += removed;
    map->eviction_cycles++;
#endif

    // Step 4: Rehash to fix probe chains
    rehash_in_place(map);

    return removed;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

static_lru_hash_map_t *static_lru_create_map(size_t size)
{
    // Enforce minimum size
    if(size < STATIC_LRU_MIN_SIZE) size = STATIC_LRU_MIN_SIZE;

    static_lru_hash_map_t *map = malloc(sizeof(static_lru_hash_map_t));
    if(!map) return NULL;

    map->table = calloc(size, sizeof(lru_kv_pair_t));
    if(!map->table)
    {
        free(map);
        return NULL;
    }

    map->size         = size;
    map->count        = 0;
    map->max_count    = (size_t)(size * STATIC_LRU_EVICTION_LOAD_FACTOR);
    map->target_count = (size_t)(size * STATIC_LRU_TARGET_LOAD_FACTOR);
    map->age_counter  = 0;
    map->_padding     = 0;

#ifdef STATIC_LRU_ENABLE_STATS
    map->total_lookups   = 0;
    map->cache_hits      = 0;
    map->total_inserts   = 0;
    map->eviction_count  = 0;
    map->eviction_cycles = 0;
#endif

    return map;
}

void static_lru_free_map(static_lru_hash_map_t *map)
{
    if(!map) return;

    free(map->table);
    free(map);
}

bool static_lru_insert_map(static_lru_hash_map_t *map, uint64_t key, uint64_t value)
{
    // Trigger eviction if we've exceeded the threshold
    if(map->count >= map->max_count) evict_lru_entries(map, 0);

    // Periodic aging of access counts
    map->age_counter++;
    if(map->age_counter >= STATIC_LRU_AGING_INTERVAL)
    {
        age_access_counts(map);
        map->age_counter = 0;
    }

#ifdef STATIC_LRU_ENABLE_STATS
    map->total_inserts++;
#endif

    // Linear probing to find slot
    size_t idx = key % map->size;

    while(map->table[idx].key != TOMBSTONE_KEY && map->table[idx].key != key) idx = (idx + 1) % map->size;

    if(map->table[idx].key == key)
    {
        // Key exists - update value and boost access count
        map->table[idx].value = value;
        if(map->table[idx].access_count < 255) map->table[idx].access_count++;
        return false;  // Not a new key
    }

    // New key insertion
    map->table[idx].key          = key;
    map->table[idx].value        = value;
    map->table[idx].access_count = MIN_ACCESS_COUNT;
    map->count++;

    return true;
}

bool static_lru_lookup_map(static_lru_hash_map_t *map, uint64_t key, uint64_t *out_value)
{
#ifdef STATIC_LRU_ENABLE_STATS
    map->total_lookups++;
#endif

    size_t idx = key % map->size;

    while(map->table[idx].key != TOMBSTONE_KEY)
    {
        if(map->table[idx].key == key)
        {
            *out_value = map->table[idx].value;

            // Increment access count (saturate at 255)
            if(map->table[idx].access_count < 255) map->table[idx].access_count++;

#ifdef STATIC_LRU_ENABLE_STATS
            map->cache_hits++;
#endif

            return true;
        }

        idx = (idx + 1) % map->size;
    }

    return false;
}

bool static_lru_contains_key(const static_lru_hash_map_t *map, uint64_t key)
{
    size_t idx = key % map->size;

    while(map->table[idx].key != TOMBSTONE_KEY)
    {
        if(map->table[idx].key == key) return true;

        idx = (idx + 1) % map->size;
    }

    return false;
}

size_t static_lru_evict(static_lru_hash_map_t *map, size_t entries_to_keep)
{ return evict_lru_entries(map, entries_to_keep); }

void static_lru_age_counts(static_lru_hash_map_t *map)
{
    age_access_counts(map);
    map->age_counter = 0;
}

double static_lru_load_factor(const static_lru_hash_map_t *map) { return (double)map->count / (double)map->size; }

size_t static_lru_free_slots(const static_lru_hash_map_t *map) { return map->size - map->count; }

#ifdef STATIC_LRU_ENABLE_STATS
double static_lru_hit_rate(const static_lru_hash_map_t *map)
{
    if(map->total_lookups == 0) return 0.0;

    return (double)map->cache_hits / (double)map->total_lookups;
}

void static_lru_reset_stats(static_lru_hash_map_t *map)
{
    map->total_lookups   = 0;
    map->cache_hits      = 0;
    map->total_inserts   = 0;
    map->eviction_count  = 0;
    map->eviction_cycles = 0;
}
#endif
