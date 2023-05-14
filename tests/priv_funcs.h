#ifdef TEST_PRIVATE_FUNCTIONS

//////////////////
// checker
//////////////////

static bool node_check_order(const struct node *node) {
    for (int i = 1; i < node->count; i++) {
        if (node->rects[i].min[0] < node->rects[i-1].min[0]) {
            fprintf(stderr, "out of order\n");
            return false;
        }
        if (node->kind == BRANCH) {
            if (!node_check_order(node->children[i])) return false;
        }
    }
    return true;
}

static bool rtree_check_order(const struct rtree *tr) {
    if (tr->root) {
        if (!node_check_order(tr->root)) return false;
    }
    return true;
}

static bool node_check_rect(const struct rect *rect, struct node *node) {
    struct rect rect2 = node_rect_calc(node);
    if (!rect_equals(rect, &rect2)){
        fprintf(stderr, "invalid rect\n");
        return false;
    }
    if (node->kind == BRANCH) {
        for (int i = 0; i < node->count; i++) {
            if (!node_check_rect(&node->rects[i], node->children[i])) {
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
        node = node->children[0];
    }
    if (height != tr->height) {
        fprintf(stderr, "invalid height\n");
        return false;
    }
    return true;
}

bool rtree_check(const struct rtree *tr) {
    if (!rtree_check_order(tr)) return false;
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

#endif // TEST_PRIVATE_FUNCTIONS
