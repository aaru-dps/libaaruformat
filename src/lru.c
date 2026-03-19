#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <uthash.h>

#include <aaruformat.h>

// LRU cache using uthash with native integer keys.
// Based on uthash LRU example by Jehiah Czebotar 2011 (public domain).
// Rewritten to use HASH_FIND/HASH_ADD with uint64_t keys directly,
// eliminating all string conversion, malloc, and snprintf overhead
// from the hot path.

/**
 * @brief Finds a value in the cache by uint64_t key.
 *
 * Searches for a value using a native 64-bit integer key and promotes it
 * to the front of the insertion-order list (LRU refresh) if found.
 *
 * @param cache Pointer to the cache header.
 * @param key 64-bit integer key to search for.
 * @return Pointer to the value if found, or NULL if not found.
 */
void *find_in_cache_uint64(struct CacheHeader *cache, const uint64_t key)
{
    struct CacheEntry *entry = NULL;
    HASH_FIND(hh, cache->cache, &key, sizeof(uint64_t), entry);
    if(entry)
    {
        // Remove and re-add to move to front of insertion-order list (LRU refresh).
        HASH_DELETE(hh, cache->cache, entry);
        HASH_ADD(hh, cache->cache, key, sizeof(uint64_t), entry);
        return entry->value;
    }
    return NULL;
}

/**
 * @brief Adds a value to the cache with a uint64_t key, evicting LRU if full.
 *
 * Adds a new entry to the cache. If the cache exceeds its maximum size,
 * evicts the least recently used (oldest insertion-order) entry.
 *
 * @param cache Pointer to the cache header.
 * @param key 64-bit integer key to add.
 * @param value Pointer to the value to store.
 */
void add_to_cache_uint64(struct CacheHeader *cache, const uint64_t key, void *value)
{
    struct CacheEntry *entry = malloc(sizeof(struct CacheEntry));
    if(!entry) return;

    entry->key   = key;
    entry->value = value;
    HASH_ADD(hh, cache->cache, key, sizeof(uint64_t), entry);

    // Evict oldest entry if cache exceeded capacity.
    if(HASH_COUNT(cache->cache) > cache->max_items)
    {
        struct CacheEntry *tmp_entry;
        HASH_ITER(hh, cache->cache, entry, tmp_entry)
        {
            HASH_DELETE(hh, cache->cache, entry);

            if(cache->free_func && entry->value)
                cache->free_func(entry->value);

            free(entry);
            break;
        }
    }
}

/**
 * @brief Frees all entries in the cache and clears it.
 *
 * Iterates through all cache entries, frees them, then clears the hash table.
 * Uses the cache's free_func if set to free cached values.
 *
 * @param cache Pointer to the cache header.
 */
void free_cache(struct CacheHeader *cache)
{
    struct CacheEntry *entry, *tmp;

    if(!cache || !cache->cache) return;

    HASH_ITER(hh, cache->cache, entry, tmp)
    {
        HASH_DELETE(hh, cache->cache, entry);

        if(cache->free_func && entry->value)
            cache->free_func(entry->value);

        free(entry);
    }

    cache->cache = NULL;
}
