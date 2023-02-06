// Copyright 2023 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "rtree.h"

////////////////////////////////

#define DATATYPE void * 
#define NUMTYPE double
#define DIMS 2
#define MAX_ENTRIES 64

////////////////////////////////

#define panic(_msg_) { \
    fprintf(stderr, "panic: %s (%s:%d)\n", (_msg_), __FILE__, __LINE__); \
    exit(1); \
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

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
    enum kind kind;     // LEAF or BRANCH
    int count;          // number of rects
    struct rect rects[MAX_ENTRIES];
    union {
        struct node *children[MAX_ENTRIES];
        struct item items[MAX_ENTRIES];
    };
};

struct group {
    struct node **nodes;
    int len, cap;
};

struct pool {
    struct group leaves;
    struct group branches;
};

struct rtree {
    size_t count;
    int height;
    struct rect rect;
    struct node *root; 
    struct pool pool;
    void *(*malloc)(size_t);
    void (*free)(void *);
};

static struct node *node_new(struct rtree *tr, enum kind kind) {
    struct node *node = (struct node *)tr->malloc(sizeof(struct node));
    memset(node, 0, sizeof(struct node));
    node->kind = kind;
    return node;
}

static void node_free(struct rtree *tr, struct node *node) {
    if (node->kind == BRANCH) {
        for (int i = 0; i < node->count; i++) {
            node_free(tr, node->children[i]);
        }
    }
    tr->free(node);
}

static struct node *gimme_node(struct rtree *tr, struct group *group) {
    if (group->len == 0) panic("out of nodes");
    struct node *node = group->nodes[--group->len];
    return node;
}

static struct node *gimme_leaf(struct rtree *tr) {
    return gimme_node(tr, &tr->pool.leaves);
}

static struct node *gimme_branch(struct rtree *tr) {
    return gimme_node(tr, &tr->pool.branches);
}

static bool grow_group(struct rtree *tr, struct group *group) {
    int cap = group->cap?group->cap*2:1;
    struct node **nodes = (struct node **)tr->malloc(sizeof(struct node*)*cap);
    if (!nodes) {
        return false;
    }
    memcpy(nodes, group->nodes, group->len*sizeof(struct node*));
    tr->free(group->nodes);
    group->nodes = nodes;
    group->cap = cap;
    return true;
}

static void release_pool(struct rtree *tr) {
    for (int i = 0; i < tr->pool.leaves.len; i++) {
        tr->free(tr->pool.leaves.nodes[i]);
    }
    tr->free(tr->pool.leaves.nodes);
    for (int i = 0; i < tr->pool.branches.len; i++) {
        tr->free(tr->pool.branches.nodes[i]);
    }
    tr->free(tr->pool.branches.nodes);
    memset(&tr->pool, 0, sizeof(struct pool));
}

// fill_pool fills the node pool prior to inserting items. This ensures there
// is enough memory before we begin doing things like splits and shadowing.
// There needs to be at least four available leaf and N*2 branches
// where N is equal to the height of the tree plus two.
static bool fill_pool(struct rtree *tr) {
    while (tr->pool.leaves.len < 4) {
        if (tr->pool.leaves.len == tr->pool.leaves.cap) {
            if (!grow_group(tr, &tr->pool.leaves)) {
                return false;
            }
        }
        struct node *leaf = node_new(tr, LEAF);
        if (!leaf) {
            return false;
        }
        tr->pool.leaves.nodes[tr->pool.leaves.len++] = leaf;
    }
    while (tr->pool.branches.len < tr->height*2+2) {
        if (tr->pool.branches.len == tr->pool.branches.cap) {
            if (!grow_group(tr, &tr->pool.branches)) {
                return false;
            }
        }
        struct node *branch = node_new(tr, BRANCH);
        if (!branch) {
            return false;
        }
        tr->pool.branches.nodes[tr->pool.branches.len++] = branch;
    }
    return true;
}

static void rect_expand(struct rect *rect, struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (other->min[i] < rect->min[i]) rect->min[i] = other->min[i];
        if (other->max[i] > rect->max[i]) rect->max[i] = other->max[i];
    }
}

static double rect_area(struct rect *rect) {
    double area = (double)(rect->max[0]) - (double)(rect->min[0]);
    for (int i = 1; i < DIMS; i++) {
        area *= (double)(rect->max[i]) - (double)(rect->min[i]);
    }
    return area;
}

// contains return true when other is 

static bool rect_contains(struct rect *rect, struct rect *other) {
    for (int i = 0; i < DIMS; i++) {
        if (other->min[i] < rect->min[i] || other->max[i] > rect->max[i]) {
            return false;
        }
    }
    return true;
}

static bool rect_intersects(struct rect *rect, struct rect *other) {
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

static bool rect_onedge(struct rect *rect, struct rect *other) {
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

static bool rect_equals(struct rect *rect, struct rect *other) {
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

// swap two rectanlges
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

static void node_qsort(struct node *node, int s, int e, int axis, bool rev, bool max) {
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
        } else {
            for (int i = 0; i < nrects; i++) {
                if (rects[i].max[axis] < rects[right].max[axis]) {
                    node_swap(node, s+i, s+left);
                    left++;
                }
            }
        }
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

static int rect_largest_axis(struct rect *rect) {
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
    struct node *right = left->kind == LEAF ? gimme_leaf(tr) : gimme_branch(tr);
    for (int i = 0; i < left->count; i++) {
        double min_dist = (double)left->rects[i].min[axis] - (double)rect->min[axis];
        double max_dist = (double)rect->max[axis] - (double)left->rects[i].max[axis];
        if (min_dist < max_dist) {
            // stay left
        } else {
            // move to right
            node_move_rect_at_index_into(left, i, right);
            i--;
        }
    }

    // Make sure that both left and right nodes have at least
    // two entries by moving items into underflowed nodes.
    if (left->count < 2) {
        // reverse sort by min axis
        node_sort_by_axis(right, axis, true, false);
        while (left->count < 2) {
            node_move_rect_at_index_into(right, right->count-1, left);
        }
    } else if (right->count < 2) {
        // reverse sort by max axis
        node_sort_by_axis(left, axis, true, true);
        while (right->count < 2) {
            node_move_rect_at_index_into(left, left->count-1, right);
        }
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

static int node_rsearch(struct node *node, NUMTYPE key) {
    for (int i = 0; i < node->count; i++) {
        if (!(node->rects[i].min[0] < key)) {
            return i;
        }
    }
    return node->count;
}

// unionedArea returns the area of two rects expanded
static double rect_unioned_area(struct rect *rect, struct rect *other) {
    double area = (double)MAX(rect->max[0], other->max[0]) - 
                  (double)MIN(rect->min[0], other->min[0]);
    for (int i = 1; i < DIMS; i++) {
        area *= (double)MAX(rect->max[i], other->max[i]) - 
                (double)MIN(rect->min[i], other->min[i]);
    }
    return area;
}

static int node_choose_least_enlargement(struct node *node, struct rect *ir) {
    int j = -1;
    double jenlargement = 0;
    double jarea = 0;
    for (int i = 0; i < node->count; i++) {
        // calculate the enlarged area
        double uarea = rect_unioned_area(&node->rects[i], ir);
        double area = rect_area(&node->rects[i]);
        double enlargement = uarea - area;
        if (j == -1 || enlargement < jenlargement || 
            (!(enlargement > jenlargement) && area < jarea)) 
        {
            j = i;
            jenlargement = enlargement;
            jarea = area;
        }
    }
    return j;
}

static int node_choose_subtree(struct node *node, struct rect *ir) {
    // Take a quick look for the first node that contain the rect.
    for (int i = 0; i < node->count; i++) {
        if (rect_contains(&node->rects[i], ir)) {
            return i;
        }
    }
    // Fallback to using che "choose least enlargment" algorithm.
    return node_choose_least_enlargement(node, ir);
}

static struct rect node_rect_calc(struct node *node) {
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

static void node_insert(struct rtree *tr, struct rect *nr, struct node *node, 
    struct rect *ir, struct item item, bool *split, bool *grown)
{
    *split = false;
    *grown = false;
    if (node->kind == LEAF) {
        if (node->count == MAX_ENTRIES) {
            *split = true;
            return;
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
        return;
    }

    // Choose a subtree for inserting the rectangle.
    int index = node_choose_subtree(node, ir);
    node_insert(tr, &node->rects[index], node->children[index], ir, item, 
        split, grown);
    if (*split) {
        if (node->count == MAX_ENTRIES) {
            return;
        }
        // split the child node
        struct node *left = node->children[index];
        struct node *right = node_split(tr, &node->rects[index], left);
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
}

static struct rtree *_rtree_new_with_allocator(void *(*cust_malloc)(size_t), 
    void (*cust_free)(void*)
) {
    if (!cust_malloc) cust_malloc = malloc;
    if (!cust_free) cust_free = free;
    struct rtree *tr = (struct rtree *)cust_malloc(sizeof(struct rtree));
    if (!tr) {
        return NULL;
    }
    memset(tr, 0, sizeof(struct rtree));
    tr->malloc = cust_malloc;
    tr->free = cust_free;
    return tr;
}

static struct rtree *_rtree_new() {
    return rtree_new_with_allocator(NULL, NULL);
}

static bool _rtree_insert(struct rtree *tr, const NUMTYPE *min, 
    const NUMTYPE *max, const DATATYPE data) 
{
    struct rect rect;
    memcpy(&rect.min[0], min, sizeof(NUMTYPE)*DIMS);
    memcpy(&rect.max[0], max?max:min, sizeof(NUMTYPE)*DIMS);
    struct item item;
    memcpy(&item.data, &data, sizeof(DATATYPE));
    if (!fill_pool(tr)) {
        return false;
    }
    
    if (!tr->root) {
        tr->root = gimme_leaf(tr);
        tr->rect = rect;
    }
    bool split = false;
    bool grown = false;
    node_insert(tr, &tr->rect, tr->root, &rect, item, &split, &grown);
    if (split) {
        struct node *left = tr->root;
        struct node *right = node_split(tr, &tr->rect, left);
        tr->root = gimme_branch(tr);
        tr->root->rects[0] = node_rect_calc(left);
        tr->root->rects[1] = node_rect_calc(right);
        tr->root->children[0] = left;
        tr->root->children[1] = right;
        tr->root->count = 2;
        tr->height++;
        node_sort(tr->root);
        return _rtree_insert(tr, min, max, data);
    }
    if (grown) {
        rect_expand(&tr->rect, &rect);
        node_sort(tr->root);
    }
    tr->count++;
    return true;
}

static void _rtree_free(struct rtree *tr) {
    if (tr->root) {
        node_free(tr, tr->root);
    }
    release_pool(tr);
    tr->free(tr);
}

static bool node_search(struct node *node, struct rect *rect,
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, void *udata), 
    void *udata) 
{
    if (node->kind == LEAF) {
        for (int i = 0; i < node->count; i++) {
            if (rect_intersects(&node->rects[i], rect)) {
                if (!iter(node->rects[i].min, node->rects[i].max, node->items[i].data, udata)) {
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

static void _rtree_search(struct rtree *tr, const NUMTYPE *min, const NUMTYPE *max,
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, void *udata), 
    void *udata)
{
    struct rect rect;
    memcpy(&rect.min[0], min, sizeof(NUMTYPE)*DIMS);
    memcpy(&rect.max[0], max?max:min, sizeof(NUMTYPE)*DIMS);
    if (tr->root && rect_intersects(&tr->rect, &rect)) {
        node_search(tr->root, &rect, iter, udata);
    }
}

static size_t _rtree_count(struct rtree *tr) {
    return tr->count;
}

static void node_delete(struct rtree *tr, struct rect *nr, struct node *node, 
    struct rect *ir, struct item item, bool *removed, bool *shrunk)
{
    *removed = false;
    *shrunk = false;
    if (node->kind == LEAF) {
        for (int i = 0; i < node->count; i++) {
            if (!rect_contains(ir, &node->rects[i]) || 
                node->items[i].data != item.data) 
            {
                continue;
            }

            // Found the target item to delete.
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
            return;
        }
        return;
    }
    for (int i = 0; i < node->count; i++) {
        if (!rect_contains(&node->rects[i], ir)) {
            continue;
        }
        struct rect crect = node->rects[i];
        node_delete(tr, &node->rects[i], node->children[i], ir, item, removed,
            shrunk);
        if (!*removed) {
            continue;
        }
        if (node->children[i]->count == 0) {
            // underflow. remove node
            node_free(tr, node->children[i]);
            memmove(&node->rects[i], &node->rects[i+1], 
                (node->count-(i+1))*sizeof(struct rect));
            memmove(&node->children[i], &node->children[i+1], 
                (node->count-(i+1))*sizeof(struct node *));
            node->count--;
            *nr = node_rect_calc(node);
            *shrunk = true;
            return;
        }
        if (*shrunk) {
            *shrunk = !rect_equals(&node->rects[i], &crect);
            if (*shrunk) {
                *nr = node_rect_calc(node);
            }
            node_order_to_right(node, i);
        }
        return;
    }
    return;
}

static void _rtree_delete(struct rtree *tr, const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data) {
    struct rect rect;
    memcpy(&rect.min[0], min, sizeof(NUMTYPE)*DIMS);
    memcpy(&rect.max[0], max?max:min, sizeof(NUMTYPE)*DIMS);
    struct item item;
    memcpy(&item.data, &data, sizeof(DATATYPE));
    if (!tr->root || !rect_contains(&tr->rect, &rect)) {
        return;
    }
    bool removed = false;
    bool shrunk = false;
    node_delete(tr, &tr->rect, tr->root, &rect, item, &removed, &shrunk);
    if (!removed) {
        return;
    }
    tr->count--;
    if (tr->count == 0) {
        node_free(tr, tr->root);
        tr->root = NULL;
        memset(&tr->rect, 0, sizeof(struct rect));
    } else {
        while (tr->root->kind == BRANCH && tr->root->count == 1) {
            struct node *prev = tr->root;
            tr->root = tr->root->children[0];
            prev->count = 0;
            node_free(tr, prev);
        }
        if (shrunk) {
            tr->rect = node_rect_calc(tr->root);
        }
    }
}

//////////////////
// checker
//////////////////

static void node_check_order(struct node *node) {
    for (int i = 1; i < node->count; i++) {
        if (node->rects[i].min[0] < node->rects[i-1].min[0]) {
            panic("out of order")
        }
        if (node->kind == BRANCH) {
            node_check_order(node->children[i]);
        }
    }
}

static void rtree_check_order(struct rtree *tr) {
    if (tr->root) {
        node_check_order(tr->root);
    }
}

static void node_check_rect(struct rect *rect, struct node *node) {
    struct rect rect2 = node_rect_calc(node);
    if (!rect_equals(rect, &rect2)){
        panic("invalid rect")
    }
    if (node->kind == BRANCH) {
        for (int i = 0; i < node->count; i++) {
            node_check_rect(&node->rects[i], node->children[i]);
        }
    }
}

static void rtree_check_rects(struct rtree *tr) {
    if (tr->root) {
        node_check_rect(&tr->rect, tr->root);
    }
}

static void _rtree_check(struct rtree *tr) {
    rtree_check_order(tr);
    rtree_check_rects(tr);
}




static const double svg_scale = 20.0;
static const char *strokes[] = { "black", "red", "green", "purple" };
static const int nstrokes = 4;

static void node_write_svg(struct node *node, struct rect *rect, FILE *f, int depth) {
    bool point = rect->min[0] == rect->max[0] && rect->min[1] == rect->max[1];
    if (node) {
        if (node->kind == BRANCH) {
            for (int i = 0; i < node->count; i++) {
                node_write_svg(node->children[i], &node->rects[i], f, depth+1);
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
static void _rtree_write_svg(struct rtree *tr, const char *path) {
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

// EXTERNS

struct rtree *rtree_new_with_allocator(void *(*malloc)(size_t), 
    void (*free)(void*))
{
    return _rtree_new_with_allocator(malloc, free);
}
struct rtree *rtree_new() {
    return _rtree_new();
}

// rtree_insert inserts an item into the rtree. 
// This operation performs a copy of the data that is pointed to in the second
// and third arguments. The R-tree expects a rectangle, which is two arrays of
// doubles. The first N values as the minimum corner of the rect, and the next
// N values as the maximum corner of the rect, where N is the number of
// dimensions.
// When inserting points, the max coordinates is optional (set to NULL).
// Returns false if the system is out of memory.
bool rtree_insert(struct rtree *tr, const NUMTYPE *min, const NUMTYPE *max, 
    const DATATYPE data) 
{
    return _rtree_insert(tr, min, max, data);
}

void rtree_free(struct rtree *tr) {
    return _rtree_free(tr);
}

void rtree_search(struct rtree *tr, const NUMTYPE *min, const NUMTYPE *max,
    bool (*iter)(const NUMTYPE *min, const NUMTYPE *max, const DATATYPE data, 
        void *udata), 
    void *udata)
{
    return _rtree_search(tr, min, max, iter, udata);
}

// rtree_count returns the number of items in the rtree.
size_t rtree_count(struct rtree *tr) {
    return _rtree_count(tr);
}

// rtree_delete deletes an item from the rtree. 
void rtree_delete(struct rtree *tr, const NUMTYPE *min, const NUMTYPE *max, 
    const DATATYPE data)
{
    return _rtree_delete(tr, min, max, data);
}

void rtree_write_svg(struct rtree *tr, const char *path) {
    return _rtree_write_svg(tr, path);
}

void rtree_check(struct rtree *tr) {
    return _rtree_check(tr);
}