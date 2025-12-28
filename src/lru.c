#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <uthash.h>

#include <aaruformat.h>

// this is an example of how to do a LRU cache in C using uthash
// http://uthash.sourceforge.net/
// by Jehiah Czebotar 2011 - jehiah@gmail.com
// this code is in the public domain http://unlicense.org/

/**
 * @brief Finds a value in the cache by string key.
 *
 * Searches for a value in the cache using a string key and moves it to the front if found.
 *
 * @param cache Pointer to the cache header.
 * @param key String key to search for.
 * @return Pointer to the value if found, or NULL if not found.
 */
void *find_in_cache(struct CacheHeader *cache, const char *key)
{
    struct CacheEntry *entry;
    HASH_FIND_STR(cache->cache, key, entry);
    if(entry)
    {
        // remove it (so the subsequent add will throw it on the front of the list)
        HASH_DELETE(hh, cache->cache, entry);
        HASH_ADD_KEYPTR(hh, cache->cache, entry->key, strlen(entry->key), entry);
        return entry->value;
    }
    return NULL;
}

/**
 * @brief Adds a value to the cache with a string key, pruning if necessary.
 *
 * Adds a new entry to the cache. If the cache exceeds its maximum size, prunes the least recently used entry.
 *
 * @param cache Pointer to the cache header.
 * @param key String key to add.
 * @param value Pointer to the value to store.
 */
void add_to_cache(struct CacheHeader *cache, const char *key, void *value)
{
    struct CacheEntry *entry;
    // TODO: Is this needed or we're just losing cycles? uthash does not free the entry
    entry        = malloc(sizeof(struct CacheEntry));
    entry->key   = strdup(key);
    entry->value = value;
    HASH_ADD_KEYPTR(hh, cache->cache, entry->key, strlen(entry->key), entry);

    // prune the cache to MAX_CACHE_SIZE
    if(HASH_COUNT(cache->cache) >= cache->max_items)
    {
        struct CacheEntry *tmp_entry;
        HASH_ITER(hh, cache->cache, entry, tmp_entry)
        {
            // prune the first entry (loop is based on insertion order so this deletes the oldest item)
            HASH_DELETE(hh, cache->cache, entry);
            free(entry->key);

            // Free the cached value if a free function is registered
            if(cache->free_func && entry->value)
                cache->free_func(entry->value);

            free(entry);
            break;
        }
    }
}

FORCE_INLINE char *uint64_to_string(const uint64_t number)
{
    char *char_key = malloc(17);  // 16 hex digits + null terminator
    if(!char_key) return NULL;
    snprintf(char_key, 17, "%016" PRIX64, number);
    return char_key;
}

/**
 * @brief Finds a value in the cache by uint64_t key, using string conversion.
 *
 * Converts the uint64_t key to a string and searches for the entry in the cache.
 *
 * @param cache Pointer to the cache header.
 * @param key 64-bit integer key to search for.
 * @return Pointer to the value if found, or NULL if not found.
 */
void *find_in_cache_uint64(struct CacheHeader *cache, const uint64_t key)
{
    char *char_key = uint64_to_string(key);
    if(!char_key) return NULL;

    void *result = find_in_cache(cache, char_key);
    free(char_key);  // Free the temporary string to prevent memory leak

    return result;
}

/**
 * @brief Adds a value to the cache with a uint64_t key, using string conversion.
 *
 * Converts the uint64_t key to a string and adds the entry to the cache.
 *
 * @param cache Pointer to the cache header.
 * @param key 64-bit integer key to add.
 * @param value Pointer to the value to store.
 */
void add_to_cache_uint64(struct CacheHeader *cache, const uint64_t key, void *value)
{
    char *char_key = uint64_to_string(key);
    if(!char_key) return;

    add_to_cache(cache, char_key, value);
    free(char_key);  // Free the temporary string (add_to_cache makes its own copy with strdup)
}

/**
 * @brief Frees all entries in the cache and clears it.
 *
 * Iterates through all cache entries, frees their keys and the entries themselves,
 * then clears the cache hash table. Uses the cache's free_func if set to free cached values.
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
        free(entry->key);

        // Free the cached value if a free function is registered
        if(cache->free_func && entry->value)
            cache->free_func(entry->value);

        free(entry);
    }

    cache->cache = NULL;
}

