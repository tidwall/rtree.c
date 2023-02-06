// Copyright 2023 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#ifndef RTREE_H
#define RTREE_H

#include <stdlib.h>
#include <stdbool.h>

struct rtree *rtree_new();
struct rtree *rtree_new_with_allocator(void *(*malloc)(size_t), void (*free)(void*));
bool rtree_insert(struct rtree *tr, const double *min, const double *max, const void *data);
void rtree_free(struct rtree *tr);
void rtree_search(struct rtree *tr, const double *min, const double *max,
    bool (*iter)(const double *min, const double *max, const void *data, void *udata), 
    void *udata);
size_t rtree_count(struct rtree *tr);
void rtree_delete(struct rtree *tr, const double *min, const double *max, const void *data);

#endif // RTREE_H
