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

void *find_in_cache(struct CacheHeader *cache, char *key)
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

void add_to_cache(struct CacheHeader *cache, char *key, void *value)
{
    struct CacheEntry *entry, *tmp_entry;
    // TODO: Is this needed or we're just losing cycles? uthash does not free the entry
    entry        = malloc(sizeof(struct CacheEntry));
    entry->key   = strdup(key);
    entry->value = value;
    HASH_ADD_KEYPTR(hh, cache->cache, entry->key, strlen(entry->key), entry);

    // prune the cache to MAX_CACHE_SIZE
    if(HASH_COUNT(cache->cache) >= cache->max_items)
    {
        HASH_ITER(hh, cache->cache, entry, tmp_entry)
        {
            // prune the first entry (loop is based on insertion order so this deletes the oldest item)
            HASH_DELETE(hh, cache->cache, entry);
            free(entry->key);
            free(entry);
            break;
        }
    }
}

FORCE_INLINE char *int64_to_string(uint64_t number)
{
    char *charKey = malloc(17);  // 16 hex digits + null terminator
    if(!charKey) return NULL;
    snprintf(charKey, 17, "%016" PRIX64, number);
    return charKey;
}

void *find_in_cache_uint64(struct CacheHeader *cache, uint64_t key)
{
    return find_in_cache(cache, int64_to_string(key));
}

void add_to_cache_uint64(struct CacheHeader *cache, uint64_t key, void *value)
{
    return add_to_cache(cache, int64_to_string(key), value);
}