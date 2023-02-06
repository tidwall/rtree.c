# rtree.c

An [R-tree](https://en.wikipedia.org/wiki/R-tree) implementation in C. 

<img src="cities.png" width="512" height="256" border="0" alt="Cities">

## Features

- Generic interface for multiple dimensions and data types
- Supports custom allocators
- Pretty darn good performance 🚀

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
rtree_count    # return number of items
rtree_insert   # insert an item
rtree_delete   # delete an item
rtree_search   # search the rtree
```

## Generic interface

By default this implementation is set to 2 dimensions, using doubles as the
numeric coordinate type, and `void *` as the data type.

The `rtree.c` and `rtree.h` files can be easily customized to change these
settings.

Please find the type parameters at the top of the `rtree.c` file:

```c
#define DATATYPE void * 
#define NUMTYPE double
#define DIMS 2
#define MAX_ENTRIES 64
```

Change these to suit your needs, then modify the `rtree.h` file to match.

## Algorithms

This implementation is a custom [variant](https://github.com/tidwall/rtree#algorithms) of the original paper [R-TREES. A DYNAMIC INDEX STRUCTURE FOR SPATIAL SEARCHING](http://www-db.deis.unibo.it/courses/SI-LS/papers/Gut84.pdf). It was originally designed for [Tile38](https://github.com/tidwall/tile38) and is highly optimized.

## Testing and benchmarks

The `tests.c` file contains tests and benchmarks.

```sh
$ cc -O3 tests.c rtree.c && ./a.out 
```

The following benchmarks were run on Ubuntu 20.04 (3.4GHz 16-Core AMD Ryzen 9 5950X) using gcc-12. 
One million random (evenly distributed) points are inserted, searched, deleted, and replaced.

```
insert         1000000 ops in 0.300 secs, 300 ns/op, 3330769 op/sec
search-item    1000000 ops in 0.267 secs, 267 ns/op, 3743566 op/sec
search-1%      1000 ops in 0.002 secs, 1976 ns/op, 506073 op/sec
search-5%      1000 ops in 0.016 secs, 15764 ns/op, 63436 op/sec
search-10%     1000 ops in 0.051 secs, 51303 ns/op, 19492 op/sec
delete         1000000 ops in 0.359 secs, 359 ns/op, 2788016 op/sec
replace        1000000 ops in 0.548 secs, 548 ns/op, 1823769 op/sec
```

## License

rtree.c source code is available under the MIT License.
