#include <stdio.h>
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

static double time_mono(void)
{
    static double base_time ;
    double now = time_hires() ;
    if ( base_time == 0 ) base_time =now ;
    return now - base_time ;
}

#define R 100000
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
        s =  0 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = x+x ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s) ;
}

void test_exp(void)
{
    double start_t = time_hires() ;
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        s =  0 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = exp(x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s) ;
}

static bool nop_wrapper(void *cxt, const void *param, void *result)
{
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
        s =  0 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

static bool exp_wrapper(void *cxt, const void *param, void *result)
{
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
        s =  0 ;
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_shift(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    double inv_n = 1.0/N ;
    for (int r = 0 ; r<R ; r++ ) {
        s = 0 ;
        int b = (r%100)+(r/1000)*1000 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+R+N) ;
            double y = ihtCacheGet_D_D(c, x);
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

void test_cache_large(void)
{
    double start_t = time_hires() ;
    IhtCache c = ihtCacheCreate(N/2, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        s =  0 ;
        int b = (r%100)+(r/1000)*1000 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    printf("%s(R=%d,N=%d): (t=%.3f) = %f\n", __func__, R, N, end_t - start_t, s) ;
    ihtCachePrintStats(stdout, c, __func__) ;
    ihtCacheDestroy(c) ;
}

int main() {

    test_nop() ;
    test_exp() ;
    test_cache_nop() ;
    test_cache_exp() ;
    test_cache_shift() ;
    test_cache_large() ;
    return 0;
}

