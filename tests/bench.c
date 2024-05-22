#define TEST_PRIVATE_FUNCTIONS
#include "tests.h"

#define bench(name, N, code) {{ \
    if (strlen(name) > 0) { \
        printf("%-14s ", name); \
    } \
    size_t tmem = (size_t)total_mem; \
    size_t tallocs = (size_t)total_allocs; \
    uint64_t bytes = 0; \
    clock_t begin = clock(); \
    { \
        (code); \
    } \
    clock_t end = clock(); \
    double elapsed_secs = (double)(end - begin) / CLOCKS_PER_SEC; \
    double bytes_sec = (double)bytes/elapsed_secs; \
    double ns_op = elapsed_secs/(double)N*1e9; \
    char *pops = commaize(N); \
    char *psec = commaize((double)N/elapsed_secs); \
    printf("%10s ops in %.3f secs %8.1f ns/op %11s op/sec", \
        pops, elapsed_secs, ns_op, psec); \
    free(psec); \
    free(pops); \
    if (bytes > 0) { \
        printf(" %.1f GB/sec", bytes_sec/1024/1024/1024); \
    } \
    if ((size_t)total_mem > tmem) { \
        size_t used_mem = (size_t)total_mem-tmem; \
        printf(" %5.2f bytes/op", (double)used_mem/N); \
    } \
    if ((size_t)total_allocs > tallocs) { \
        size_t used_allocs = (size_t)total_allocs-tallocs; \
        printf(" %5.2f allocs/op", (double)used_allocs/N); \
    } \
    printf("\n"); \
}}

uint32_t interleave(uint32_t x) {
	x = (x | (x << 8)) & 0x00FF00FF;
	x = (x | (x << 4)) & 0x0F0F0F0F;
	x = (x | (x << 2)) & 0x33333333;
	x = (x | (x << 1)) & 0x55555555;
	return x;
}

uint32_t hilbertXYToIndex_logarithmic(uint32_t x, uint32_t y) {
  uint32_t A, B, C, D;

  // Initial prefix scan round, prime with x and y
  {
    uint32_t a = x ^ y;
    uint32_t b = 0xFFFF ^ a;
    uint32_t c = 0xFFFF ^ (x | y);
    uint32_t d = x & (y ^ 0xFFFF);

    A = a | (b >> 1);
    B = (a >> 1) ^ a;

    C = ((c >> 1) ^ (b & (d >> 1))) ^ c;
    D = ((a & (c >> 1)) ^ (d >> 1)) ^ d;
  }

  {
    uint32_t a = A;
    uint32_t b = B;
    uint32_t c = C;
    uint32_t d = D;

    A = ((a & (a >> 2)) ^ (b & (b >> 2)));
    B = ((a & (b >> 2)) ^ (b & ((a ^ b) >> 2)));

    C ^= ((a & (c >> 2)) ^ (b & (d >> 2)));
    D ^= ((b & (c >> 2)) ^ ((a ^ b) & (d >> 2)));
  }

  {
    uint32_t a = A;
    uint32_t b = B;
    uint32_t c = C;
    uint32_t d = D;

    A = ((a & (a >> 4)) ^ (b & (b >> 4)));
    B = ((a & (b >> 4)) ^ (b & ((a ^ b) >> 4)));

    C ^= ((a & (c >> 4)) ^ (b & (d >> 4)));
    D ^= ((b & (c >> 4)) ^ ((a ^ b) & (d >> 4)));
  }

  // Final round and projection
  {
    uint32_t a = A;
    uint32_t b = B;
    uint32_t c = C;
    uint32_t d = D;

    C ^= ((a & (c >> 8)) ^ (b & (d >> 8)));
    D ^= ((b & (c >> 8)) ^ ((a ^ b) & (d >> 8)));
  }

  // Undo transformation prefix scan
  uint32_t a = C ^ (C >> 1);
  uint32_t b = D ^ (D >> 1);

  // Recover index bits
  uint32_t i0 = x ^ y;
  uint32_t i1 = b | (0xFFFF ^ (i0 | a));

  return (interleave(i1) << 1) | interleave(i0);
}


// These are multiplication tables of the alternating group A4,
// preconvolved with the mapping between Morton and Hilbert curves.
static const uint8_t mortonToHilbertTable[] = {
	48, 33, 27, 34, 47, 78, 28, 77,
	66, 29, 51, 52, 65, 30, 72, 63,
	76, 95, 75, 24, 53, 54, 82, 81,
	18,  3, 17, 80, 61,  4, 62, 15,
	 0, 59, 71, 60, 49, 50, 86, 85,
	84, 83,  5, 90, 79, 56,  6, 89,
	32, 23,  1, 94, 11, 12,  2, 93,
	42, 41, 13, 14, 35, 88, 36, 31,
	92, 37, 87, 38, 91, 74,  8, 73,
	46, 45,  9, 10,  7, 20, 64, 19,
	70, 25, 39, 16, 69, 26, 44, 43,
	22, 55, 21, 68, 57, 40, 58, 67,
};

static const uint8_t hilbertToMortonTable[] = {
	48, 33, 35, 26, 30, 79, 77, 44,
	78, 68, 64, 50, 51, 25, 29, 63,
	27, 87, 86, 74, 72, 52, 53, 89,
	83, 18, 16,  1,  5, 60, 62, 15,
	 0, 52, 53, 57, 59, 87, 86, 66,
	61, 95, 91, 81, 80,  2,  6, 76,
	32,  2,  6, 12, 13, 95, 91, 17,
	93, 41, 40, 36, 38, 10, 11, 31,
	14, 79, 77, 92, 88, 33, 35, 82,
	70, 10, 11, 23, 21, 41, 40,  4,
	19, 25, 29, 47, 46, 68, 64, 34,
	45, 60, 62, 71, 67, 18, 16, 49,
};


uint32_t transformCurve(uint32_t in, uint32_t bits, const uint8_t* lookupTable) {
	uint32_t transform = 0;
	uint32_t out = 0;
	for (int32_t i = 3 * (bits - 1); i >= 0; i -= 3) {
		transform = lookupTable[transform | ((in >> i) & 7)];
		out = (out << 3) | (transform & 7);
		transform &= ~7;
	}
	return out;
}

uint32_t mortonToHilbert3D(uint32_t mortonIndex, uint32_t bits) {
	return transformCurve(mortonIndex, bits, mortonToHilbertTable);
}

uint32_t hilbertToMorton3D(uint32_t hilbertIndex, uint32_t bits) {
	return transformCurve(hilbertIndex, bits, hilbertToMortonTable);
}

uint32_t hilbert(double lat, double lon) {
    uint32_t x = ((lon + 180.0) / 360.0) * 0xFFFF;
    uint32_t y = ((lat + 90.0) / 180.0) * 0xFFFF;
    return hilbertXYToIndex_logarithmic(x, y);
}




static bool search_iter(const RTREE_NUMTYPE *min, const RTREE_NUMTYPE *max, const void *item, void *udata) {
    (*(int*)udata)++;
    return true;
}

struct search_iter_one_context {
    RTREE_NUMTYPE *point;
    void *data;
    int count;
};

static bool search_iter_one(const RTREE_NUMTYPE *min, const RTREE_NUMTYPE *max, const void *data, void *udata) {
    struct search_iter_one_context *ctx = (struct search_iter_one_context *)udata;
    if (data == ctx->data) {
        assert(memcmp(min, ctx->point, sizeof(RTREE_NUMTYPE)*RTREE_DIMS) == 0);
        assert(memcmp(max, ctx->point, sizeof(RTREE_NUMTYPE)*RTREE_DIMS) == 0);
        ctx->count++;
        return false;
    }
    return true;
}

int point_compare(const void *a, const void *b) {
    DISABLE_WARNING_PUSH
    DISABLE_WARNING(-Wvoid-pointer-to-int-cast)
    const RTREE_NUMTYPE *p1 = (const RTREE_NUMTYPE*)a;
    const RTREE_NUMTYPE *p2 = (const RTREE_NUMTYPE*)b;
    DISABLE_WARNING_POP
    
    if (is_float_point(RTREE_NUMTYPE)) {
        uint32_t h1 = hilbert(p1[1],p1[0]);
        uint32_t h2 = hilbert(p2[1],p2[0]);

        if (h1 < h2) {
            return -1;
        }
        if (h1 > h2) {
            return 1;
        }
        return 0;
    }

    if (p1[0] < p2[0]) {
        return -1;
    }
    if (p1[0] > p2[0]) {
        return 1;
    }
    return 0;
}


void shuffle_points(double *points, int N) {
    shuffle(points, N, sizeof(double)*2);
}

RTREE_NUMTYPE *make_random_points(int N) {
    RTREE_NUMTYPE *points = (RTREE_NUMTYPE *)xmalloc(N*RTREE_DIMS*sizeof(RTREE_NUMTYPE));
    if(is_float_point(RTREE_NUMTYPE)) {
        for (int i = 0; i < N; i++) {
            #if RTREE_DIMS == 2
                points[i*2+0] = rand_double() * 360.0 - 180.0;;
                points[i*2+1] = rand_double() * 180.0 - 90.0;;
            #else
                for (int d = 0; d < RTREE_DIMS; d++){
                    points[i*RTREE_DIMS+d] = rand_double();
                }
            #endif
        }
    }else{
        for (int i = 0; i < N; i++) {
            for (int d = 0; d < RTREE_DIMS; d++){
                points[i*RTREE_DIMS+d] = rand();
            }
        }
    }
    return points;
}

void sort_points(RTREE_NUMTYPE *points, int N) {
    qsort(points, N, sizeof(RTREE_NUMTYPE)*RTREE_DIMS, point_compare);
} 

void insert(struct rtree *tr,RTREE_NUMTYPE *points, int N){
    for (int i = 0; i < N; i++){
        RTREE_NUMTYPE *point = &points[i*RTREE_DIMS];
        rtree_insert(tr, point, point, (void *)(uintptr_t)(i));
        assert(rtree_count(tr) == i+1);
    }
}

void search_item(struct rtree *tr,RTREE_NUMTYPE *points, int N){
    for (int i = 0; i < N; i++){
        RTREE_NUMTYPE *point = &points[i*RTREE_DIMS];
        struct search_iter_one_context ctx = { 0 };
        ctx.point = point;
        ctx.data = (void *)(uintptr_t)(i);
        rtree_search(tr, point, point, search_iter_one, &ctx);
        assert(ctx.count == 1);
    }
}

void search(struct rtree *tr,RTREE_NUMTYPE *points, int N, double p){
    for (int i = 0; i < N; i++){
        RTREE_NUMTYPE min[RTREE_DIMS];
        RTREE_NUMTYPE max[RTREE_DIMS];
        if(is_float_point(RTREE_NUMTYPE)){
            #if RTREE_DIMS == 2
            min[0] = rand_double() * 360.0 - 180.0;
            min[1] = rand_double() * 180.0 - 90.0;
            max[0] = min[0] + 360.0*p;
            max[1] = min[1] + 180.0*p;
            #else
            for (int d = 0; d < RTREE_DIMS; d++){
                min[d] = rand_double();
                max[d] = min[d] + 360.0*p;
            }
            #endif
        }else{
            for (int d = 0; d < RTREE_DIMS; d++){
                min[d] = rand();
                max[d] = min[d] + 360*p;
            }
        }
        int res = 0;
        rtree_search(tr, min, max, search_iter, &res);
        // printf("%d\n", res);
    }
}

void delete(struct rtree *tr,RTREE_NUMTYPE *points, int N){
    for (int i = 0; i < N; i++){
        RTREE_NUMTYPE *point = &points[i*RTREE_DIMS];
        rtree_delete(tr, point, point, (void*)(uintptr_t)(i));
        assert(rtree_count(tr) == N-i-1);
    }
}

void replace(struct rtree *tr,RTREE_NUMTYPE *points, RTREE_NUMTYPE *points2, int N){
    for (int i = 0; i < N; i++){
        assert(rtree_count(tr) == N);
        RTREE_NUMTYPE *point = &points[i*RTREE_DIMS];
        rtree_delete(tr, point, point, (void*)(uintptr_t)(i));
        assert(rtree_count(tr) == N-1);
        RTREE_NUMTYPE *point2 = &points2[i*RTREE_DIMS];
        rtree_insert(tr, point2, point2, (void*)(uintptr_t)(i));
        assert(rtree_count(tr) == N);
    }
}

void test_rand_bench(bool hilbert_ordered, int N) {
    if (hilbert_ordered) {
        printf("-- HILBERT ORDER --\n");
    } else {
        printf("-- RANDOM ORDER --\n");
    }
    RTREE_NUMTYPE *points = make_random_points(N);
    if (hilbert_ordered) {
        sort_points(points, N);
    }


    struct rtree *tr = rtree_new_with_allocator(xmalloc, xfree);
    bench("insert", N, insert(tr,points,N));

    rtree_check(tr);

    // sort_points(points, N);
    bench("search-item", N, search_item(tr,points,N));

    bench("search-1%", N, search(tr,points,N,0.01));
    bench("search-5%", N, search(tr,points,N,0.05));
    bench("search-10%", N, search(tr,points,N,0.10));
    bench("delete", N, delete(tr,points,N));

    RTREE_NUMTYPE *points2 = (RTREE_NUMTYPE *)xmalloc(N*RTREE_DIMS*sizeof(RTREE_NUMTYPE));
    for (int i = 0; i < N; i++) {
        RTREE_NUMTYPE *point = &points[i*RTREE_DIMS];
        rtree_insert(tr, point, point, (void*)(uintptr_t)(i));
        assert(rtree_count(tr) == i+1);
        if(is_float_point(RTREE_NUMTYPE)){
            double rsize = 0.01; // size of rectangle in degrees
            for (int d = 0; d < RTREE_DIMS; d++){
                points2[i*RTREE_DIMS+d] = points2[i*RTREE_DIMS+d] + rand_double()*rsize - rsize/2;
            }
        }else{
            int rsize = 2;
            for (int d = 0; d < RTREE_DIMS; d++){
                points2[i*RTREE_DIMS+d] = points2[i*RTREE_DIMS+d] + rand()*rsize - rsize/2;
            }
        }

    }

    bench("replace", N, replace(tr,points,points2,N));

    rtree_check(tr);

    RTREE_NUMTYPE *tmp = points;
    points = points2;
    points2 = tmp;

    bench("search-item", N, search_item(tr,points,N));
    bench("search-1%", N, search(tr,points,N,0.01));
    bench("search-5%", N, search(tr,points,N,0.05));
    bench("search-10%", N, search(tr,points,N,0.10));

    rtree_free(tr);
    xfree(points);
    xfree(points2);
}




int main() {
    int seed = getenv("SEED")?atoi(getenv("SEED")):time(NULL);
    int N = getenv("N")?atoi(getenv("N")):10000;
    printf("seed=%d, count=%d\n", seed, N);
    srand(seed);

    init_test_allocator(false);
    test_rand_bench(false, N);
    test_rand_bench(true, N);
    cleanup_test_allocator();
    return 0;
}