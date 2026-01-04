/**
 * @file test_iht_small.c
 * @author dasho fast (dasho.fast@gmail.com)
 * @brief test iht API with small objects (1K, double key, double value)
 * @version 0.1
 * @date 2026-01-01
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include <math.h>
#include <time.h>
#include "index-hash-table.h"

static inline double time_hires(void)
{
    struct timespec ts ;
    clock_gettime(CLOCK_MONOTONIC, &ts) ;
    double now = (ts.tv_sec) + (ts.tv_nsec/1e9) ;
    return now ;
}

#define R 10000
#define N 1000

static inline double vv(int pos, int count)
{
    return 0.5 + (9.5*(pos%count))/count ;
}

void test_nop(void)
{
    double start_t = time_hires() ;
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = x+x ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    fprintf(stderr, "%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N);
}

void test_exp(void)
{
    double start_t = time_hires() ;
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = exp(x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
}

static bool nop_wrapper(void *cxt, const void *param, void *result)
{
    (void) cxt ;
    double x = *(double*) param ;
    double v = x+x ;
    *(double *) result = v ;
    return true ;
}

void test_cache_nop(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), nop_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s/R/N) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

static bool exp_wrapper(void *cxt, const void *param, void *result)
{
    (void) cxt ;
    double x = *(double*) param ;
    double v = exp(x) ;
    *(double *) result = v ;
    return true ;
}

void test_cache_exp(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double *y = ihtCacheGet(c, &x) ;
            s += *y ;
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
    IhtCache c = ihtCacheCreate(N/2, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double *y = ihtCacheGet(c, &x) ;
            s += *y ;
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
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    ihtCacheSetMaxLoadFactor(c, 0.75) ;
    ihtCacheReconfigure(c);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
           double *y = ihtCacheGet(c, &x) ;
            s += *y ;
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
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (r/100)*100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, R+N) ;
            double *y = ihtCacheGet(c, &x) ;
            s += *y ;
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
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (r/100)*100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = i ? vv(i+b, R+N) : vv(r, R+1) ;
            double *y = ihtCacheGet(c, &x) ;
            s += *y ;
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
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (r/100)*100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = i%2 ? vv(i+b, N+R) : vv(i+r, N+R+1) ;
            double *y = ihtCacheGet(c, &x) ;
            s += *y ;
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

