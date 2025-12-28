//
// Created by claunia on 2/10/22.
//

#ifndef LIBAARUFORMAT_LRU_H
#define LIBAARUFORMAT_LRU_H

#include <stdint.h>
#include <uthash.h>

/** \struct CacheEntry
 *  \brief Single hash entry in the in-memory cache.
 *
 *  This structure is managed by uthash (open addressing with chaining semantics provided by macros).
 *  It represents one key/value association tracked by the cache. The cache implementation supports
 *  both string keys (null-terminated) and 64-bit numeric keys; numeric keys are stored by casting
 *  to a temporary string buffer upstream (see implementation). Callers do not allocate or free
 *  individual entries directly; use the cache API helpers.
 *
 *  Lifetime & ownership:
 *   - key points either to a heap-allocated C string owned by the cache or to a short-lived buffer
 *     duplicated internally; callers must not free it after insertion.
 *   - value is an opaque pointer supplied by caller; the cache does not take ownership of the pointee
 *     (caller remains responsible for the underlying object unless documented otherwise).
 */
struct CacheEntry
{
    char          *key;    ///< Null-terminated key string (unique within the cache). May encode numeric keys.
    void          *value;  ///< Opaque value pointer associated with key (not freed automatically on eviction/clear).
    UT_hash_handle hh;  ///< uthash handle linking this entry into the hash table (must remain last or per uthash docs).
};

/** \struct CacheHeader
 *  \brief Cache top-level descriptor encapsulating the hash table root and capacity limit.
 *
 *  The cache enforces an upper bound (max_items) on the number of tracked entries. Insert helpers are expected
 *  to evict (or refuse) when the limit is exceeded (strategy defined in implementation; current behavior may be
 *  simple non-evicting if not yet implemented as a true LRU). The cache pointer holds the uthash root (NULL when
 * empty).
 *
 *  Fields:
 *   - max_items: Maximum number of entries allowed; 0 means "no explicit limit" if accepted by implementation.
 *   - cache:     uthash root pointer; NULL when the cache is empty.
 */
struct CacheHeader
{
    uint64_t max_items;         ///< Hard limit for number of entries (policy: enforce/ignore depends on implementation).
    struct CacheEntry *cache;   ///< Hash root (uthash). NULL when empty.
    void (*free_func)(void *);  ///< Optional callback to free cached values. NULL if values don't need freeing.
};

void *find_in_cache(struct CacheHeader *cache, const char *key);
void  add_to_cache(struct CacheHeader *cache, const char *key, void *value);
void *find_in_cache_uint64(struct CacheHeader *cache, uint64_t key);
void  add_to_cache_uint64(struct CacheHeader *cache, uint64_t key, void *value);
void  free_cache(struct CacheHeader *cache);

#endif  // LIBAARUFORMAT_LRU_H
