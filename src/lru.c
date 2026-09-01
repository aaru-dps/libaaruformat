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
 * @brief Frees a cache entry's value and unlinks it from the cache.
 *
 * @param cache Pointer to the cache header.
 * @param entry Entry to drop. Must be linked in cache.
 */
static void drop_entry(struct CacheHeader *cache, struct CacheEntry *entry)
{
    HASH_DELETE(hh, cache->cache, entry);

    cache->cur_bytes -= entry->size;

    if(cache->free_func && entry->value) cache->free_func(entry->value);

    free(entry);
}

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
 * @brief Adds a value to the cache with a uint64_t key, evicting LRU entries if over budget.
 *
 * Adds a new entry to the cache. If the cache exceeds its memory budget, the least recently
 * used entries are evicted until it fits again. The entry just inserted is never evicted, so the
 * caller may keep using the pointer it handed over for the remainder of the call.
 *
 * @param cache Pointer to the cache header.
 * @param key 64-bit integer key to add.
 * @param value Pointer to the value to store.
 * @param size Size in bytes of the memory pointed to by value.
 */
void add_to_cache_uint64(struct CacheHeader *cache, const uint64_t key, void *value, const size_t size)
{
    struct CacheEntry *entry = malloc(sizeof(struct CacheEntry));
    if(!entry) return;

    entry->key   = key;
    entry->value = value;
    entry->size  = size;
    HASH_ADD(hh, cache->cache, key, sizeof(uint64_t), entry);
    cache->cur_bytes += size;

    if(cache->max_bytes == 0) return;

    // Evict least recently used entries until back under budget. Never evict the entry just
    // inserted: the caller still uses that pointer after this returns.
    while(cache->cur_bytes > cache->max_bytes && HASH_COUNT(cache->cache) > 1)
    {
        struct CacheEntry *oldest = cache->cache;

        if(oldest == NULL || oldest == entry) break;

        drop_entry(cache, oldest);
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

    HASH_ITER(hh, cache->cache, entry, tmp) { drop_entry(cache, entry); }

    cache->cache     = NULL;
    cache->cur_bytes = 0;
}
