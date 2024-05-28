#ifndef TESTS_H
#define TESTS_H

#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <ctype.h>
#include "../rtree.c"

#define is_float_point(x) _Generic((x)0,       \
        _Bool: 0, unsigned char:            0, \
         char: 0, signed char:              0, \
    short int: 0, unsigned short int:       0, \
          int: 0, unsigned int:             0, \
     long int: 0, unsigned long int:        0, \
long long int: 0, unsigned long long int:   0, \
        float: 1, double:                   1, \
  long double: 1, char *:                   0, \
       void *: 0, int *:                    0, \
      default: 0) 


#if defined(_MSC_VER)
    #define DISABLE_WARNING_PUSH           __pragma(warning( push ))
    #define DISABLE_WARNING_POP            __pragma(warning( pop )) 
    #define DISABLE_WARNING(warningNumber) __pragma(warning( disable : warningNumber ))

#elif defined(__GNUC__) || defined(__clang__)
    #define DO_PRAGMA(X) _Pragma(#X)
    #define DISABLE_WARNING_PUSH           DO_PRAGMA(GCC diagnostic push)
    #define DISABLE_WARNING_POP            DO_PRAGMA(GCC diagnostic pop) 
    #define DISABLE_WARNING(warningName)   DO_PRAGMA(GCC diagnostic ignored #warningName)    
#else
    #define DISABLE_WARNING_PUSH
    #define DISABLE_WARNING_POP
    #define DISABLE_WARNING(x)
#endif

DISABLE_WARNING(-Wcompound-token-split-by-macro)
DISABLE_WARNING(-Wpedantic)

#include <stdio.h>

//////////////////
// checker
//////////////////

static bool node_check_rect(const struct rect *rect, struct node *node) {
    struct rect rect2 = node_rect_calc(node);
    if (!rect_equals(rect, &rect2)){
        fprintf(stderr, "invalid rect\n");
        return false;
    }
    if (node->kind == BRANCH) {
        for (int i = 0; i < node->count; i++) {
            if (!node_check_rect(&node->rects[i], node->nodes[i])) {
                return false;
            }
        }
    }
    return true;
}

static bool rtree_check_rects(const struct rtree *tr) {
    if (tr->root) {
        if (!node_check_rect(&tr->rect, tr->root)) return false;
    }
    return true;
}

static bool rtree_check_height(const struct rtree *tr) {
    size_t height = 0;
    struct node *node = tr->root;
    while (node) {
        height++;
        if (node->kind == LEAF) break;
        node = node->nodes[0];
    }
    if (height != tr->height) {
        fprintf(stderr, "invalid height\n");
        return false;
    }
    return true;
}

bool rtree_check(const struct rtree *tr) {
    if (!rtree_check_rects(tr)) return false;
    if (!rtree_check_height(tr)) return false;
    return true;
}

static const double svg_scale = 20.0;
static const char *strokes[] = { "black", "red", "green", "purple" };
static const int nstrokes = 4;

static void node_write_svg(const struct node *node, const struct rect *rect, 
    FILE *f, int depth)
{
    bool point = rect->min[0] == rect->max[0] && rect->min[1] == rect->max[1];
    if (node) {
        if (node->kind == BRANCH) {
            for (int i = 0; i < node->count; i++) {
                node_write_svg(node->nodes[i], &node->rects[i], f, depth+1);
            }
        } else {
            for (int i = 0; i < node->count; i++) {
                node_write_svg(NULL, &node->rects[i], f, depth+1);
            }
        }
    }
    if (point) {
        double w = (rect->max[0]-rect->min[0]+1/svg_scale)*svg_scale*10;
        fprintf(f, 
            "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\" "
                "fill=\"%s\" fill-opacity=\"1\" "
                "rx=\"3\" ry=\"3\"/>\n",
            (rect->min[0])*svg_scale-w/2, 
            (rect->min[1])*svg_scale-w/2,
            w, w, 
            strokes[depth%nstrokes]);
    } else {
        fprintf(f, 
            "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\" "
                "stroke=\"%s\" fill=\"%s\" "
                "stroke-width=\"%d\" "
                "fill-opacity=\"0\" stroke-opacity=\"1\"/>\n",
            (rect->min[0])*svg_scale,
            (rect->min[1])*svg_scale,
            (rect->max[0]-rect->min[0]+1/svg_scale)*svg_scale,
            (rect->max[1]-rect->min[1]+1/svg_scale)*svg_scale,
            strokes[depth%nstrokes],
            strokes[depth%nstrokes],
            1);
    }
}

// rtree_write_svg draws the R-tree to an SVG file. This is only useful with
// small geospatial 2D dataset. 
void rtree_write_svg(const struct rtree *tr, const char *path) {
    FILE *f = fopen(path, "wb+");
    fprintf(f, "<svg viewBox=\"%.0f %.0f %.0f %.0f\" " 
        "xmlns =\"http://www.w3.org/2000/svg\">\n",
        -190.0*svg_scale, -100.0*svg_scale,
        380.0*svg_scale, 190.0*svg_scale);
    fprintf(f, "<g transform=\"scale(1,-1)\">\n");
    if (tr->root) {
        node_write_svg(tr->root, &tr->rect, f, 0);
    }
    fprintf(f, "</g>\n");
    fprintf(f, "</svg>\n");
    fclose(f);
}

int64_t crand(void) {
    uint64_t seed = 0;
    FILE *urandom = fopen("/dev/urandom", "r");
    assert(urandom);
    assert(fread(&seed, sizeof(uint64_t), 1, urandom));
    fclose(urandom);
    return (int64_t)(seed>>1);
}

void seedrand(void) {
    srand(crand());
}

static int64_t seed = 0;

#define do_test0(name, trand) { \
    if (argc < 2 || strstr(#name, argv[1])) { \
        if ((trand)) { \
            seed = getenv("SEED")?atoi(getenv("SEED")):crand(); \
            printf("SEED=%" PRId64 "\n", seed); \
            srand(seed); \
        } else { \
            seedrand(); \
        } \
        printf("%s\n", #name); \
        init_test_allocator(false); \
        name(); \
        cleanup(); \
        cleanup_test_allocator(); \
    } \
}

#define do_test(name) do_test0(name, 0)
#define do_test_rand(name) do_test0(name, 1)

// chaos test simple ensures that 1 out of 3 mallocs fail. 
#define do_chaos_test(name) { \
    if (argc < 2 || strstr(#name, argv[1])) { \
        printf("%s\n", #name); \
        seedrand(); \
        init_test_allocator(true); \
        name(); \
        cleanup(); \
        cleanup_test_allocator(); \
    } \
}

void shuffle(void *array, size_t numels, size_t elsize) {
    if (numels < 2) return;
    char tmp[elsize];
    char *arr = array;
    for (size_t i = 0; i < numels - 1; i++) {
        int j = i + rand() / (RAND_MAX / (numels - i) + 1);
        memcpy(tmp, arr + j * elsize, elsize);
        memcpy(arr + j * elsize, arr + i * elsize, elsize);
        memcpy(arr + i * elsize, tmp, elsize);
    }
}

void cleanup(void) {
}

atomic_int total_allocs = 0;
atomic_int total_mem = 0;

double now(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec*1e9 + now.tv_nsec) / 1e9;
}

static bool rand_alloc_fail = false;
// 1 in 10 chance malloc or realloc will fail.
static int rand_alloc_fail_odds = 3; 

static void *xmalloc_mul(size_t size, int mul) {
    if (rand_alloc_fail && rand()%(rand_alloc_fail_odds*mul) == 0) {
        return NULL;
    }
    void *mem = malloc(sizeof(uint64_t)+size);
    assert(mem);
    *(uint64_t*)mem = size;
    atomic_fetch_add(&total_allocs, 1);
    atomic_fetch_add(&total_mem, (int)size);
    return (char*)mem+sizeof(uint64_t);
}

void *xmalloc(size_t size) {
    return xmalloc_mul(size, 1);
}
void *xmalloc1(size_t size) {
    return xmalloc_mul(size, 10);
}
void *xmalloc2(size_t size) {
    return xmalloc_mul(size, 100);
}
void *xmalloc3(size_t size) {
    return xmalloc_mul(size, 1000);
}

static void xfree(void *ptr) {
    if (ptr) {
        atomic_fetch_sub(&total_mem,
            (int)(*(uint64_t*)((char*)ptr-sizeof(uint64_t))));
        atomic_fetch_sub(&total_allocs, 1);
        free((char*)ptr-sizeof(uint64_t));
    }
}

static void *(*__malloc)(size_t) = NULL;
static void (*__free)(void *) = NULL;

void init_test_allocator(bool random_failures) {
    rand_alloc_fail = random_failures;
    __malloc = xmalloc;
    __free = xfree;
}

void cleanup_test_allocator(void) {
    if (atomic_load(&total_allocs) > 0 || atomic_load(&total_mem) > 0) {
        fprintf(stderr, "test failed: %d unfreed allocations, %d bytes\n",
            atomic_load(&total_allocs), atomic_load(&total_mem));
        exit(1);
    }
    __malloc = NULL;
    __free = NULL;
}

double rand_double() {
    return (double)rand() / ((double)RAND_MAX+1);
}

void fill_rand_rect(RTREE_NUMTYPE *coords) {
    if (is_float_point(RTREE_NUMTYPE)){
        #if RTREE_DIMS == 2
        coords[0] = rand_double()*360-180;
        coords[1] = rand_double()*180-90;
        coords[2] = coords[0]+(rand_double()*2);
        coords[3] = coords[1]+(rand_double()*2);
        #else
        for (int d = 0; d < RTREE_DIMS; d++){
            coords[d] = rand_double();
            coords[d+RTREE_DIMS] = coords[d]+(rand_double());
        }
        #endif
    }else{
        for (int d = 0; d < RTREE_DIMS; d++){
            coords[d] = rand()>>1;
            coords[d+RTREE_DIMS] = coords[d]+(rand()>>1);
        }
    }
    
}

struct rect rand_rect() {
    struct rect rect;
    fill_rand_rect(&rect.min[0]);
    return rect;
}

// // struct btree *btree_new_for_test(size_t elsize, size_t max_items,
// //     int (*compare)(const void *a, const void *b, void *udata),
// //     void *udata)
// // {
// //     return btree_new_with_allocator(__malloc, NULL, __free, elsize, max_items, 
// //         compare, udata);
// // }

// struct btree *btree_new_for_test(size_t elsize, size_t degree,
//     int (*compare)(const void *a, const void *b, void *udata),
//     void *udata)
// {
//     return btree_new_with_allocator(__malloc, NULL, __free, elsize, 
//         degree, compare, udata);
// }


struct find_one_context {
    void *target;
    void *found_data;
    bool found;
    int(*compare)(const void *a, const void *b);
};

bool find_one_iter(const RTREE_NUMTYPE *min, const RTREE_NUMTYPE *max, const void *data,
    void *udata)
{
    (void)min; (void)max;
    struct find_one_context *ctx = udata;
    if ((ctx->compare && ctx->compare(data, ctx->target) == 0) ||
        data == ctx->target)
    {
        assert(!ctx->found);
        ctx->found_data = (void*)data;
        ctx->found = true;
    }
    return true;
}

bool find_one(struct rtree *tr, const RTREE_NUMTYPE min[], const RTREE_NUMTYPE max[], 
    void *data, int(*compare)(const void *a, const void *b), void **found_data)
{
    struct find_one_context ctx = { .target = data, .compare = compare };
    rtree_search(tr, min, max, find_one_iter, &ctx);
    if (found_data) {
        *found_data = ctx.found ? ctx.found_data : NULL;
    }
    return ctx.found;
}

char *commaize(unsigned int n) {
    char s1[64];
    char *s2 = malloc(64);
    assert(s2);
    memset(s2, 0, sizeof(64));
    snprintf(s1, sizeof(s1), "%d", n);
    int i = strlen(s1)-1; 
    int j = 0;
	while (i >= 0) {
		if (j%3 == 0 && j != 0) {
            memmove(s2+1, s2, strlen(s2)+1);
            s2[0] = ',';
		}
        memmove(s2+1, s2, strlen(s2)+1);
		s2[0] = s1[i];
        i--;
        j++;
	}
	return s2;
}

char *rand_key(int nchars) {
    char *key = xmalloc(nchars+1);
    for (int i = 0 ; i < nchars; i++) {
        key[i] = (rand()%26)+'a';
    }
    key[nchars] = '\0';
    return key;
}

static void *oom_ptr = (void*)(uintptr_t)(intptr_t)-1;

void *rtree_set(struct rtree *tr, const RTREE_NUMTYPE *min, const RTREE_NUMTYPE *max, 
    void *data)
{
    void *prev = NULL;
    size_t n = rtree_count(tr);
    if (find_one(tr, min, max, data, NULL, NULL)) {
        prev = data;
        size_t n = rtree_count(tr);
        if (!rtree_delete(tr, min, max, data)) return oom_ptr;
        assert(rtree_count(tr) == n-1);
        n--;
    }
    if (!rtree_insert(tr, min, max, data)) return oom_ptr;
    assert(rtree_count(tr) == n+1);
    
    return prev;
}

// rsleep randomly sleeps between min_secs and max_secs
void rsleep(double min_secs, double max_secs) {
    double duration = max_secs - min_secs;
    double start = now();
    while (now()-start < duration) {
        usleep(10000); // sleep for ten milliseconds
    }
}

#endif // TESTS_H


