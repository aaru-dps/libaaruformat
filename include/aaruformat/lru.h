//
// Created by claunia on 2/10/22.
//

#ifndef LIBAARUFORMAT_LRU_H
#define LIBAARUFORMAT_LRU_H

#include <stdint.h>
#include <uthash.h>

struct CacheEntry
{
    char*          key;
    void*          value;
    UT_hash_handle hh;
};

struct CacheHeader
{
    uint64_t           max_items;
    struct CacheEntry* cache;
};

/**
 * Finds an item in the specified cache
 * @param cache Pointer to the cache header
 * @param key Key
 * @return Value if found, NULL if not
 */
void* find_in_cache(struct CacheHeader* cache, char* key);

/**
 * Adds an item to the specified cache
 * @param cache Pointer to the cache header
 * @param key Key
 * @param value Value
 */
void add_to_cache(struct CacheHeader* cache, char* key, void* value);

/**
 * Finds an item in the specified cache using a 64-bit integer key
 * @param cache Pointer to the cache header
 * @param key Key
 * @return Value if found, NULL if not
 */
void* find_in_cache_uint64(struct CacheHeader* cache, uint64_t key);

/**
 * Adds an item to the specified cache using a 64-bit integer key
 * @param cache Pointer to the cache header
 * @param key Key
 * @param value Value
 */
void add_to_cache_uint64(struct CacheHeader* cache, uint64_t key, void* value);

#endif // LIBAARUFORMAT_LRU_H
