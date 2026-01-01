#include <stdio.h>
#include <math.h>

#include <time.h>

#include "index-hash-table.h"

struct t_key {
    double a, b, c, d ;
} ;

struct t_value {
    double x, y, z, u ;
} ;

static inline double time_hires(void)
{
    struct timespec ts ;
    clock_gettime(CLOCK_MONOTONIC, &ts) ;
    double now = (ts.tv_sec) + (ts.tv_nsec/1e9) ;
    return now ;
}

static double time_mono(void)
{
    static double base_time ;
    double now = time_hires() ;
    if ( base_time == 0 ) base_time =now ;
    return now - base_time ;
}

#define R 10000
#define N 1000

static void set_key(int pos, int count, struct t_key *key)
{
    double v = 0.5 + (9.5*(pos%count))/count ;
    key->a = v ;
    key->b = v + 1.0 ;
    key->c = v + 2.0 ;
    key->d = v + 3.0 ;
}

static void calc_value(const struct t_key *key, struct t_value *value)
{
    double v = exp(key->a) ;
    value->x = v + 0 ;
    value->y = v + 1 ;
    value->z = v + 2 ;
    value->u = v + 3 ;
}

static void nop_value(const struct t_key *key, struct t_value *value)
{
    value->x = key->a ;
    value->y = key->b ;
    value->z = key->c ;
    value->u = key->d ;
}

void test_nop(void)
{
    double start_t = time_hires() ;
    double s = 0 ;
    struct t_key key ;
    struct t_value value ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            set_key(i+r%100, 100+N, &key) ;
            nop_value(&key, &value) ;
            s += value.y ;
        }
    }
    double end_t = time_hires() ;
    fprintf(stderr, "%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N);
}

void test_exp(void)
{
    double start_t = time_hires() ;
    double s = 0 ;
    struct t_key key ;
    struct t_value value ;

    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            set_key(i+r%100, 100+N, &key) ;
            calc_value(&key, &value) ;
            s += value.y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
}

static bool nop_wrapper(void *cxt, const void *param, void *result)
{
    struct t_key *key = (struct t_key *) param ;
    struct t_value *value = (struct t_value *) result ;
    nop_value(key, value) ;
    return true ;
}

void test_cache_nop(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(struct t_key), sizeof(struct t_value), nop_wrapper, NULL);
    double s = 0 ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            set_key(i+r%100, 100+N, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

static bool exp_wrapper(void *cxt, const void *param, void *result)
{
    struct t_key *key = (struct t_key *) param ;
    struct t_value *value = (struct t_value *) result ;
    calc_value(key, value) ;
    return true ;
}

void test_cache_exp(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(struct t_key), sizeof(struct t_value ), exp_wrapper, NULL);
    double s = 0 ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            set_key(i+b, 100+N, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    fprintf(stderr, "%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_half(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N/2, sizeof(struct t_key), sizeof(struct t_value ), exp_wrapper, NULL);
    double s = 0 ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            struct t_key key ;
            set_key(i+b, 100+N, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_pack(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(struct t_key), sizeof(struct t_value ), exp_wrapper, NULL);
    ihtCacheSetMaxLoadFactor(c, 0.75) ;
    ihtCacheReconfigure(c);
    double s = 0 ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            set_key(i+b, 100+N, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_shift(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(struct t_key), sizeof(struct t_value ), exp_wrapper, NULL);
    double s = 0 ;
    double inv_n = 1.0/N ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (r/100)*100 ;
        for (int i=0 ; i<N ; i++ ) {
            set_key(i+b, R+N, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    fprintf(stderr, "%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_noise(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(struct t_key), sizeof(struct t_value ), exp_wrapper, NULL);
    double s = 0 ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (r/100)*100 ;
        for (int i=0 ; i<N ; i++ ) {
            i ? set_key(i+b, R+N, &key) : set_key(r, R+1, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    fprintf(stderr, "%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_fuzzy(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(struct t_key), sizeof(struct t_value ), exp_wrapper, NULL);
    double s = 0 ;
    struct t_key key ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (r/100)*100 ;
        for (int i=0 ; i<N ; i++ ) {
            i%2 ? set_key(i+b, R+N, &key) : set_key(i+r, N+R+1, &key) ;
            struct t_value *value = ihtCacheGet(c, &key) ;
            s += value->y ;
        }
    }
    double end_t = time_hires() ;
    fprintf(stderr, "%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

int main() {

    test_nop() ;
    test_exp() ;
    test_cache_nop() ;
    test_cache_exp() ;
    test_cache_shift() ;
    test_cache_pack() ;
    test_cache_half() ;
    test_cache_noise() ;
    test_cache_fuzzy() ;
    return 0;
}

