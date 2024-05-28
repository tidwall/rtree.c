# rtree.c

An [R-tree](https://en.wikipedia.org/wiki/R-tree) implementation in C. 

<img src="cities.png" border="0" alt="Cities">

## Features

- [Generic interface](#generic-interface) for multiple dimensions and data types
- Supports custom allocators
- Copy-on-write support
- Includes [test suite](#testing-and-benchmarks) with 100% coverage.
- [Very fast](#testing-and-benchmarks) 🚀

## Example

```c
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rtree.h"

struct city {
    char *name;
    double lat;
    double lon;
};

struct city phx = { .name = "Phoenix", .lat = 33.448, .lon = -112.073 };
struct city enn = { .name = "Ennis", .lat = 52.843, .lon = -8.986 };
struct city pra = { .name = "Prague", .lat = 50.088, .lon = 14.420 };
struct city tai = { .name = "Taipei", .lat = 25.033, .lon = 121.565 };
struct city her = { .name = "Hermosillo", .lat = 29.102, .lon = -110.977 };
struct city him = { .name = "Himeji", .lat = 34.816, .lon = 134.700 };

bool city_iter(const double *min, const double *max, const void *item, void *udata) {
    const struct city *city = item;
    printf("%s\n", city->name);
    return true;
}

int main() {
    // Create a new rtree where each item is a `struct city*`. 
    struct rtree *tr = rtree_new();

    // Load some cities into the rtree. Each insert operation performs a copy
    // of the data that is pointed to in the second and third arguments. 
    // The R-tree expects a rectangle, which is two arrays of doubles. 
    // The first N values as the minimum corner of the rect, and the next
    // N values as the maximum corner of the rect, where N is the number of
    // dimensions. The default R-tree has 2 dimensions.
    // When inserting points, the max coordinates are optional.
    rtree_insert(tr, (double[2]){phx.lon, phx.lat}, NULL, &phx);
    rtree_insert(tr, (double[2]){enn.lon, enn.lat}, NULL, &enn);
    rtree_insert(tr, (double[2]){pra.lon, pra.lat}, NULL, &pra);
    rtree_insert(tr, (double[2]){tai.lon, tai.lat}, NULL, &tai);
    rtree_insert(tr, (double[2]){her.lon, her.lat}, NULL, &her);
    rtree_insert(tr, (double[2]){him.lon, him.lat}, NULL, &him);
    
    printf("\n-- Northwestern cities --\n");
    rtree_search(tr, (double[2]){-180, 0}, (double[2]){0, 90}, city_iter, NULL);

    printf("\n-- Northeastern cities --\n");
    rtree_search(tr, (double[2]){0, 0}, (double[2]){180, 90}, city_iter, NULL);

    // Deleting an item is the same inserting
    rtree_delete(tr, (double[2]){phx.lon, phx.lat}, NULL, &phx);

    printf("\n-- Northwestern cities --\n");
    rtree_search(tr, (double[2]){-180, 0}, (double[2]){0, 90}, city_iter, NULL);

    rtree_free(tr);
}
// output:
// -- Northwestern cities --
// Phoenix
// Hermosillo
// Ennis
// 
// -- Northeastern cities --
// Prague
// Taipei
// Himeji
// 
// -- Northwestern cities --
// Hermosillo
// Ennis
```

## Functions

```sh
rtree_new      # allocate a new rtree
rtree_free     # free the rtree
rtree_count    # return number of items in rtree
rtree_insert   # insert an item
rtree_delete   # delete an item
rtree_search   # search the rtree for items with interecting rectangles
rtree_clone    # make an clone of the rtree using a copy-on-write technique
```

## Generic interface

This implementation is set to 2 dimensions, using doubles as the
numeric coordinate type, and `void *` as the data type.

The `rtree.c` and `rtree.h` files can be easily customized to change these
settings.

Please set the parameters globally or before `rtree.h` including. Otherwise default values will be set:

```c
#define DATATYPE void * 
#define NUMTYPE double
#define DIMS 2
#define MAXITEMS 64
```

Change these to suit your needs, then modify the `rtree.h` file to match.

## Testing and benchmarks

```sh
$ tests/run.sh         # run tests
$ tests/run.sh bench   # run benchmarks
```

The following benchmarks were run on Ubuntu 20.04 (3.4GHz 16-Core AMD Ryzen 9 5950X) using clang-17. 
One million random (evenly distributed) points are inserted, searched, deleted, and replaced.

```
clang-17 -O3 -DTEST_PRIVATE_FUNCTIONS -DTEST_DEBUG ../rtree.c bench.c -lm
seed=1694563565, count=1000000
-- RANDOM ORDER --
insert          1,000,000 ops in 0.151 secs    151.3 ns/op   6,610,346 op/sec
search-item     1,000,000 ops in 0.242 secs    242.4 ns/op   4,125,140 op/sec
search-1%           1,000 ops in 0.002 secs   1958.0 ns/op     510,725 op/sec
search-5%           1,000 ops in 0.017 secs  16585.0 ns/op      60,295 op/sec
search-10%          1,000 ops in 0.051 secs  50791.0 ns/op      19,688 op/sec
delete          1,000,000 ops in 0.233 secs    232.8 ns/op   4,296,197 op/sec
replace         1,000,000 ops in 0.302 secs    302.3 ns/op   3,308,355 op/sec
search-item     1,000,000 ops in 0.241 secs    241.4 ns/op   4,142,862 op/sec
search-1%           1,000 ops in 0.002 secs   1968.0 ns/op     508,130 op/sec
search-5%           1,000 ops in 0.017 secs  16680.0 ns/op      59,952 op/sec
search-10%          1,000 ops in 0.052 secs  52415.0 ns/op      19,078 op/sec
```

The following benchmarks are the same as above but the points are ordered on a
[hilbert curve](https://en.wikipedia.org/wiki/Hilbert_curve).

```
-- HILBERT ORDER --
insert          1,000,000 ops in 0.073 secs     73.1 ns/op  13,686,068 op/sec
search-item     1,000,000 ops in 0.083 secs     83.1 ns/op  12,039,199 op/sec
search-1%           1,000 ops in 0.002 secs   2015.0 ns/op     496,277 op/sec
search-5%           1,000 ops in 0.016 secs  16031.0 ns/op      62,379 op/sec
search-10%          1,000 ops in 0.046 secs  46241.0 ns/op      21,625 op/sec
delete          1,000,000 ops in 0.063 secs     62.7 ns/op  15,941,844 op/sec
replace         1,000,000 ops in 0.083 secs     83.2 ns/op  12,013,599 op/sec
search-item     1,000,000 ops in 0.084 secs     84.5 ns/op  11,840,344 op/sec
search-1%           1,000 ops in 0.002 secs   2110.0 ns/op     473,933 op/sec
search-5%           1,000 ops in 0.016 secs  16055.0 ns/op      62,285 op/sec
search-10%          1,000 ops in 0.046 secs  46134.0 ns/op      21,675 op/sec
```

## Algorithms

This implementation is a variant of the original paper:  
[R-TREES. A DYNAMIC INDEX STRUCTURE FOR SPATIAL SEARCHING](https://www.cs.princeton.edu/courses/archive/fall08/cos597B/papers/rtrees.pdf)

### Inserting

Similar to the original paper. From the root to the leaf, the rects which will incur the least enlargment are chosen. Ties go to rects with the smallest area. 

Added to this implementation: when a rect does not incur any enlargement at all, it's chosen immediately and without further checks on other rects in the same node. 
### Deleting

A target rect is searched for from root to the leaf, and if found it's deleted. When there are no more child rects in a node, that node is immedately removed from the tree.

### Searching

Same as the original algorithm.

### Splitting

This is a custom algorithm. It attempts to minimize intensive operations such as pre-sorting the children and comparing overlaps & area sizes. The desire is to do simple single axis distance calculations each child only once, with a target 50/50 chance that the child might be moved in-memory.

When a rect has reached it's max number of entries it's largest axis is calculated and the rect is split into two smaller rects, named `left` and `right`.
Each child rects is then evaluated to determine which smaller rect it should be placed into.
Two values, `min-dist` and `max-dist`, are calcuated for each child. 

- `min-dist` is the distance from the parent's minumum value of it's largest axis to the child's minumum value of the parent largest axis.
- `max-dist` is the distance from the parent's maximum value of it's largest axis to the child's maximum value of the parent largest axis.

When the `min-dist` is less than `max-dist` then the child is placed into the `left` rect. 
When the `max-dist` is less than `min-dist` then the child is placed into the `right` rect. 
When the `min-dist` is equal to `max-dist` then the child is placed into an `equal` bucket until all of the children are evaluated.
Each `equal` rect is then one-by-one placed in either `left` or `right`, whichever has fewer children.

## License

rtree.c source code is available under the MIT License.
