#ifndef INDEX_HASH_TABLE_H
#define INDEX_HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file index-hash-table.h
 * @brief Index-hash-table — compact in-memory cache / index API.
 *
 * Provides a high-performance indexed hash table API for fixed-size keys and values,
 * with optional lazy population via a filler callback. Supports fast register-passed
 * key/value structs for x86_64 and exposes configuration and statistics helpers.
 *
 * @author <your name>
 * @date 2025-12-29
 * @version 1.0
 *
 * @note
 * - Key and value sizes are fixed per-cache instance.
 * - Fast key/value variants (IhtCacheFastKey / IhtCacheFastValue) are optimized
 *   for platforms that pass up to 16 bytes in registers.
 *
 * @see ihtCacheCreate()
 * @see ihtCacheFetch()
 */

/**
 * @typedef IhtCache
 * @brief Opaque pointer to an index hash table cache structure.
 * 
 * This is an opaque type that represents the internal state of the cache.
 * All manipulation should be done through the provided API functions.
 */
typedef struct iht_cache *IhtCache;

/**
 * @struct IhtCacheFastKey
 * @brief Fast key structure optimized for register passing on x86_64.
 * 
 * On x86_64 architecture, structures up to 16 bytes can be passed in registers,
 * allowing for efficient passing of composite keys.
 * @var v0 First 64-bit component
 * @var v1 Second 64-bit component
 */
typedef struct { uint64_t v0, v1 ; } IhtCacheFastKey ;

/**
 * @struct IhtCacheFastValue
 * @brief Fast value structure optimized for register passing on x86_64.
 * 
 * On x86_64 architecture, structures up to 16 bytes can be passed in registers,
 * allowing for efficient passing of composite values.
 * @var v0 First 64-bit component
 * @var v1 Second 64-bit component
 */
typedef struct { uint64_t v0, v1 ; } IhtCacheFastValue ;

/**
 * @typedef ihtCacheCxtDestroyer
 * @brief Callback function for destroying the cache context.
 * @param cxt The context pointer to be destroyed.
 */
typedef void (*ihtCacheCxtDestroyer)(void *cxt) ;

/**
 * @typedef ihtCacheValueDestroyer
 * @brief Callback function for destroying cached values.
 * @param cxt The context pointer.
 * @param value The value to be destroyed.
 */
typedef void (*ihtCacheValueDestroyer)(void *cxt, void *value) ;

/**
 * @typedef ihtCacheFiller
 * @brief Callback function for filling cache entries when a cache miss occurs.
 * @param cxt The context pointer.
 * @param key The cache key to look up.
 * @param value_out Pointer to write the filled value to.
 * @return true if the value was successfully filled, false otherwise.
 */
typedef bool (*ihtCacheFiller)(void *cxt, const void *key, void *value_out);

/**
 * @brief Create and initialize a new index hash table cache.
 * 
 * Allocates and initializes a new cache with the specified parameters.
 * The cache size is automatically rounded up to the next power of two.
 * 
 * @param min_capacity Minimum number of entries the cache should hold.
 * @param key_size Size in bytes of each cache key.
 * @param value_sz Size in bytes of each cache value.
 * @param filler Optional callback function to fill cache entries on miss (may be NULL).
 * @param cxt Optional context pointer to pass to the filler callback.
 * @return A new IhtCache instance, or NULL if memory allocation fails.
 */
IhtCache ihtCacheCreate(int min_capacity, int key_size, int value_sz, ihtCacheFiller filler, void *cxt);
/**
 * @brief Remove all entries from the cache, resetting it to an empty state.
 * @param cache The cache to clear.
 */
void ihtCacheRemoveAll(IhtCache cache) ;

/**
 * @brief Destroy and free all resources associated with a cache.
 * @param cache The cache to destroy.
 */
void ihtCacheDestroy(IhtCache cache) ;

/**
 * @brief Fetch a value from the cache, invoking the filler callback if needed.
 * 
 * Attempts to retrieve a value from the cache. If the key is not found and a
 * filler callback is registered, it will be invoked to populate the cache.
 * 
 * @param cache The cache instance.
 * @param key Pointer to the key to fetch.
 * @param value_out Pointer to write the fetched value to.
 * @return true if the value was found or successfully filled, false otherwise.
 */
bool ihtCacheFetch(IhtCache cache, const void *key, void *value_out) ;

/**
 * @brief Insert or update a key-value pair in the cache.
 * @param cache The cache instance.
 * @param key Pointer to the key to insert.
 * @param value Pointer to the value to insert.
 * @return true if the operation succeeded, false otherwise.
 */
bool ihtCachePut(IhtCache cache, const void *key, const void *value) ;

/**
 * @brief Look up a value in the cache without invoking the filler callback.
 * @param cache The cache instance.
 * @param key Pointer to the key to look up.
 * @param value_out Pointer to write the value to if found.
 * @return true if the key was found in the cache, false otherwise.
 */
bool ihtCacheLookup(IhtCache cache, const void *key, void *value_out) ;

/**
 * @brief Get a pointer to a cached value directly.
 * 
 * Returns a pointer to the value in the cache without copying.
 * The returned pointer is valid until the next call to ihtCachePut(),
 * which may evict the key.
 * 
 * @param cache The cache instance.
 * @param key Pointer to the key to retrieve.
 * @return Pointer to the cached value, or NULL if not found.
 */
void *ihtCacheGet(IhtCache cache, const void *key) ;

/**
 * @brief Fast lookup using optimized FAST key and value structures.
 * 
 * This function is optimized for performance on x86_64 by passing
 * structures in registers rather than through memory.
 * 
 * @param cache The cache instance.
 * @param key The FAST key structure to look up.
 * @return The FAST value associated with the key, or a zero-initialized value if not found.
 */
IhtCacheFastValue ihtCacheGet_Fast(IhtCache cache, IhtCacheFastKey key) ;

/**
 * @brief Specialized fast lookup for double-precision floating point keys and values.
 * 
 * Optimized for caches with double keys and values, leveraging register passing
 * on x86_64 architecture.
 * 
 * @param cache The cache instance.
 * @param key The double key value to look up.
 * @return The double value associated with the key, or 0.0 if not found.
 */
static inline double ihtCacheGet_D_D(IhtCache cache, double key)
{
    union fast_double {
        double d ;
        IhtCacheFastKey k ;
        IhtCacheFastValue v ;
    } ;
    union fast_double fast_k = { .d = key } ;
    union fast_double fast_v = { .v = ihtCacheGet_Fast(cache, fast_k.k) } ;
    return fast_v.d ;
}

/**
 * @brief Check if the cache has a registered filler callback.
 * @param cache The cache instance.
 * @return true if a filler callback is registered, false otherwise.
 */
bool ihtCacheHasFiller(IhtCache cache) ;

/**
 * @brief Get the current number of items in the cache's item pool.
 * @param cache The cache instance.
 * @return The current item count, or 0 if cache is NULL.
 */
int ihtCacheGetItemCount(IhtCache cache) ;

/**
 * @brief Get the maximum capacity of the cache's item pool.
 * @param cache The cache instance.
 * @return The maximum number of items, or 0 if cache is NULL.
 */
int ihtCacheGetMaxItems(IhtCache cache) ;

/**
 * @brief Get the size in bytes of cache keys.
 * @param cache The cache instance.
 * @return The key size in bytes, or 0 if cache is NULL.
 */
int ihtCacheGetKeySize(IhtCache cache) ;

/**
 * @brief Get the size in bytes of cache values.
 * @param cache The cache instance.
 * @return The value size in bytes, or 0 if cache is NULL.
 */
int ihtCacheGetValueSize(IhtCache cache) ;

/**
 * @brief Get the maximum load factor for the cache.
 * @param cache The cache instance.
 * @return The maximum load factor, or 0.0 if cache is NULL.
 */
double ihtCacheGetMaxLoadFactor(IhtCache cache) ;

/**
 * @brief Set the maximum load factor for the cache.
 * 
 * The load factor determines when the cache should be resized.
 * Typical values are between 0.5 and 0.9.
 * 
 * @param cache The cache instance.
 * @param max_load_factor The maximum load factor (ratio of entries to capacity).
 */
void ihtCacheSetMaxLoadFactor(IhtCache cache, double max_load_factor) ;

/**
 * @brief Set the minimum capacity for the cache.
 * @param cache The cache instance.
 * @param min_capacity The minimum number of entries the cache should hold.
 */
void ihtCacheSetMinCapacity(IhtCache cache, int min_capacity) ;

/**
 * @brief Set the context destroyer callback function.
 * 
 * The callback will be invoked when the cache is destroyed to clean up
 * any resources associated with the context.
 * 
 * @param cache The cache instance.
 * @param cxt_destroyer Pointer to the context destroyer callback function.
 */
void ihtCacheSetCxtDestroyer(IhtCache cache, ihtCacheCxtDestroyer cxt_destroyer) ;

/**
 * @brief Set the value destroyer callback function.
 * 
 * The callback will be invoked when cached values are evicted or the cache
 * is destroyed to clean up any resources associated with values.
 * 
 * @param cache The cache instance.
 * @param value_destroyer Pointer to the value destroyer callback function.
 */
void ihtCacheSetValueDestroyer(IhtCache cache, ihtCacheValueDestroyer value_destroyer) ;

/**
 * @brief Set a special NA (not available) value for cache misses.
 * @param cache The cache instance.
 * @param na_value Pointer to the NA value to use for cache misses.
 */
void ihtCacheSetNAValue(IhtCache cache, const void *na_value) ;

/**
 * @brief Reconfigure the cache based on updated settings.
 * 
 * Call this function after modifying cache settings (e.g., load factor,
 * capacity) to apply the changes.
 * 
 * @param cache The cache instance.
 */
void ihtCacheReconfigure(IhtCache cache) ;

/**
 * @brief Clear all cache statistics counters.
 * @param cache The cache instance.
 */
void ihtCacheClearStats(IhtCache cache) ;

/**
 * @brief Print cache statistics to a file.
 * @param fp File pointer to write statistics to.
 * @param cache The cache instance.
 * @param label A label string to prefix the statistics output with.
 * @param detail_level The level of detail to include in the output (0 = minimal, 1 = standard, 2 = full).
 */
void ihtCachePrintStats(FILE *fp, IhtCache cache, const char *label) ;
void ihtCachePrintStats1(FILE *fp, IhtCache cache, const char *label, int indent, int show_stats) ;

#ifdef __cplusplus
}
#endif

#endif