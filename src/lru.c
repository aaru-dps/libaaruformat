#include <string.h>
#include <uthash.h>

#include <aaruformat.h>

// this is an example of how to do a LRU cache in C using uthash
// http://uthash.sourceforge.net/
// by Jehiah Czebotar 2011 - jehiah@gmail.com
// this code is in the public domain http://unlicense.org/

void* find_in_cache(struct CacheHeader* cache, char* key)
{
    struct CacheEntry* entry;
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

void add_to_cache(struct CacheHeader* cache, char* key, void* value)
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

FORCE_INLINE char* int64_to_string(uint64_t number)
{
    char* charKey;

    charKey = malloc(17);

    charKey[0]  = (char)(number >> 60);
    charKey[1]  = (char)((number & 0xF00000000000000ULL) >> 56);
    charKey[2]  = (char)((number & 0xF0000000000000ULL) >> 52);
    charKey[3]  = (char)((number & 0xF000000000000ULL) >> 48);
    charKey[4]  = (char)((number & 0xF00000000000ULL) >> 44);
    charKey[5]  = (char)((number & 0xF0000000000ULL) >> 40);
    charKey[6]  = (char)((number & 0xF000000000ULL) >> 36);
    charKey[7]  = (char)((number & 0xF00000000ULL) >> 32);
    charKey[8]  = (char)((number & 0xF0000000ULL) >> 28);
    charKey[9]  = (char)((number & 0xF000000ULL) >> 24);
    charKey[10] = (char)((number & 0xF00000ULL) >> 20);
    charKey[11] = (char)((number & 0xF0000ULL) >> 16);
    charKey[12] = (char)((number & 0xF000ULL) >> 12);
    charKey[13] = (char)((number & 0xF00ULL) >> 8);
    charKey[14] = (char)((number & 0xF0ULL) >> 4);
    charKey[15] = (char)(number & 0xFULL);
    charKey[16] = 0;

    return charKey;
}

void* find_in_cache_uint64(struct CacheHeader* cache, uint64_t key)
{
    return find_in_cache(cache, int64_to_string(key));
}

void add_to_cache_uint64(struct CacheHeader* cache, uint64_t key, void* value)
{
    return add_to_cache(cache, int64_to_string(key), value);
}