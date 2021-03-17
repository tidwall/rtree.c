# rtree.c

An [R-tree](https://en.wikipedia.org/wiki/R-tree) implementation in C. 

<img src="cities.png" width="512" height="256" border="0" alt="Cities">

## Features

- Supports any number of dimensions
- Generic interface with support for variable sized items
- ANSI C (C99)
- Supports custom allocators
- Robust, self-contained tests
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
struct city pra = { .name = "Prague", .lat = 50.088, .lon = -14.420 };
struct city tai = { .name = "Taipei", .lat = 25.033, .lon = 121.565 };
struct city her = { .name = "Hermosillo", .lat = 29.102, .lon = -110.977 };
struct city him = { .name = "Himeji", .lat = 34.816, .lon = 134.700 };

bool city_iter(const double *rect, const void *item, void *udata) {
    const struct city *city = item;
    printf("%s\n", city->name);
    return true;
}

int main() {
    // create a new rtree where each item is a `struct city*`. 
    struct rtree *tr = rtree_new(sizeof(struct city*), 2);

    // load some cities into the rtree. Each set operation performs a copy of 
    // the data that is pointed to in the second and third arguments. 
    // The R-tree expects a rectangle, which is an array of doubles, that
    // has the first N values as the minimum corner of the rect, and the next
    // N values as the maximum corner of the rect, where N is the number of
    // dimensions provided to rtree_new(). For points the the min and max
    // values should match.
    rtree_insert(tr, (double[]){ phx.lon, phx.lat, phx.lon, phx.lat }, &phx);
    rtree_insert(tr, (double[]){ enn.lon, enn.lat, enn.lon, enn.lat }, &enn);
    rtree_insert(tr, (double[]){ pra.lon, pra.lat, pra.lon, pra.lat }, &pra);
    rtree_insert(tr, (double[]){ tai.lon, tai.lat, tai.lon, tai.lat }, &tai);
    rtree_insert(tr, (double[]){ her.lon, her.lat, her.lon, her.lat }, &her);
    rtree_insert(tr, (double[]){ him.lon, him.lat, him.lon, him.lat }, &him);
    
    printf("\n-- Northwestern cities --\n");
    rtree_search(tr, (double[]){ -180, 0, 0, 90 }, city_iter, NULL);

    printf("\n-- Northeastern cities --\n");
    rtree_search(tr, (double[]){ 0, 0, 180, 90 }, city_iter, NULL);

    // deleting an item is the same inserting
    rtree_delete(tr, (double[]){ phx.lon, phx.lat, phx.lon, phx.lat }, &phx);

    printf("\n-- Northwestern cities --\n");
    rtree_search(tr, (double[]){ -180, 0, 0, 90 }, city_iter, NULL);

    rtree_free(tr);

}
// output:
// -- northwest cities --
// Phoenix
// Ennis
// Prague
// Hermosillo
// 
// -- northeast cities --
// Taipei
// Himeji
// 
// -- northwest cities --
// Ennis
// Prague
// Hermosillo
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

## Algorithms

This implementation is a [variant](https://github.com/tidwall/rtree#algorithm) of the original paper:  
[R-TREES. A DYNAMIC INDEX STRUCTURE FOR SPATIAL SEARCHING](http://www-db.deis.unibo.it/courses/SI-LS/papers/Gut84.pdf)


## Testing and benchmarks

The `rtree.c` file also contains robust testing and benchmark code.

```sh
$ cc -DRTREE_TEST rtree.c && ./a.out              # run tests
$ cc -DRTREE_TEST -O3 rtree.c && BENCH=1 ./a.out  # run benchmarks
```

The following benchmarks were run on my 2019 Macbook Pro (2.4 GHz 8-Core Intel Core i9) using gcc-9. One million random (evenly distributed) rectangles are inserted, searched, and deleted.

```
insert   1000000 ops in 0.406 secs, 406 ns/op, 2462496 op/sec
search   1000000 ops in 0.936 secs, 936 ns/op, 1068225 op/sec
delete   1000000 ops in 0.901 secs, 901 ns/op, 1109395 op/sec
```

## License

rtree.c source code is available under the MIT License.
