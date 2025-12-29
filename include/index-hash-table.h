#ifndef INDEX_HASH_TABLE_H
#define INDEX_HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Simple function to demonstrate linkage
void say_hello() ;

// Opaque type for the index hash table cache
typedef struct iht_cache *IhtCache;

// On x86_64 architecture, structures up to 16 bytes can be passed in registers
typedef struct { uint64_t v0, v1 ; } IhtCacheFastKey ;
typedef struct { uint64_t v0, v1 ; } IhtCacheFastValue ;

// Function pointer types for context and result destruction, and value filling
typedef void (*ihtCacheCxtDestroyer)(void *cxt) ;
typedef void (*ihtCacheValueDestroyer)(void *cxt, void *value) ;
typedef bool (*ihtCacheFiller)(void *cxt, const void *key, void *value_out);

// Cache management functions
IhtCache ihtCacheCreate(int min_capacity, int key_size, int value_sz, ihtCacheFiller filler, void *cxt);
void ihtCacheRemoveAll(IhtCache cache) ;
void ihtCacheDestroy(IhtCache cache) ;

// Basic get, put, and lookup functions
bool ihtCacheFetch(IhtCache cache, const void *key, void *value_out) ;
bool ihtCachePut(IhtCache cache, const void *key, const void *value) ;
bool ihtCacheLookup(IhtCache cache, const void *key, void *value_out) ;

// Basic get function, result valid until next call to Put which may evict key
void *ihtCacheGet(IhtCache cache, const void *key) ;

// Fast get function using FAST key and value structures
IhtCacheFastValue ihtCacheGet_Fast(IhtCache cache, IhtCacheFastKey key) ;

// Specialized get function for double keys and values
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

// Cache information retrieval functions
bool ihtCacheHasFiller(IhtCache cache) ;
int ihtCacheGetItemCount(IhtCache cache) ;
int ihtCacheGetMaxItems(IhtCache cache) ;
int ihtCacheGetKeySize(IhtCache cache) ;
int ihtCacheGetValueSize(IhtCache cache) ;
double ihtCacheGetMaxLoadFactor(IhtCache cache) ;

// Configuration
void ihtCacheSetMaxLoadFactor(IhtCache cache, double max_load_factor) ;
void ihtCacheSetMinCapacity(IhtCache cache, int min_capacity) ;
void ihtCacheSetCxtDestroyer(IhtCache cache, ihtCacheCxtDestroyer cxt_destroyer) ;
void ihtCacheSetValueDestroyer(IhtCache cache, ihtCacheValueDestroyer value_destroyer) ;
void ihtCacheSetNAValue(IhtCache cache, const void *na_value) ;
void ihtCacheReconfigure(IhtCache cache) ;
void ihtCacheClearStats(IhtCache cache) ;
void ihtCachePrintStats(FILE *fp, IhtCache cache, const char *label) ;

#ifdef __cplusplus
}
#endif

#endif