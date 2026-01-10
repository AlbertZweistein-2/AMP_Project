#pragma once
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include <omp.h>

#if VERSION == 1
    #include "Ex1.h"
#elif VERSION == 2
    #include "Ex2.h"
#elif VERSION == 4
    #include "Ex4b.h"
#elif VERSION == 5
    #include "Ex5.h"
#else
    #include "Ex1.h" // Default to Ex1 if VERSION is not defined
#endif

#define DEFAULT_FILENAME "benchmark_results.csv"

typedef enum {
    BATCH_SPEC_FIXED = 0,
    BATCH_SPEC_RANGE = 1,
} batch_spec_kind_t;

typedef struct {
    batch_spec_kind_t kind;
    int min;
    int max;
} batch_spec_t;

// Helper for running the benchmark: returns the batch size for one batch.
// For fixed specs, returns the fixed value; for ranges, samples uniformly from [min,max] (inclusive).

// error printer for pattern parsing
static void die_pattern(const char* pattern, const char* msg)
{
    fprintf(stderr, "Invalid batch size pattern '%s': %s\n", pattern ? pattern : "(null)", msg);
    exit(EXIT_FAILURE);
}

// Skip whitespace characters; returns pointer to first non-whitespace.
static const char* skip_ws(const char* s)
{
    while (s && *s && isspace((unsigned char)*s)) {
        ++s;
    }
    return s;
}

// parse integer token from *inout; updates *inout to point after the parsed integer.
static int parse_int_token(const char* pattern, const char** inout, int* out)
{
    const char* s = skip_ws(*inout);
    if (!s || !*s) {
        return 0;
    }

    errno = 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) {
        return 0;
    }
    if (errno != 0) {
        die_pattern(pattern, "integer out of range");
    }
    if (v < 0 || v > INT32_MAX) {
        die_pattern(pattern, "negative or too large integer");
    }

    *out = (int)v;
    *inout = end;
    return 1;
}

// parse one spec: either fixed integer or tuple (min,max)
static batch_spec_t parse_spec(const char* pattern, const char** inout)
{
    const char* s = skip_ws(*inout);
    if (!s || !*s) {
        die_pattern(pattern, "unexpected end of input");
    }

    if (*s == '(') {
        ++s;
        int min_v = 0;
        int max_v = 0;
        if (!parse_int_token(pattern, &s, &min_v)) {
            die_pattern(pattern, "expected integer min in tuple '(min,max)' ");
        }
        s = skip_ws(s);
        if (*s != ',') {
            die_pattern(pattern, "expected ',' in tuple '(min,max)'");
        }
        ++s;
        if (!parse_int_token(pattern, &s, &max_v)) {
            die_pattern(pattern, "expected integer max in tuple '(min,max)'");
        }
        s = skip_ws(s);
        if (*s != ')') {
            die_pattern(pattern, "expected ')' to close tuple '(min,max)'");
        }
        ++s;

        if (min_v < 0 || max_v < 0) {
            die_pattern(pattern, "negative sizes are invalid");
        }
        if (min_v > max_v) {
            die_pattern(pattern, "tuple requires min <= max");
        }

        *inout = s;
        return (batch_spec_t){ .kind = BATCH_SPEC_RANGE, .min = min_v, .max = max_v };
    }

    int v = 0;
    if (!parse_int_token(pattern, &s, &v)) {
        die_pattern(pattern, "expected integer or tuple '(min,max)'");
    }
    *inout = s;
    return (batch_spec_t){ .kind = BATCH_SPEC_FIXED, .min = v, .max = v };
}

// parse a list of specs: either [x0,x1,...] or {x0,x1,...}
static batch_spec_t* parse_spec_list(const char* pattern, const char* inner, char open_ch, char close_ch, int* out_len)
{
    const char* s = skip_ws(inner);
    if (!s || *s != open_ch) {
        die_pattern(pattern, "internal error: list expected");
    }
    ++s;

    int cap = 8;
    int len = 0;
    batch_spec_t* elems = (batch_spec_t*)malloc((size_t)cap * sizeof(batch_spec_t));
    if (!elems) {
        die_pattern(pattern, "out of memory");
    }

    s = skip_ws(s);
    if (*s == close_ch) {
        free(elems);
        die_pattern(pattern, "empty list is not allowed");
    }

    while (1) {
        if (len == cap) {
            cap *= 2;
            batch_spec_t* grown = (batch_spec_t*)realloc(elems, (size_t)cap * sizeof(batch_spec_t));
            if (!grown) {
                free(elems);
                die_pattern(pattern, "out of memory");
            }
            elems = grown;
        }

        elems[len++] = parse_spec(pattern, &s);
        s = skip_ws(s);

        if (*s == ',') {
            ++s;
            s = skip_ws(s);
            continue;
        }
        if (*s == close_ch) {
            ++s;
            break;
        }
        free(elems);
        die_pattern(pattern, "expected ',' or closing bracket");
    }

    s = skip_ws(s);
    if (*s != '\0') {
        free(elems);
        die_pattern(pattern, "trailing characters after pattern");
    }

    *out_len = len;
    return elems;
}

// parse batch size pattern string into per-thread specs
void parse_batch_size_pattern(const char* pattern, int num_threads, batch_spec_t** out_specs)
{
    if (!pattern || !out_specs) {
        die_pattern(pattern, "null input");
    }
    if (num_threads <= 0) {
        die_pattern(pattern, "num_threads must be > 0");
    }

    const char* s = skip_ws(pattern);
    if (!s || *s == '\0') {
        die_pattern(pattern, "empty pattern");
    }

    batch_spec_t* per_thread = (batch_spec_t*)malloc((size_t)num_threads * sizeof(batch_spec_t));
    if (!per_thread) {
        die_pattern(pattern, "out of memory");
    }

    // 1) Fixed integer or tuple: replicate to all threads
    if (*s != '[' && *s != '{') {
        const char* p = s;
        batch_spec_t one = parse_spec(pattern, &p);
        p = skip_ws(p);
        if (*p != '\0') {
            free(per_thread);
            die_pattern(pattern, "trailing characters after pattern");
        }
        for (int t = 0; t < num_threads; ++t) {
            per_thread[t] = one;
        }
        *out_specs = per_thread;
        return;
    }

    // 2) Repeating pattern list: [x0,x1,...]
    if (*s == '[') {
        int len = 0;
        batch_spec_t* elems = parse_spec_list(pattern, s, '[', ']', &len);
        for (int t = 0; t < num_threads; ++t) {
            per_thread[t] = elems[t % len];
        }
        free(elems);
        *out_specs = per_thread;
        return;
    }

    // 3) Explicit per-thread list: {x0,...,xT-1}
    if (*s == '{') {
        int len = 0;
        batch_spec_t* elems = parse_spec_list(pattern, s, '{', '}', &len);
        if (len != num_threads) {
            free(elems);
            free(per_thread);
            die_pattern(pattern, "explicit per-thread list must have exactly num_threads elements");
        }
        for (int t = 0; t < num_threads; ++t) {
            per_thread[t] = elems[t];
        }
        free(elems);
        *out_specs = per_thread;
        return;
    }

    free(per_thread);
    die_pattern(pattern, "unknown pattern format");
}

// generate random batch size according to spec
int batch_spec_sample(const batch_spec_t* spec, unsigned int *rng_state)
{
    if (!spec) {
        return 0;
    }
    if (spec->kind == BATCH_SPEC_FIXED) {
        return spec->min;
    }
    int lo = spec->min;
    int hi = spec->max;
    if (lo == hi) {
        return lo;
    }
    unsigned int r = rand_r(rng_state);
    unsigned int span = (unsigned int)(hi - lo + 1);
    return lo + (int)(r % span);
}

// parse command-line arguments
static void parse_args(int argc, char** argv,
                       int* out_num_threads,
                       int* out_num_repetitions,
                       int* out_time_interval,
                       batch_spec_t** out_enq_specs,
                       batch_spec_t** out_deq_specs,
                       char** out_filename)
{
    // 1) Validate argc
    if(argc < 6 || argc > 7) {
        printf("Usage: %s <num_threads> <num_repetitions> <time_interval> <enq_batch_size> <deq_batch_size>\n", argv[0]);
        // Inform about enqueue/dequeue batch size patterns
        printf("Batch size patterns (entered as strings / CLI args; quote patterns in your shell):\n");
        printf("  Fixed: integer value (e.g., \"10\")\n");
        printf("  Random range: tuple (min,max) (e.g., \"(0,10)\")\n");
        printf("  Repeating per-thread pattern: list [x0,x1,...] (e.g., \"[5,10,(0,20)]\")\n");
        printf("  Explicit per-thread list: {x0,x1,...,xT-1} (e.g., \"{5,(0,10),10,20}\")\n");
        printf("  Output filename (optional): if provided, results will be written to this file\n");

        (void) argc;
        (void) argv;
        (void)out_num_threads;
        (void)out_num_repetitions;
        (void)out_time_interval;
        (void)out_enq_specs;
        (void)out_deq_specs;
        (void)out_filename;
        return exit(EXIT_FAILURE);
    }

    // 2) Parse arguments
    *out_num_threads = atoi(argv[1]);
    *out_num_repetitions = atoi(argv[2]);
    *out_time_interval = atoi(argv[3]);

    if (*out_num_threads <= 0) {
        fprintf(stderr, "num_threads must be > 0\n");
        exit(EXIT_FAILURE);
    }
    if (*out_num_repetitions <= 0) {
        fprintf(stderr, "num_repetitions must be > 0\n");
        exit(EXIT_FAILURE);
    }
    if (*out_time_interval <= 0) {
        fprintf(stderr, "time_interval must be > 0\n");
        exit(EXIT_FAILURE);
    }

    parse_batch_size_pattern(argv[4], *out_num_threads, out_enq_specs);
    parse_batch_size_pattern(argv[5], *out_num_threads, out_deq_specs);

    if (out_filename) {
        if (argc >= 7) {
            *out_filename = argv[6];
        } else {
            *out_filename = (char*)DEFAULT_FILENAME;
        }
    }
}

// Debug: print one batch spec
void print_batch_spec(const batch_spec_t* spec)
{
    if (!spec) {
        printf("(null)");
        return;
    }
    if (spec->kind == BATCH_SPEC_FIXED) {
        printf("%d", spec->min);
    } else if (spec->kind == BATCH_SPEC_RANGE) {
        printf("(%d,%d)", spec->min, spec->max);
    } else {
        printf("(unknown spec kind)");
    }
}

// Debug: print full configuration
void print_configuration(int num_threads,
                         int num_repetitions,
                         int time_interval,
                         const batch_spec_t* enq_specs,
                         const batch_spec_t* deq_specs)
{
    printf("Configuration:\n");
    printf("  num_threads: %d\n", num_threads);
    printf("  num_repetitions: %d\n", num_repetitions);
    printf("  time_interval: %d\n", time_interval);
    printf("  Enqueue batch specs per thread:\n");
    for (int t = 0; t < num_threads; ++t) {
        printf("    Thread %d: ", t);
        print_batch_spec(&enq_specs[t]);
        printf("\n");
    }
    printf("  Dequeue batch specs per thread:\n");
    for (int t = 0; t < num_threads; ++t) {
        printf("    Thread %d: ", t);
        print_batch_spec(&deq_specs[t]);
        printf("\n");
    }
}

