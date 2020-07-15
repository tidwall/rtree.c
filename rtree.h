// Copyright 2020 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#ifndef RTREE_H
#define RTREE_H

#include <stdbool.h>
#include <stddef.h>

struct rtree;

bool rtree_insert(struct rtree *rtree, double *rect, void *item);
struct rtree *rtree_new(size_t elsize, int dims);
void rtree_free(struct rtree *rtree);
size_t rtree_count(struct rtree *rtree);
bool rtree_insert(struct rtree *rtree, double *rect, void *item);
bool rtree_delete(struct rtree *rtree, double *rect, void *item);
bool rtree_search(struct rtree *rtree, double *rect, 
                  bool (*iter)(const double *rect, const void *item, 
                               void *udata), 
                  void *udata);

void rtree_set_allocator(void *(malloc)(size_t), void (*free)(void*));

#endif

