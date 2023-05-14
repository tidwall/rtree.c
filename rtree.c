// Copyright 2023 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "rtree.h"

////////////////////////////////

#define DATATYPE void *
#define NUMTYPE double
#define DIMS 2
#define MAX_ENTRIES 64

////////////////////////////////

// node chooser options
#define IGNORE_AREA_EQUALITY_CHECK
#define FAST_CHOOSER 2  // 0 = off , 1 == fast, 2 == faster

// used for splits
#define MIN_ENTRIES_PERCENTAGE 10
#define MIN_ENTRIES ((MAX_ENTRIES) * (MIN_ENTRIES_PERCENTAGE) / 100 + 1)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

enum kind {
    LEAF = 1,
    BRANCH = 2,
};

struct rect {
    NUMTYPE min[DIMS];
    NUMTYPE max[DIMS];
};

struct item {
    DATATYPE data;
};

struct node {
    atomic_int rc;      // reference counter for copy-on-write
    enum kind kind;     // LEAF or BRANCH
    int count;          // number of rects
    struct rect rects[MAX_ENTRIES];
    union {
        struct node *children[MAX_ENTRIES];
        struct item items[MAX_ENTRIES];
    };
};

struct rtree {
    struct rect rect;
    struct node *root;
    size_t count;
    size_t height;
    void *(*malloc)(size_t);
    void (*free)(void *);
    void *udata;
    bool (*item_clone)(const DATATYPE item, DATATYPE *into, void *udata);
    void (*item_free)(const DATATYPE item, void *udata);
};

void rtree_set_udata(struct rtree *tr, void *udata) {
    tr->udata = udata;
}

static struct node *node_new(struct rtree *tr, enum kind kind) {
    struct node *node = (struct node *)tr->malloc(sizeof(struct node));
    if (!node) return NULL;
    memset(node, 0, sizeof(struct node));
    node->kind = kind;
    return node;
}

static struct node *node_copy(struct rtree *tr, struct node *node) {
    struct node *node2 = (struct node *)tr->malloc(sizeof(struct node));
    if (!node2) return NULL;
    memcpy(node2, node, sizeof(struct node));
    node2->rc = 0;
    if (node2->kind == BRANCH) {
        for (int i = 0; i < node2->count; i++) {
            atomic_fetch_add(&node2->children[i]->rc, 1);
        }
    } else {
        if (tr->item_clone) {
            int n = 0;
            bool oom = false;
            for (int i = 0; i < node2->count; i++) {
                if (!tr->item_clone(node->items[i].data, &node2->items[i].data,
                    tr->udata))
                {
                    oom = true;
                    break;
                }
                n++;
            }
            if (oom) {
                if (tr->item_free) {
                    for (int i = 0; i < n; i++) {
                        tr->item_free(node2->items[i].data, tr->udata);
                    }
                }
                tr->free(node2);
                return NULL;
            }
        }
    }
    return node2;
}

static void node_free(struct rtree *tr, struct node *node) {
    if (atomic_fetch_sub(&node->rc, 1) > 0) return;
    if (node->kind == BRANCH) {
        for (int i = 0; i < node->count; i++) {
            node_free(tr, node->children[i]);
        }
    } else {
        if (tr->item_free) {
            for (int i = 0; i < node->count; i++) {
                tr->item_free(node->items[i].data, tr->udata);
            }
        }
    }
    tr->free(node);
}

#define cow_node_or(rnode, code) { \
    if (atomic_load(&(rnode)->rc) > 0) { \
        struct node *node2 = node_copy(tr, (rnode)); \
        if (!node2) { code; } \
        atomic_fetch_sub(&(rnode)->rc, 1); \
        (rnode) = node2; \
    } \
}

static void rect_expand(struct rect *rect, const struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (other->min[i] < rect->min[i]) rect->min[i] = other->min[i];
        if (other->max[i] > rect->max[i]) rect->max[i] = other->max[i];
    }
}

static double rect_area(const struct rect *rect) {
    double area = (double)(rect->max[0]) - (double)(rect->min[0]);
    for (int i = 1; i < DIMS; i++) {
        area *= (double)(rect->max[i]) - (double)(rect->min[i]);
    }
    return area;
}

static bool rect_contains(const struct rect *rect, const struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (other->min[i] < rect->min[i] || other->max[i] > rect->max[i]) {
            return false;
        }
    }
    return true;
}

static bool rect_intersects(const struct rect *rect, const struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (other->min[i] > rect->max[i] || other->max[i] < rect->min[i]) {
            return false;
        }
    }
    return true;
}

static bool nums_equal(NUMTYPE a, NUMTYPE b) {
    return !(a < b || a > b);
}

static bool rect_onedge(const struct rect *rect, const struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (nums_equal(rect->min[i], other->min[i])) {
            return true;
        }
        if (nums_equal(rect->max[i], other->max[i])) {
            return true;
        }
    }
    return false;
}

static bool rect_equals(const struct rect *rect, const struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (!nums_equal(rect->min[i], other->min[i])) {
            return false;
        }
        if (!nums_equal(rect->max[i], other->max[i])) {
            return false;
        }
    }
    return true;
}

// swap two rectangles
static void node_swap(struct node *node, int i, int j) {
    struct rect tmp = node->rects[i];
    node->rects[i] = node->rects[j];
    node->rects[j] = tmp;
    if (node->kind == LEAF) {
        struct item tmp = node->items[i];
        node->items[i] = node->items[j];
        node->items[j] = tmp;
    } else {
        struct node *tmp = node->children[i];
        node->children[i] = node->children[j];
        node->children[j] = tmp;
    }
}

static void node_qsort(struct node *node, int s, int e, int axis, bool rev, 
    bool max)
{
    int nrects = e - s;
    if (nrects < 2) {
        return;
    }
    int left = 0;
    int right = nrects-1;
    int pivot = nrects / 2; // rand and mod not worth it
    node_swap(node, s+pivot, s+right);
    struct rect *rects = &node->rects[s];
    if (!rev) {
        if (!max) {
            for (int i = 0; i < nrects; i++) {
                if (rects[i].min[axis] < rects[right].min[axis]) {
                    node_swap(node, s+i, s+left);
                    left++;
                }
            }
        }
        // else {
        //     // unreachable
        //     for (int i = 0; i < nrects; i++) {
        //         if (rects[i].max[axis] < rects[right].max[axis]) {
        //             node_swap(node, s+i, s+left);
        //             left++;
        //         }
        //     }
        // }
    } else {
        if (!max) {
            for (int i = 0; i < nrects; i++) {
                if (rects[right].min[axis] < rects[i].min[axis]) {
                    node_swap(node, s+i, s+left);
                    left++;
                }
            }
        } else {
            for (int i = 0; i < nrects; i++) {
                if (rects[right].max[axis] < rects[i].max[axis]) {
                    node_swap(node, s+i, s+left);
                    left++;
                }
            }
        }
    }
    node_swap(node, s+left, s+right);
    node_qsort(node, s, s+left, axis, rev, max);
    node_qsort(node, s+left+1, e, axis, rev, max);
}

// sort the node rectangles
static void node_sort(struct node *node) {
    node_qsort(node, 0, node->count, 0, false, false);
}

// sort the node rectangles by the axis. used during splits
static void node_sort_by_axis(struct node *node, int axis, bool rev, bool max) {
    node_qsort(node, 0, node->count, axis, rev, max);
}

static int rect_largest_axis(const struct rect *rect) {
    int axis = 0;
    double nlength = (double)rect->max[0] - (double)rect->min[0];
    for (int i = 1; i < DIMS; i++) {
        double length = (double)rect->max[i] - (double)rect->min[i];
        if (length > nlength) {
            nlength = length;
            axis = i;
        }
    }
    return axis;
}

static void node_move_rect_at_index_into(struct node *from, int index, 
    struct node *into)
{
    into->rects[into->count] = from->rects[index];
    from->rects[index] = from->rects[from->count-1];
    if (from->kind == LEAF) {
        into->items[into->count] = from->items[index];
        from->items[index] = from->items[from->count-1];
    } else {
        into->children[into->count] = from->children[index];
        from->children[index] = from->children[from->count-1];
    }
    from->count--;
    into->count++;
}

static struct node *node_split_largest_axis_edge_snap(struct rtree *tr, 
    struct rect *rect, struct node *left) 
{
    int axis = rect_largest_axis(rect);
    struct node *right = node_new(tr, left->kind);
    if (!right) return NULL;
    for (int i = 0; i < left->count; i++) {
        double min_dist = (double)left->rects[i].min[axis] - 
                          (double)rect->min[axis];
        double max_dist = (double)rect->max[axis] - 
                          (double)left->rects[i].max[axis];
        if (min_dist < max_dist) {
            // stay left
        } else {
            // move to right
            node_move_rect_at_index_into(left, i, right);
            i--;
        }
    }
    // Make sure that both left and right nodes have at least
    // min_entries by moving items into underflowed nodes.
    if (left->count < MIN_ENTRIES) {
        // reverse sort by min axis
        node_sort_by_axis(right, axis, true, false);
        do { 
            node_move_rect_at_index_into(right, right->count-1, left);
        } while (left->count < MIN_ENTRIES);
    } else if (right->count < MIN_ENTRIES) {
        // reverse sort by max axis
        node_sort_by_axis(left, axis, true, true);
        do { 
            node_move_rect_at_index_into(left, left->count-1, right);
        } while (right->count < MIN_ENTRIES);
    }
    node_sort(right);
    node_sort(left);
    return right;
}

static struct node *node_split(struct rtree *tr, struct rect *r,
    struct node *left)
{
    return node_split_largest_axis_edge_snap(tr, r, left);
}

static int node_rsearch(const struct node *node, NUMTYPE key) {
    for (int i = 0; i < node->count; i++) {
        if (!(node->rects[i].min[0] < key)) {
            return i;
        }
    }
    return node->count;
}

// unionedArea returns the area of two rects expanded
static double rect_unioned_area(const struct rect *rect, 
    const struct rect *other)
{
    double area = (double)MAX(rect->max[0], other->max[0]) - 
                  (double)MIN(rect->min[0], other->min[0]);
    for (int i = 1; i < DIMS; i++) {
        area *= (double)MAX(rect->max[i], other->max[i]) - 
                (double)MIN(rect->min[i], other->min[i]);
    }
    return area;
}

static int node_choose_least_enlargement(const struct node *node, 
    const struct rect *ir)
{
    int j = 0;
    double jenlarge = INFINITY;
    double jarea = 0;
    (void)jarea;

    for (int i = 0; i < node->count; i++) {
        // calculate the enlarged area
        double uarea = rect_unioned_area(&node->rects[i], ir);
        double area = rect_area(&node->rects[i]);
        double enlarge = uarea - area;
        if ((enlarge < jenlarge)
#ifndef IGNORE_AREA_EQUALITY_CHECK
            || (!(enlarge > jenlarge) && area < jarea)
#endif
        ) {
            j = i;
            jenlarge = enlarge;
            jarea = area;
        }
    }
    return j;
}

static int node_choose_subtree(const struct node *node, 
    const struct rect *ir)
{
    // Take a quick look for the first node that contain the rect.
#if FAST_CHOOSER == 1
        int index = -1;
        double narea;
        for (int i = 0; i < node->count; i++) {
            if (rect_contains(&node->rects[i], ir)) {
                double area = rect_area(&node->rects[i]);
                if (index == -1 || area < narea) {
                    narea = area;
                    index = i;
                }
            }
        }
        if (index != -1) {
            return index;
        }
#elif FAST_CHOOSER == 2
        for (int i = 0; i < node->count; i++) {
            if (rect_contains(&node->rects[i], ir)) {
                return i;
            }
        }
#endif

    // Fallback to using che "choose least enlargment" algorithm.
    return node_choose_least_enlargement(node, ir);
}

static struct rect node_rect_calc(const struct node *node) {
    struct rect rect = node->rects[0];
    for (int i = 1; i < node->count; i++) {
        rect_expand(&rect, &node->rects[i]);
    }
    return rect;
}

static int node_order_to_right(struct node *node, int index) {
    while (index < node->count-1 && 
        node->rects[index+1].min[0] < node->rects[index].min[0]) 
    {
        node_swap(node, index+1, index);
        index++;
    }
    return index;
}

static int node_order_to_left(struct node *node, int index) {
    while (index > 0 && node->rects[index].min[0] < 
        node->rects[index-1].min[0])
    {
        node_swap(node,index, index-1);
        index--;
    }
    return index;
}

// node_insert returns false if out of memory
static bool node_insert(struct rtree *tr, struct rect *nr, struct node *node, 
    struct rect *ir, struct item item, bool *split, bool *grown)
{
    *split = false;
    *grown = false;
    if (node->kind == LEAF) {
        if (node->count == MAX_ENTRIES) {
            *split = true;
            return true;
        }
        int index = node_rsearch(node, ir->min[0]);
        memmove(&node->rects[index+1], &node->rects[index], 
            (node->count-index)*sizeof(struct rect));
        memmove(&node->items[index+1], &node->items[index], 
            (node->count-index)*sizeof(struct item));
        node->rects[index] = *ir;
        node->items[index] = item;
        node->count++;
        *grown = !rect_contains(nr, ir);
        return true;
    }

    // Choose a subtree for inserting the rectangle.
    int index = node_choose_subtree(node, ir);
    cow_node_or(node->children[index], return false);
    if (!node_insert(tr, &node->rects[index], node->children[index], ir, item, 
        split, grown))
    {
        return false;
    }
    if (*split) {
        if (node->count == MAX_ENTRIES) {
            return true;
        }
        // split the child node
        struct node *left = node->children[index];
        struct node *right = node_split(tr, &node->rects[index], left);
        if (!right) {
            return false;
        }
        node->rects[index] = node_rect_calc(left);
        memmove(&node->rects[index+2], &node->rects[index+1], 
            (node->count-(index+1))*sizeof(struct rect));
        memmove(&node->children[index+2], &node->children[index+1], 
            (node->count-(index+1))*sizeof(struct node*));
        node->rects[index+1] = node_rect_calc(right);
        node->children[index+1] = right;
        node->count++;
        if (node->rects[index].min[0] > node->rects[index+1].min[0]) {
            node_swap(node, index+1, index);
        }
        index++;
        node_order_to_right(node, index);
        return node_insert(tr, nr, node, ir, item, split, grown);
    }
    if (*grown) {
        // The child rectangle must expand to accomadate the new item.
        rect_expand(&node->rects[index], ir);
        node_order_to_left(node, index);
        *grown = !rect_contains(nr, ir);
    }
    return true;
}

struct rtree *rtree_new_with_allocator(void *(*_malloc)(size_t), 
    void (*_free)(void*)
) {
    _malloc = _malloc ? _malloc : malloc;
    _free = _free ? _free : free;
    struct rtree *tr = (struct rtree *)_malloc(sizeof(struct rtree));
    if (!tr) return NULL;
    memset(tr, 0, sizeof(struct rtree));
    tr->malloc = _malloc;
    tr->free = _free;
    return tr;
}

struct rtree *rtree_new(void) {
    return rtree_new_with_allocator(NULL, NULL);
}

void rtree_set_item_callbacks(struct rtree *tr,
    bool (*clone)(const DATATYPE item, DATATYPE *into, void *udata),
    void (*free)(const DATATYPE item, void *udata))
{
    tr->item_clone = clone;
    tr->item_free = free;
}

bool rtree_insert(struct rtree *tr, const NUMTYPE *min, 
    const NUMTYPE *max, const DATATYPE data) 
{
    // prepare the inputs
    struct rect rect;
    memcpy(&rect.min[0], min, sizeof(NUMTYPE)*DIMS);
    memcpy(&rect.max[0], max?max:min, sizeof(NUMTYPE)*DIMS);
    struct item item;
    if (tr->item_clone) {
        if (!tr->item_clone(data, &item.data, tr->udata)) {
            return false;
        }
    } else {
        memcpy(&item.data, &data, sizeof(DATATYPE));
    }
insert:
    if (!tr->root) {
        struct node *new_root = node_new(tr, LEAF);
        if (!new_root) goto oom;
        tr->root = new_root;
        tr->rect = rect;
        tr->height = 1;
    }
    bool split = false;
    bool grown = false;
    cow_node_or(tr->root, goto oom);
    if (!node_insert(tr, &tr->rect, tr->root, &rect, item, &split, &grown)) {
        goto oom;
    }
    if (split) {
        struct node *new_root = node_new(tr, BRANCH);
        if (!new_root) goto oom;
        struct node *left = tr->root;
        struct node *right = node_split(tr, &tr->rect, left);
        if (!right) {
            tr->free(new_root);
            goto oom;
        }
        tr->root = new_root;
        tr->root->rects[0] = node_rect_calc(left);
        tr->root->rects[1] = node_rect_calc(right);
        tr->root->children[0] = left;
        tr->root->children[1] = right;
        tr->root->count = 2;
        tr->height++;
        node_sort(tr->root);
        goto insert;
    }
    if (grown) {
        rect_expand(&tr->rect, &rect);
        node_sort(tr->root);
    }
    tr->count++;
    return true;
oom:
    if (tr->item_free) {
        tr->item_free(item.data, tr->udata);
    }
    return false;
}

void rtree_free(struct rtree *tr) {
    if (tr->root) {
        node_free(tr, tr->root);
    }
    tr->free(tr);
}

static bool node_search(struct node *node, struct rect *rect,
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, 
        void *udata), 
    void *udata) 
{
    if (node->kind == LEAF) {
        for (int i = 0; i < node->count; i++) {
            if (rect_intersects(&node->rects[i], rect)) {
                if (!iter(node->rects[i].min, node->rects[i].max, 
                    node->items[i].data, udata))
                {
                    return false;
                }
            }
        }
        return true;
    }
    for (int i = 0; i < node->count; i++) {
        if (rect_intersects(&node->rects[i], rect)) {
            if (!node_search(node->children[i], rect, iter, udata)) {
                return false;
            }
        }
    }
    return true;
}

void rtree_search(const struct rtree *tr, 
    const NUMTYPE min[], const NUMTYPE max[],
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, 
        void *udata), 
    void *udata)
{
    struct rect rect;
    memcpy(&rect.min[0], min, sizeof(NUMTYPE)*DIMS);
    memcpy(&rect.max[0], max?max:min, sizeof(NUMTYPE)*DIMS);
    if (tr->root && rect_intersects(&tr->rect, &rect)) {
        node_search(tr->root, &rect, iter, udata);
    }
}

static bool node_scan(struct node *node,
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, 
        void *udata), 
    void *udata) 
{
    if (node->kind == LEAF) {
        for (int i = 0; i < node->count; i++) {
            if (!iter(node->rects[i].min, node->rects[i].max, 
                node->items[i].data, udata))
            {
                return false;
            }
        }
        return true;
    }
    for (int i = 0; i < node->count; i++) {
        if (!node_scan(node->children[i], iter, udata)) {
            return false;
        }
    }
    return true;
}

void rtree_scan(const struct rtree *tr, 
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, 
        void *udata), 
    void *udata)
{
    if (tr->root) {
        node_scan(tr->root, iter, udata);
    }
}

size_t rtree_count(const struct rtree *tr) {
    return tr->count;
}

static bool node_delete(struct rtree *tr, struct rect *nr, struct node *node, 
    struct rect *ir, struct item item, bool *removed, bool *shrunk,
    int (*compare)(const DATATYPE a, const DATATYPE b, void *udata),
    void *udata)
{
    *removed = false;
    *shrunk = false;
    if (node->kind == LEAF) {
        for (int i = 0; i < node->count; i++) {
            if (!rect_contains(ir, &node->rects[i])) {
                continue;
            }
            int cmp;
            if (compare) {
                cmp = compare(node->items[i].data, item.data, udata);
            } else {
                cmp = memcmp(&node->items[i].data, &item.data, sizeof(DATATYPE));
            }
            if (cmp != 0) {
                continue;
            }
            // Found the target item to delete.
            if (tr->item_free) {
                tr->item_free(node->items[i].data, tr->udata);
            }
            memmove(&node->rects[i], &node->rects[i+1], 
                (node->count-(i+1))*sizeof(struct rect));
            memmove(&node->items[i], &node->items[i+1], 
                (node->count-(i+1))*sizeof(struct item));
            node->count--;
            if (rect_onedge(ir, nr)) {
                // The item rect was on the edge of the node rect.
                // We need to recalculate the node rect.
                *nr = node_rect_calc(node);
                // Notify the caller that we shrunk the rect.
                *shrunk = true; 
            }
            *removed = true;
            return true;
        }
        return true;
    }
    for (int i = 0; i < node->count; i++) {
        if (!rect_contains(&node->rects[i], ir)) {
            continue;
        }
        struct rect crect = node->rects[i];
        cow_node_or(node->children[i], return false);
        if (!node_delete(tr, &node->rects[i], node->children[i], ir, item, 
            removed, shrunk, compare, udata))
        {
            return false;
        }
        if (!*removed) {
            continue;
        }
        if (node->children[i]->count == 0) {
            // underflow
            node_free(tr, node->children[i]);
            memmove(&node->rects[i], &node->rects[i+1], 
                (node->count-(i+1))*sizeof(struct rect));
            memmove(&node->children[i], &node->children[i+1], 
                (node->count-(i+1))*sizeof(struct node *));
            node->count--;
            *nr = node_rect_calc(node);
            *shrunk = true;
            return true;
        }
        if (*shrunk) {
            *shrunk = !rect_equals(&node->rects[i], &crect);
            if (*shrunk) {
                *nr = node_rect_calc(node);
            }
            node_order_to_right(node, i);
        }
        return true;
    }
    return true;
}

// returns false if out of memory
static bool rtree_delete0(struct rtree *tr, const NUMTYPE *min, 
    const NUMTYPE *max, const DATATYPE data,
    int (*compare)(const DATATYPE a, const DATATYPE b, void *udata),
    void *udata)
{
    struct rect rect;
    memcpy(&rect.min[0], min, sizeof(NUMTYPE)*DIMS);
    memcpy(&rect.max[0], max?max:min, sizeof(NUMTYPE)*DIMS);
    struct item item;
    memcpy(&item.data, &data, sizeof(DATATYPE));
    if (!tr->root) {
        return true;
    }
    bool removed = false;
    bool shrunk = false;
    cow_node_or(tr->root, return false);
    if (!node_delete(tr, &tr->rect, tr->root, &rect, item, &removed, &shrunk, 
        compare, udata))
    {
        return false;
    }
    if (!removed) {
        return true;
    }
    tr->count--;
    if (tr->count == 0) {
        node_free(tr, tr->root);
        tr->root = NULL;
        memset(&tr->rect, 0, sizeof(struct rect));
        tr->height = 0;
    } else {
        while (tr->root->kind == BRANCH && tr->root->count == 1) {
            struct node *prev = tr->root;
            tr->root = tr->root->children[0];
            prev->count = 0;
            node_free(tr, prev);
            tr->height--;
        }
        if (shrunk) {
            tr->rect = node_rect_calc(tr->root);
        }
    }
    return true;
}

bool rtree_delete(struct rtree *tr, const NUMTYPE *min, const NUMTYPE *max, 
    const DATATYPE data)
{
    return rtree_delete0(tr, min, max, data, NULL, NULL);
}

bool rtree_delete_with_comparator(struct rtree *tr, const NUMTYPE *min, 
    const NUMTYPE *max, const DATATYPE data,
    int (*compare)(const DATATYPE a, const DATATYPE b, void *udata),
    void *udata)
{
    return rtree_delete0(tr, min, max, data, compare, udata);
}

struct rtree *rtree_clone(struct rtree *tr) {
    if (!tr) return NULL;
    struct rtree *tr2 = tr->malloc(sizeof(struct rtree));
    if (!tr2) return NULL;
    memcpy(tr2, tr, sizeof(struct rtree));
    if (tr2->root) atomic_fetch_add(&tr2->root->rc, 1);
    return tr2;
} 

#ifdef TEST_PRIVATE_FUNCTIONS
#include "tests/priv_funcs.h"
#endif
