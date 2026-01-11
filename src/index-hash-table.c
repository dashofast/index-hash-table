#include "index-hash-table.h"

#include <stddef.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


#define MIN_CAPACITY 16
#define DEFAULT_LOAD_FACTOR 0.40
#define MAX_EVICTION_SEARCH 16

#define KNUTH_GOLD_32 0x9e377989                 // Knuth 32-bit golden ratio
#define KNUTH_GOLD_64 0x9e3779b97f4a7c15ULL      // Knuth 64-bit golden ratio

#define int_sizeof(x) (int) sizeof(x)

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) (__builtin_expect(!!(x), 1))
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define LIKLEY(x) (!!(x))
#define UNLIKLEY(x) (!!(x))
#endif

typedef enum SLOT_STATE { SLOT_EMPTY = 0, SLOT_REMOVED = 1, SLOT_MIN_AGE = 2, SLOT_MAX_AGE = 7 } SlotState ;

// set to SLOT_MIN_AGEA+n to make it more LRU.
static SlotState INITIAL_STATE = SLOT_MIN_AGE ; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static inline bool empty_slot(SlotState slot_state) {
    return slot_state <= SLOT_REMOVED ;
}

typedef struct iht_entry {
    uint32_t hash_value ;  // cached hash value
    int item_index ;
//    SlotState state:8 ;
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
    double max_load_factor ;
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
    int evict_index ;          // index of next victim for eviction

    void *na_value ;            // value representing NA
    IhtCacheFastKey work_key ;  // Zero-padded key

    // Storage
    unsigned char *states ;     // [max entries]
    IhtEntry entries ;          // [max_entries]
    IhtItem items ;             // [max_items] of item_size bytes
    

    struct iht_stats stats ;
} ;

static bool use_crc ; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)


// Accessors
static inline void *item_addr(IhtCache cache, int item_index) {
    return ((char*)cache->items) + ((ptrdiff_t) cache->item_size*item_index) ;
}

static inline void *item_value(IhtCache cache, int item_index) {
    return ((char*) item_addr(cache, item_index)) + cache->value_offset ;
}

static inline void *item_key(IhtCache cache, int item_index) {
    return ((char*) item_addr(cache, item_index)) + cache->key_offset ;
}

static inline bool is_slot_empty(IhtCache cache, unsigned entry_index) {
    return empty_slot( (SlotState) cache->states[entry_index] ) ;
}

static inline bool is_slot_used(IhtCache cache, unsigned entry_index) {
    return !empty_slot( (SlotState) cache->states[entry_index] ) ;
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

static inline int next_entry(IhtCache cache, int index) {
    return (index + 1) & cache->entries_mask ;
}

// NOLINT((llvm-include-order)
#include <nmmintrin.h>

__attribute__((target("sse4.2")))
static inline uint32_t fast_key_hash(IhtCacheFastKey key)
{
    if ( LIKELY(use_crc) ) {
        uint32_t crc = KNUTH_GOLD_32 ;
#ifdef __x86_64__
        crc = (uint32_t) _mm_crc32_u64(crc, key.v0) ;
        crc = (uint32_t) _mm_crc32_u64(crc, key.v1) ;
#else
        crc = _mm_crc32_u32(crc, (int32_t) (key.v0>>32));
        crc = _mm_crc32_u32(crc, (int32_t) (key.));
        crc = _mm_crc32_u32(crc, (int32_t) (lo>>32));
        crc = _mm_crc32_u32(crc, (int32_t) (lo));
#endif
        return crc ;

    // Without support for SSE4.2 - no CRC instructions.
    } 
    uint64_t h = key.v0 ^ (key.v1 + KNUTH_GOLD_64);
    h *= KNUTH_GOLD_64;
    // Reduce to 32 bit, mix high/low 16 bits.
    h ^= h >> UINT32_WIDTH ;   
    h ^= h >> UINT16_WIDTH ;
    return (uint32_t)h;
}

static inline uint32_t key_hash(IhtCache cache, const void *key)
{
    if ( cache->short_key ) {
        memcpy( &cache->work_key, key, cache->key_size) ;
        return fast_key_hash ( cache->work_key) ;
    } ;
    if ( cache->fast_key ) {
        return fast_key_hash( *(IhtCacheFastKey *) key) ;
    }

    int bytes = cache->key_size ;
    uint64_t h = KNUTH_GOLD_64 + bytes ;
    int pos = 0 ;
    for (pos = 0 ; pos<bytes ; pos+= sizeof(h) ) {
        h ^= *(uint64_t *) ((char *) key + pos ) ;
        h *= KNUTH_GOLD_64;
    }
    int n_tail = bytes - pos ;
    if ( n_tail > 0 ) {
        uint64_t tail = 0 ;
        memcpy( &tail, (char *) key + pos, n_tail ) ;
        h ^= tail ;
        h *= KNUTH_GOLD_64;        
    }

    // Reduce to 32 bit, mix high/low 16 bits.
    h ^= h >> UINT32_WIDTH ;   
    h ^= h >> UINT16_WIDTH ;
    return (uint32_t)h;
}

static inline void bump_counter(IhtCounter *c, int scans)
{
    c->count++ ;
    c->scans += scans ;  
}

static inline void touch_entry(IhtCache cache, int index) {
    SlotState state = cache->states[index] ;
    if ( state < SLOT_MAX_AGE ) {
        cache->states[index] = state+1 ;
    }
}

static void setup(IhtCache cache) {
    // Initialization logic for the cache
    int capacity = cache->min_capacity;
    if ( capacity < MIN_CAPACITY) capacity = MIN_CAPACITY;

    int min_entries = (int) ceil(capacity/cache->max_load_factor) ;

    // round up to next power of two for item_count
    int max_entries = 1;
    while (max_entries < min_entries) max_entries *= 2;

    cache->item_count = 0;
    cache->max_entries = max_entries;
    cache->entries_mask = max_entries - 1;
    cache->max_items = (int) (max_entries * cache->max_load_factor);

    cache->short_key = (cache->key_size < int_sizeof(IhtCacheFastKey)) ;
    cache->fast_key = (cache->key_size <= int_sizeof(IhtCacheFastKey));
    cache->fast_value = (cache->value_size <= int_sizeof(IhtCacheFastValue));

    bool fast_mode = cache->fast_mode = cache->fast_key && cache->fast_value ;
    
    cache->key_offset = offsetof(struct iht_item, key);
    cache->value_offset = offsetof(struct iht_item, value);
    cache->item_size = sizeof(struct iht_item);

    if ( !fast_mode ) {
        cache->item_size = cache->key_size + cache->value_size ;
        int max_align = alignof(max_align_t) ;
        if ( cache->key_offset < cache->value_offset && !cache->fast_key) {
            int adj = max_align*(1+(cache->value_size - int_sizeof(IhtCacheFastValue)-1)/max_align) ;
            cache->value_offset += adj ;
            cache->item_size += adj ;
        } else if ( cache->key_offset > cache->value_offset && !cache->fast_value ) {
            int adj = max_align*(1+(cache->key_size - int_sizeof(IhtCacheFastKey)-1)/max_align) ;
            cache->key_offset += adj ;
            cache->item_size += adj ;
        }
    }

}

static void allocate(IhtCache cache) {
    // Memory allocation logic for entries and items
    cache->entries = calloc(cache->max_entries, sizeof(*cache->entries));
    cache->items = calloc(cache->max_items, cache->item_size);
    cache->states = calloc(cache->max_entries, sizeof(*cache->states));
    int na_size = cache->fast_value ? int_sizeof(IhtCacheFastValue) : cache->value_size ;
    if ( !cache->na_value) cache->na_value = calloc(1, na_size) ;
}

static void deallocate(IhtCache cache) {
    free(cache->entries);
    cache->entries = NULL;
    free(cache->items);
    cache->items = NULL;
    free(cache->states);
    cache->states = NULL;
}

static void remove_all(IhtCache cache) {
    // Logic to remove all entries from the cache
    if ( cache->value_destroyer ) {
        for (int i = 0; i < cache->max_entries; i++) {
            if ( !is_slot_empty(cache, i) ) {
                IhtEntry e = entry_addr(cache, i);
                cache->value_destroyer(cache->cxt, item_value(cache, e->item_index));
            }
        }
    }
    cache->item_count = 0;
    bzero(cache->entries, cache->max_entries * sizeof(*cache->entries));
    bzero(cache->states, cache->max_entries * sizeof(*cache->states));
    bzero(cache->items, cache->max_items * (size_t) cache->item_size);
}

static IhtEntry lookup_entry(IhtCache cache, const void *key) {
    // Logic to look up an entry by key
    unsigned hash = key_hash(cache, key);
    int index = hash & cache->entries_mask;
    IhtEntry e = entry_addr(cache, index) ;
    cache->stats.lookups++ ;
    int scans = 0 ;
    while ( is_slot_used(cache, index) ) {
        if ( e->hash_value == hash ) {
            if ( key_equals(cache, item_key(cache, e->item_index), key) ) {
                bump_counter(&cache->stats.hits, scans) ;
                touch_entry(cache, index) ;
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
    cache->stats.lookups++ ;

    // Unroll the first check, mostly likely to be a hit
    SlotState state = cache->states[index] ;
    if ( UNLIKELY(empty_slot(state)) ) {
        bump_counter(&cache->stats.misses, 0);
        return NULL ;
    }

    IhtEntry e = &cache->entries[index] ;
    if ( LIKELY(e->hash_value == hash) ) {
        if ( LIKELY(fast_key_equals( cache->items[e->item_index].key, key)) ) {
            bump_counter(&cache->stats.hits, 0) ;
            if ( state < SLOT_MAX_AGE ) cache->states[index] = state+1 ;
            return e ;
        }
    }

    index = next_entry(cache, index) ;
    int scans = 1 ;

    while ( is_slot_used(cache, index) ) {
        e = &cache->entries[index] ;
        if ( LIKELY(e->hash_value == hash) ) {
            if ( LIKELY(fast_key_equals( cache->items[e->item_index].key, key)) ) {
                bump_counter(&cache->stats.hits, scans) ;
                touch_entry(cache, index);
                return e ;
            }   
        }   
        index = next_entry(cache, index) ;
        scans++ ;
    }
    bump_counter(&cache->stats.misses, scans);
    return NULL; // Not found
}

static int find_victim(IhtCache cache) {
    SlotState victim_state = SLOT_MAX_AGE + 1 ;
    int scans = 0 ;
    int index = cache->evict_index ;
    int victim_index = index ;

    for (int search = MAX_EVICTION_SEARCH ; search > 0 ; scans++, index = next_entry(cache, index) ) {
        SlotState slot_state = cache->states[index];
        if ( empty_slot(slot_state) ) continue ;
        if ( slot_state < victim_state ) {
            victim_index = index ;
            victim_state = slot_state ;
            if ( victim_state == SLOT_MIN_AGE ) {
                search = 0 ;
                continue ;
            }
        } ;
        cache->states[index] = slot_state - 1 ;
        // Limit scan to slow evictions
        search-- ;
    }
    cache->evict_index = index ;
    bump_counter(&cache->stats.evictions, scans);

    return victim_index ;
}

static IhtEntry alloc_new_entry(IhtCache cache, const void *key)
{
//    IhtEntry victim = NULL ;
    int victim_index = -1 ;
    SlotState victim_state = SLOT_EMPTY ;
    //struct iht_entry victim_entry ;
    int new_entry_index = cache->item_count ;

    if ( LIKELY(new_entry_index >= cache->max_items )) {
//        victim = find_victim(cache) ;
        // victim is saved for the unlikley case that the key is already in the cache
        // in this case, the victim will be resurretced .
        victim_index = find_victim(cache) ;
        victim_state = cache->states[victim_index] ;
        cache->states[victim_index] = SLOT_EMPTY ;
        //        victim_entry = *victim ;
//        *victim = (struct iht_entry) {} ;
        cache->item_count--;
        new_entry_index = cache->entries[victim_index].item_index ;
    }

    unsigned hash_value = key_hash(cache, key); 
    unsigned index = hash_value & cache->entries_mask;
    IhtEntry e = entry_addr(cache, index) ;
    int scans = 0 ;
    while ( is_slot_used(cache, index) ) {
        // Very unlikely, key may already be in the table, in this case, we need to undo
        // the removal - restore the victim, and assume the existing location is where
        // the item will be inserted.
        if ( UNLIKELY(e->hash_value == hash_value && key_equals(cache, item_key(cache, e->item_index), key))) {
            if ( victim_index >= 0 ) {
                cache->states[victim_index] = victim_state ;
//                *victim = victim_entry ;
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
    *e = (struct iht_entry) { .hash_value = hash_value, .item_index = new_entry_index} ;
    cache->states[index] = INITIAL_STATE ;

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
    cache->stats = (struct iht_stats) {} ;
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
    return true ;
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
    if ( UNLIKELY(!e) ) {
        e = calc_new_entry(cache, &key) ;
        if ( UNLIKELY(!e) ) return *(IhtCacheFastValue *) cache->na_value ;
    }
    return cache->items[e->item_index].value ;
}

static void print_counter(FILE *fp, const char *label, IhtCounter counter, int indent)
{
    double ratio = counter.count>0 ? (double) counter.scans/counter.count : -1 ;
    fprintf(fp, "%*s%s: %d (scans=%d, ratio=%.2f)\n", indent*2, "", label, counter.count, counter.scans, ratio) ;
}

void ihtCachePrintStats(FILE *fp, IhtCache cache, const char *label)
{
    return ihtCachePrintStats1(fp, cache, label, true, 2) ;
}


void ihtCachePrintStats1(FILE *fp, IhtCache cache, const char *label, int indent, int show_stats)
{
    struct iht_stats *stats = &cache->stats;
    fprintf(fp, "%*s%s: Cache Stats: lookups: %d hit=%.2f miss=%.2f\n", indent, "", label,
        stats->lookups,
        (100.0 * stats->hits.count) / (stats->lookups + !stats->lookups),
        (100.0 * stats->misses.count) / (stats->lookups + !stats->lookups));
    if  (show_stats>=2) {
        print_counter(fp, "hits", stats->hits, indent);
        print_counter(fp, "misses", stats->misses, indent);
        print_counter(fp, "adds", stats->adds, indent);
        print_counter(fp, "updates", stats->updates, indent);
        print_counter(fp, "evictions", stats->evictions, indent);
    }
}