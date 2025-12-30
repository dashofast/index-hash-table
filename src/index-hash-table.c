#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include "index-hash-table.h"

#define MIN_CAPACITY 16
#define DEFAULT_LOAD_FACTOR 0.40
#define MAX_EVICTION_SEARCH 16

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) (__builtin_expect(!!(x), 1))
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define LIKLEY(x) (!!(x))
#define UNLIKLEY(x) (!!(x))
#endif


typedef enum SLOT_STATE { SLOT_EMPTY = 0, SLOT_REMOVED = 1, SLOT_MIN_AGE = 2, SLOT_MAX_AGE = 7 } SlotState ;

static SlotState INITIAL_STATE = SLOT_MIN_AGE ; // set to SLOT_MIN_AGEA+n to make it more LRU.

static inline bool empty_slot(SlotState slot_state) {
    return slot_state <= SLOT_REMOVED ;
}

typedef struct iht_entry {
    uint32_t hash_value ;  // cached hash value
    unsigned int item_index ;
    SlotState state:8 ;
} *IhtEntry ;

typedef struct iht_item {
    IhtCacheFastKey key ;
    IhtCacheFastValue value ;
} *IhtItem ;

typedef struct iht_counter { int count; int scans ; } IhtCounter ;

struct iht_stats {
    int lookups ;
    IhtCounter hits ;
    IhtCounter misses ;
    IhtCounter adds ;
    IhtCounter updates ;
    IhtCounter evictions ;
} ;

struct iht_cache {
    // Configuration
    int min_capacity ;
    int key_size ;
    int value_size ;
    float max_load_factor ;
    ihtCacheFiller filler ;
    void *cxt ;
    ihtCacheCxtDestroyer cxt_destroyer ;
    ihtCacheValueDestroyer value_destroyer ;
    // State
    bool fast_mode:1 ;            // Use FastParam and FastResult
    bool fast_key:1 ;
    bool fast_value:1 ;
    bool short_key:1 ;

    int item_count ;
    int max_entries ;           // power of 2
    int entries_mask ;          // max_entries-1
    int max_items ;
    int item_size ;
    int key_offset ;
    int value_offset ;
    int victim_index ;          // index of next victim for eviction

    void *na_value ;            // value representing NA
    IhtCacheFastKey work_key ;  // Zero-padded key

    // Storage
    IhtEntry entries ;          // [max_entries]
    IhtItem items ;             // [max_items] of item_size bytes

    struct iht_stats stats ;
} ;

static bool use_crc ;

// Accessors
static inline void *item_addr(IhtCache cache, int item_index) {
    return ((char*)cache->items) + cache->item_size*item_index ;
}

static inline void *item_value(IhtCache cache, int item_index) {
    return ((char*) item_addr(cache, item_index)) + cache->value_offset ;
}

static inline void *item_key(IhtCache cache, int item_index) {
    return ((char*) item_addr(cache, item_index)) + cache->key_offset ;
}

static inline IhtEntry entry_addr(IhtCache cache, unsigned entry_index) {
    return &cache->entries[entry_index] ;
}

static inline bool key_equals(IhtCache cache, const void *key1, const void *key2) {
    return memcmp(key1, key2, cache->key_size) == 0 ;
}

static inline bool fast_key_equals(IhtCacheFastKey key1, IhtCacheFastKey key2) {
    return ((key1.v0 ^ key2.v0 ) | (key1.v1 ^ key2.v1)) == 0 ;
}

static inline unsigned next_entry(IhtCache cache, unsigned index) {
    return (index + 1) & cache->entries_mask ;
}


#include <nmmintrin.h>

__attribute__((target("sse4.2")))
static inline uint32_t fast_key_hash(IhtCacheFastKey key)
{
    static const int32_t SEED_32 = 0x9e377989 ;       // Knuth 32-bit golden ratio

    if ( LIKELY(use_crc) ) {
        int crc = SEED_32 ;
#ifdef __x86_64__
        crc = _mm_crc32_u64(crc, key.v0) ;
        crc = _mm_crc32_u64(crc, key.v1) ;
#else
        crc = _mm_crc32_u32(crc, (int32_t) (key.v0>>32));
        crc = _mm_crc32_u32(crc, (int32_t) (key.));
        crc = _mm_crc32_u32(crc, (int32_t) (lo>>32));
        crc = _mm_crc32_u32(crc, (int32_t) (lo));
#endif
        return crc ;

    // Without support for SSE4.2 - no CRC instructions.
    } else {
        uint64_t h = key.v0 ^ (key.v1 + 0x9e3779b97f4a7c15ULL);
        h *= 0x9e3779b97f4a7c15ULL;
        h ^= h >> 32;
        return (uint32_t)h;
    }
}

static inline uint32_t key_hash(IhtCache cache, const void *key)
{
    if ( cache->short_key ) {
        memcpy( &cache->work_key, key, cache->key_size) ;
        return fast_key_hash ( cache->work_key) ;
    } else if ( cache->fast_key ) {
        return fast_key_hash( *(IhtCacheFastKey *) key) ;
    }

    // Use simple hashing with via rotation
    static uint64_t SEED_64 = 0x9e3779b97f4a7c15ULL ;

    const uint64_t *data = key ;
    int count = cache->key_size/sizeof(*data) ;
    uint64_t h = SEED_64 ;
    for (int i = 0 ; i<count ; i++ ) {
        h ^= data[i];
        h *= 0x9e3779b97f4a7c15ULL;
    }

    // final avalanche + reduce to 32-bit
    h ^= h >> 32;
    h ^= h >> 16;
    return (uint32_t)h;
}

static inline void bump_counter(IhtCounter *c, int scans)
{
    c->count++ ;
    c->scans += scans ;  
}

static inline void touch_entry(IhtCache cache, IhtEntry e, int scans) {
    bump_counter(&cache->stats.hits, scans) ;
    if ( e->state < SLOT_MAX_AGE ) {
        e->state += 1 ;
    }
}

static void setup(IhtCache cache) {
    // Initialization logic for the cache
    int capacity = cache->min_capacity;
    if ( capacity < MIN_CAPACITY) capacity = MIN_CAPACITY;

    int min_entries = (capacity+1) / cache->max_load_factor - 1;

    // round up to next power of two for item_count
    int max_entries = 1;
    while (max_entries < min_entries) max_entries *= 2;

    cache->item_count = 0;
    cache->max_entries = max_entries;
    cache->entries_mask = max_entries - 1;
    cache->max_items = max_entries * cache->max_load_factor;

    cache->short_key = (cache->key_size < sizeof(IhtCacheFastKey)) ;
    cache->fast_key = (cache->key_size <= sizeof(IhtCacheFastKey));
    cache->fast_value = (cache->value_size <= sizeof(IhtCacheFastValue));

    bool fast_mode = cache->fast_mode = cache->fast_key && cache->fast_value ;
    
    cache->key_offset = offsetof(struct iht_item, key);
    cache->value_offset = offsetof(struct iht_item, value);
    cache->item_size = sizeof(struct iht_item);

    if ( !fast_mode ) {
        int max_align = alignof(max_align_t) ;
        if ( cache->key_offset < cache->value_offset && !cache->fast_key) {
            int adj = max_align*(1+(cache->value_size - sizeof(IhtCacheFastValue)-1)/max_align) ;
            cache->value_offset += adj ;
            cache->item_size += adj ;
        } else if ( cache->key_offset > cache->value_offset && !cache->fast_value ) {
            int adj = max_align*(1+(cache->key_size - sizeof(IhtCacheFastKey)-1)/max_align) ;
            cache->key_offset += adj ;
            cache->item_size += adj ;
        }
    }

}

static void allocate(IhtCache cache) {
    // Memory allocation logic for entries and items
    cache->entries = calloc(cache->max_entries, sizeof(*cache->entries));
    cache->items = calloc(cache->max_items, cache->item_size);
    int na_size = cache->fast_value ? sizeof(IhtCacheFastValue) : cache->value_size ;
    if ( !cache->na_value) cache->na_value = calloc(1, na_size) ;
}

static void deallocate(IhtCache cache) {
    free(cache->entries);
    cache->entries = NULL;
    free(cache->items);
    cache->items = NULL;
}

static void remove_all(IhtCache cache) {
    // Logic to remove all entries from the cache
    if ( cache->value_destroyer ) {
        for (int i = 0; i < cache->max_entries; i++) {
            IhtEntry e = entry_addr(cache, i);
            if ( e->state > SLOT_REMOVED ) {
                cache->value_destroyer(cache->cxt, item_value(cache, e->item_index));
            }
        }
    }
    cache->item_count = 0;
    bzero(cache->entries, cache->max_entries * sizeof(*cache->entries));
    bzero(cache->items, cache->max_items * cache->item_size);
}

static IhtEntry lookup_entry(IhtCache cache, const void *key) {
    // Logic to look up an entry by key
    unsigned hash = key_hash(cache, key);
    unsigned index = hash & cache->entries_mask;
    IhtEntry e = entry_addr(cache, index) ;
    cache->stats.lookups++ ;
    int scans = 0 ;
    while ( !empty_slot(e->state) ) {
        if ( e->hash_value == hash ) {
            if ( key_equals(cache, item_key(cache, e->item_index), key) ) {
                touch_entry(cache, e, scans);
                return e ;
            }
        }
        index = next_entry(cache, index) ;
        e = entry_addr(cache, index) ;
        scans++ ;
    }
    bump_counter(&cache->stats.misses, scans);
    return NULL; // Not found
}

static IhtEntry fast_lookup_entry(IhtCache cache, IhtCacheFastKey key) {
    // Logic to look up an entry by key
    unsigned hash = fast_key_hash(key);
    unsigned index = hash & cache->entries_mask;
    IhtEntry e = entry_addr(cache, index) ;
    cache->stats.lookups++ ;
    int scans = 0 ;

    while ( !empty_slot(e->state) ) {
        if ( LIKELY(e->hash_value == hash) ) {
            if ( LIKELY(fast_key_equals( cache->items[e->item_index].key, key)) ) {
                touch_entry(cache, e, scans);
                return e ;
            }
        }   
        index = next_entry(cache, index) ;
        e = entry_addr(cache, index) ;
        scans++ ;
    }
    bump_counter(&cache->stats.misses, scans);
    return NULL; // Not found
}

static IhtEntry find_victim(IhtCache cache) {
    IhtEntry victim = NULL ;
    SlotState victim_state = SLOT_MAX_AGE + 1 ;
    int scans = 0 ;
    int index = cache->victim_index ;

    for (int search = MAX_EVICTION_SEARCH ; search > 0 ; scans++, index = next_entry(cache, index) ) {
        IhtEntry e = entry_addr(cache, index) ;
        int slot_state = e->state ;
        if ( empty_slot(slot_state) ) continue ;
        if ( slot_state == SLOT_MIN_AGE ) {
            victim = e ;
            break ;
        }
        search-- ;
        if ( slot_state < victim_state ) {
            victim = e ;
            victim_state = slot_state ;
        } ;
        e->state = slot_state = (slot_state-1) ;
        // Limit scan to slow evictions
        search-- ;
    }
    cache->victim_index = index ;
    bump_counter(&cache->stats.evictions, scans);

    return victim ;
}

static IhtEntry alloc_new_entry(IhtCache cache, const void *key)
{
    IhtEntry victim = NULL ;
    struct iht_entry victim_entry ;
    int new_entry_index = cache->item_count ;

    if ( new_entry_index >= cache->max_items ) {
        victim = find_victim(cache) ;
        // victim is saved for the unlikley case that the key is already in the cache
        // in this case, the victim will be resurretced .
        victim_entry = *victim ;
        *victim = (struct iht_entry) {} ;
        cache->item_count--;
        new_entry_index = victim_entry.item_index ;
    }

    unsigned hash_value = key_hash(cache, key); // TODO: Implement a proper hash function
    unsigned index = hash_value & cache->entries_mask;
    IhtEntry e = entry_addr(cache, index) ;
    int scans = 0 ;
    while ( e->state > SLOT_REMOVED ) {
        // Very unlikely, key may already be in the table, in this case, we need to undo
        // the removal - restore the victim, and assume the existing location is where
        // the item will be inserted.
        if ( e->hash_value == hash_value && key_equals(cache, item_key(cache, e->item_index), key)) {
            // Should we update existing entry?
            // In general, not expecting to be here
            if ( victim ) {
                *victim = victim_entry ;
                cache->item_count++ ;
            }

            bump_counter(&cache->stats.updates, scans);
            return e ; // Found existing entry
        }
        index = next_entry(cache, index) ;
        e = entry_addr(cache, index) ;
        scans++ ;
    };

    // e is populated with the new entry data
    *e = (struct iht_entry) { .hash_value = hash_value, .item_index = new_entry_index, .state = SLOT_MIN_AGE} ;

    bump_counter(&cache->stats.adds, scans) ;
    cache->item_count++;
    return e ;
}

static void store_item(IhtCache cache, int item_index, const void *key, const char *value) {
    char *entry_space = item_addr(cache, item_index) ;
    memcpy(entry_space + cache->key_offset, key, cache->key_size) ;
    memcpy(entry_space + cache->value_offset, value, cache->value_size) ;
}    

static IhtEntry calc_new_entry(IhtCache cache, const void *key) {
    // Logic to calculate and store a new entry
    if ( !cache->filler ) return NULL ; // No filler available

    alignas(max_align_t) char value_space[cache->value_size] ;
    if ( !cache->filler(cache->cxt, key, value_space) ) {
        return NULL ; // Filler failed
    }

    IhtEntry e = alloc_new_entry(cache, key) ;
    store_item(cache, e->item_index, key, value_space) ;

    return e ;
}
    
// Public API functions

IhtCache ihtCacheCreate(int min_capacity, int key_size, int value_sz, ihtCacheFiller filler, void *cxt)
{
    static bool done ;
    if (  !done ) {
        use_crc = __builtin_cpu_supports("sse4.2") ;
        done = true ;
    }

    IhtCache cache = calloc(1, sizeof(*cache));
    cache->min_capacity = min_capacity;
    cache->key_size = key_size;
    cache->value_size = value_sz;
    cache->max_load_factor = DEFAULT_LOAD_FACTOR;
    cache->filler = filler;
    cache->cxt = cxt;

    setup(cache);
    allocate(cache);

    return cache;
}

void ihtCacheRemoveAll(IhtCache cache)
{
    remove_all(cache);
}

void ihtCacheDestroy(IhtCache cache) 
{
    remove_all(cache);
    deallocate(cache);
    if ( cache->cxt_destroyer ) {
        cache->cxt_destroyer(cache->cxt);
    }
    free(cache->na_value);
    free(cache);
}


bool ihtCacheHasFiller(IhtCache cache)
{
    return cache->filler != NULL ;
}

int ihtCacheGetItemCount(IhtCache cache)
{
    return cache->item_count ;
}

int ihtCacheGetMaxItems(IhtCache cache)
{
    return cache->max_items ;
}

int ihtCacheGetKeySize(IhtCache cache)
{
    return cache->key_size ;
}
int ihtCacheGetValueSize(IhtCache cache)
{
    return cache->value_size ;
}
double ihtCacheGetMaxLoadFactor(IhtCache cache)
{
    return cache->max_load_factor ;
}
void ihtCacheSetMaxLoadFactor(IhtCache cache, double max_load_factor)
{
    cache->max_load_factor = max_load_factor ;
}
void ihtCacheSetMinCapacity(IhtCache cache, int min_capacity)
{
    cache->min_capacity = min_capacity ;
}
void ihtCacheSetCxtDestroyer(IhtCache cache, ihtCacheCxtDestroyer cxt_destroyer)
{
    cache->cxt_destroyer = cxt_destroyer ;
}
void ihtCacheSetValueDestroyer(IhtCache cache, ihtCacheValueDestroyer value_destroyer)
{
    cache->value_destroyer = value_destroyer ;
}
void ihtCacheSetNAValue(IhtCache cache, const void *na_value)
{
    if ( na_value ) {
        memcpy(cache->na_value, na_value, cache->value_size) ;
    } else {
        bzero(cache->na_value, cache->value_size) ;
    }
}
void ihtCacheReconfigure(IhtCache cache)
{
    remove_all(cache);
    deallocate(cache);
    setup(cache);
    allocate(cache);
}

void ihtCacheClearStats(IhtCache cache)
{
    cache->stats = (struct iht_stats){} ;
}

// Basic get, put, and lookup functions

bool ihtCacheFetch(IhtCache cache, const void *key, void *value_out)
{
    IhtEntry e = lookup_entry(cache, key);
    if ( !e ) {
        e = calc_new_entry(cache, key) ;
        if ( !e ) return false ;
    }
    memcpy(value_out, item_value(cache, e->item_index), cache->value_size) ;
    return true ;
}

bool ihtCachePut(IhtCache cache, const void *key, const void *value)
{
    IhtEntry e = alloc_new_entry(cache, key) ;
    if ( !e ) return false ;
    store_item(cache, e->item_index, key, value) ;
}   

bool ihtCacheLookup(IhtCache cache, const void *key, void *value_out)
{
    IhtEntry e = lookup_entry(cache, key);
    if ( !e ) return false ;
    memcpy(value_out, item_value(cache, e->item_index), cache->value_size) ;
    return true ;
}

void *ihtCacheGet(IhtCache cache, const void *key)
{
    IhtEntry e = lookup_entry(cache, key);
    if ( !e ) {
        e = calc_new_entry(cache, key) ;
        if ( !e ) return NULL ;
    }
    return item_value(cache, e->item_index) ;
}

IhtCacheFastValue ihtCacheGet_Fast(IhtCache cache, IhtCacheFastKey key)
{
    IhtEntry e = fast_lookup_entry(cache, key) ;
    if ( !e ) {
        e = calc_new_entry(cache, &key) ;
        if ( UNLIKELY(!e) ) return *(IhtCacheFastValue *) cache->na_value ;
    }
    return *(IhtCacheFastValue *) item_value(cache, e->item_index) ;
}

static void print_counter(FILE *fp, const char *label, IhtCounter counter)
{
    double ratio = counter.count>0 ? (double) counter.scans/counter.count : -1 ;
    fprintf(fp, "   %s: %d (scans=%d, ratio=%.2f)\n", label, counter.count, counter.scans, ratio) ;
}

void ihtCachePrintStats(FILE *fp, IhtCache cache, const char *label)
{
    struct iht_stats *stats = &cache->stats ;
    fprintf(fp, "Cache Stats(%s): lookups: %d hit=%.2f miss=%.2f\n", label, stats->lookups,
        100.0*stats->hits.count/(stats->lookups+0.001),
        100.0*stats->misses.count/(stats->lookups+0.001));
    print_counter(fp, "hits", stats->hits) ;
    print_counter(fp, "misses", stats->misses) ;
    print_counter(fp, "adds", stats->adds) ;
    print_counter(fp, "updates", stats->updates) ;
    print_counter(fp, "evictions", stats->evictions);
}