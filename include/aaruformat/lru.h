//
// Created by claunia on 2/10/22.
//

#ifndef LIBAARUFORMAT_LRU_H
#define LIBAARUFORMAT_LRU_H

#include <stddef.h>
#include <stdint.h>
#include <uthash.h>

/** \struct CacheEntry
 *  \brief Single hash entry in the in-memory cache.
 *
 *  This structure is managed by uthash and represents one key/value association
 *  tracked by the cache. Keys are native 64-bit integers, hashed directly by
 *  uthash without string conversion. Callers do not allocate or free individual
 *  entries directly; use the cache API helpers.
 *
 *  Lifetime & ownership:
 *   - value is an opaque pointer supplied by caller; the cache does not take
 *     ownership unless a free_func is registered on the CacheHeader.
 */
struct CacheEntry
{
    uint64_t       key;    ///< 64-bit integer key (unique within the cache).
    void          *value;  ///< Opaque value pointer associated with key.
    size_t         size;   ///< Size in bytes of the memory pointed to by value.
    UT_hash_handle hh;     ///< uthash handle (must remain per uthash docs).
};

/** \struct CacheHeader
 *  \brief Cache top-level descriptor encapsulating the hash table root and capacity limit.
 *
 *  The cache is bounded by the total number of *bytes* held by its values, not by the
 *  number of entries: the caches in this library store values of wildly different sizes
 *  (a block header is a few dozen bytes, a decompressed data block is megabytes), so an
 *  entry-count limit cannot express a memory budget. On insert, the least recently used
 *  entries are evicted until the cache is back under max_bytes.
 *
 *  Fields:
 *   - max_bytes: Memory budget in bytes; 0 means unlimited.
 *   - cur_bytes: Bytes currently held by the cached values.
 *   - cache:     uthash root pointer; NULL when the cache is empty.
 *   - free_func: Optional callback to free cached values on eviction/clear.
 *
 *  The entry just inserted is never evicted, so a caller may keep using the pointer it
 *  handed over for the remainder of the call even if the value is larger than max_bytes.
 */
struct CacheHeader
{
    uint64_t max_bytes;         ///< Memory budget in bytes for the cached values. 0 means unlimited.
    uint64_t cur_bytes;         ///< Bytes currently held by the cached values.
    struct CacheEntry *cache;   ///< Hash root (uthash). NULL when empty.
    void (*free_func)(void *);  ///< Optional callback to free cached values. NULL if not needed.
};

void *find_in_cache_uint64(struct CacheHeader *cache, uint64_t key);
void  add_to_cache_uint64(struct CacheHeader *cache, uint64_t key, void *value, size_t size);
void  free_cache(struct CacheHeader *cache);

#endif  // LIBAARUFORMAT_LRU_H
