/**
 * @file
 * @brief Test iht cache fast key/value API (1K, double key, double value)
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <getopt.h>
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

static void check_test(const char *test_name, double dt, double expected, double result)
{
    double error = 2*(result - expected)/(expected + result) ;
    fprintf(stderr, "%s (%.3f seconds): Error=%.2f (V=%.3f)\n", test_name, dt, 100.0*error, result) ;
}

static void show_test_details(IhtCache c, const char *test_name, int show_stats)
{
    if ( !show_stats) return ;
    ihtCachePrintStats1(stdout, c, test_name, 2, show_stats) ;
}

static inline double vv(int pos, int count)
{
    return 0.5 + (9.5*(pos%count))/count ;
}

static inline double mult2(double x)
{
    return x + x ;
}

static double test_nop(int N, int R)
{
    double start_t = time_mono() ;
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = mult2(x);
            s += y ;
        }
    }
    double end_t = time_mono() ;
    double result = s/R/N ;
    printf("%s (%.3f seconds): V=%.3f\n", __func__, end_t - start_t, result) ;
    return result ;
}

static double test_exp(int N, int R)
{
    double start_t = time_mono() ;
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = exp(x) ;
            s += y ;
        }
    }
    double end_t = time_mono() ;
    double result = s/R/N ;
    printf("%s (%.3f seconds): V=%.3f\n", __func__, end_t - start_t, result) ;
    return result ;
}

static bool nop_wrapper(void *cxt, const void *param, void *result)
{
    (void) cxt ;
    double x = *(double*) param ;
    double v = mult2(x) ;
    *(double *) result = v ;
    return true ;
}

static bool exp_wrapper(void *cxt, const void *param, void *result)
{
    (void) cxt ;
    double x = *(double*) param ;
    double v = exp(x) ;
    *(double *) result = v ;
    return true ;
}

void test_cache_nop(int N, int R, double s0, int show_stats)
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), nop_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+r%100, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_mono() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

void test_cache_exp(int N, int R, double s0, int show_stats )
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_mono() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

// Test with smaller cache (N/2)
void test_cache_too_small(int N, int R, double s0, int show_stats)
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N/2, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_mono() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

// Test with higher load factor (0.75)
void test_cache_high_load(int N, int R, double s0, int show_stats)
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    ihtCacheSetMaxLoadFactor(c, 0.9) ;
    ihtCacheReconfigure(c);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = r%100 ;
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, 100+N) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_mono() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

void test_cache_shift(int N, int R, double s0, int show_stats)
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (10*N*r)/R  ;    // shift every R/10 rounds by N/2
        for (int i=0 ; i<N ; i++ ) {
            double x = vv(i+b, N+10*N) ;
            double y = ihtCacheGet_D_D(c, x);
            s += y ;
        }
    }
    double end_t = time_mono() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

void test_cache_noise(int N, int R, double s0, int show_stats)
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (10*N*r)/R  ;    // shift every R/10 rounds by N/2
        for (int i=0 ; i<N ; i++ ) {
            double x = i%100 ? vv(i+b, N+10*N) : vv(r+1, R+1) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_mono() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

void test_cache_fuzzy(int N, int R, double s0, int show_stats)
{
    double start_t = time_mono() ;
    IhtCache c = ihtCacheCreate(N, sizeof(double), sizeof(double), exp_wrapper, NULL);
    double s = 0 ;
    for (int r = 0 ; r<R ; r++ ) {
        int b = (10*N*r)/R  ;    // shift every R/10 rounds by N/2
        for (int i=0 ; i<N ; i++ ) {
            double x = i%3 ? vv(i+b, N+10*N) : vv(r+1, R+1) ;
            double y = ihtCacheGet_D_D(c, x) ;
            s += y ;
        }
    }
    double end_t = time_hires() ;
    check_test(__func__, end_t - start_t, s0, s/R/N) ;
    show_test_details(c, __func__, show_stats) ;
    ihtCacheDestroy(c) ;
}

// Invoke with '-nN' and '-rR' to set N and R valuee
// Default

int main(int argc, char **argv) {
    int N = 1000 ;
    int R = 1000 ;
    int show_stats = 1 ;
    int opt ;
    while ( (opt=getopt(argc, argv, "Dsn:r:")) != -1 ) {
        switch ( opt ) {
            case 'n':
                N = atoi(optarg) ;
                break ;
            case 'r':
                R = atoi(optarg) ;
                break ;
            case 'q':
                show_stats = 0 ;
                break ;
            case 's':
                show_stats = 2 ;
                break ;
            default:
                fprintf(stderr, "Unknown option: %c\n", optopt) ;
                exit(2) ;
        }
    }
    
    fprintf(stderr, "Test IHT Fast Cache (N=%d,R=%d)\n", N, R) ;
    double nop_result = test_nop(N, R) ;
    double exp_result = test_exp(N, R) ;
    test_cache_nop(N, R, nop_result, show_stats) ;
    test_cache_exp(N, R, exp_result, show_stats) ;
    test_cache_too_small(N, R, exp_result, show_stats) ;
    test_cache_high_load(N, R, exp_result, show_stats) ;
    test_cache_shift(N, R, exp_result, show_stats) ;
    test_cache_noise(N, R, exp_result, show_stats) ;
    test_cache_fuzzy(N, R, exp_result, show_stats);
    return 0;
}

