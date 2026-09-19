#include "rbtree.h"
#include <stdlib.h>
#include <string.h>

typedef enum { RB_RED, RB_BLACK } rb_color_t;

typedef struct rbnode {
    char *key;
    void *value;
    rb_color_t color;
    struct rbnode *left, *right, *parent;
} rbnode_t;

struct rbtree {
    rbnode_t *root;
    rbnode_t nil_storage;  /* embedded sentinel; t->nil points here */
    rbnode_t *nil;
    size_t size;
    rb_value_free_fn value_free;
};

static void *rb_malloc(size_t n) {
    return malloc(n);
}

static void rb_free(void *p) {
    free(p);
}

rbtree_t *rb_create(rb_value_free_fn value_free) {
    rbtree_t *t = rb_malloc(sizeof *t);
    if (!t) return NULL;
    t->nil_storage.color = RB_BLACK;
    t->nil_storage.left = t->nil_storage.right = t->nil_storage.parent = NULL;
    t->nil_storage.key = NULL;
    t->nil_storage.value = NULL;
    t->nil = &t->nil_storage;
    t->root = t->nil;
    t->size = 0;
    t->value_free = value_free;
    return t;
}

/* Standard BST left rotation around x, re-linking through x's parent
 * (or t->root, when x has none -- x->parent == t->nil). Only pointers
 * move; no allocation, no key/value touched. */
static void left_rotate(rbtree_t *t, rbnode_t *x) {
    rbnode_t *y = x->right;
    x->right = y->left;
    if (y->left != t->nil) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == t->nil) {
        t->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

/* Mirror of left_rotate (swap left/right throughout). */
static void right_rotate(rbtree_t *t, rbnode_t *x) {
    rbnode_t *y = x->left;
    x->left = y->right;
    if (y->right != t->nil) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == t->nil) {
        t->root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }
    y->right = x;
    x->parent = y;
}

/* Restores red-black invariants 1-3 after inserting red leaf z. Only a
 * red parent can violate no-red-red, so the loop's job is to walk that
 * violation up toward the root, one grandparent at a time, until either
 * the parent is black or z reaches the root (root's parent is the
 * shared t->nil, always black, so the loop condition stops the walk
 * there with no extra check needed). */
static void rb_insert_fixup(rbtree_t *t, rbnode_t *z) {
    while (z->parent->color == RB_RED) {
        if (z->parent == z->parent->parent->left) {
            rbnode_t *uncle = z->parent->parent->right;
            if (uncle->color == RB_RED) {
                /* Case 1: red uncle. Parent and uncle can both drop to
                 * black and grandparent can turn red without changing
                 * any path's black-height; the violation just moves up
                 * to the grandparent, so keep looping from there. */
                z->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    /* Case 2: black uncle, z is the "inner" child
                     * (zig-zag). Rotate left at the parent to make z
                     * the outer child instead, falling through to
                     * case 3 with z now pointing at the old parent. */
                    z = z->parent;
                    left_rotate(t, z);
                }
                /* Case 3: black uncle, z is the "outer" child
                 * (zig-zig). Recolor parent/grandparent and rotate
                 * right at the grandparent -- this restores
                 * black-height at this subtree's root without needing
                 * to touch anything further up, so the loop ends. */
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                right_rotate(t, z->parent->parent);
            }
        } else {
            /* Exact mirror of the above with left/right swapped. */
            rbnode_t *uncle = z->parent->parent->left;
            if (uncle->color == RB_RED) {
                z->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    right_rotate(t, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                left_rotate(t, z->parent->parent);
            }
        }
    }
    /* Unconditional: fixes the very first insert (root starts RB_RED in
     * rb_insert) and any run where the loop pushed red up to the root. */
    t->root->color = RB_BLACK;
}

int rb_insert(rbtree_t *t, const char *key, void *value) {
    if (!t) return -1;

    rbnode_t *parent = t->nil;
    rbnode_t *cur = t->root;
    int cmp = 0;
    /* invariant: at each step, key belongs somewhere in cur's subtree
     * (or, once cur == t->nil, key is not present and parent is where
     * the new node attaches). */
    while (cur != t->nil) {
        cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            /* Existing key: overwrite in place, not a new node -- size
             * and structure stay untouched. */
            if (t->value_free) t->value_free(cur->value);
            cur->value = value;
            return 0;
        }
        parent = cur;
        cur = (cmp < 0) ? cur->left : cur->right;
    }

    /* New key. Allocate node + key copy before touching the tree so that
     * on failure the tree is left exactly as it was and value stays
     * owned by the caller, per the header contract. */
    rbnode_t *node = rb_malloc(sizeof *node);
    if (!node) return -1;

    size_t keylen = strlen(key) + 1;
    node->key = rb_malloc(keylen);
    if (!node->key) {
        rb_free(node);
        return -1;
    }
    memcpy(node->key, key, keylen);

    node->value = value;
    node->color = RB_RED;
    node->left = t->nil;
    node->right = t->nil;
    node->parent = parent;

    if (parent == t->nil) {
        t->root = node;
    } else if (cmp < 0) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    rb_insert_fixup(t, node);
    t->size++;
    return 0;
}

void *rb_find(const rbtree_t *t, const char *key) {
    if (!t) return NULL;

    rbnode_t *cur = t->root;
    /* invariant: key, if present, lies within cur's subtree. */
    while (cur != t->nil) {
        int cmp = strcmp(key, cur->key);
        if (cmp == 0) return cur->value;
        cur = (cmp < 0) ? cur->left : cur->right;
    }
    return NULL;
}

size_t rb_size(const rbtree_t *t) {
    return t ? t->size : 0;
}

/* In-order walk checking strictly increasing keys (invariant 4 only --
 * see CLAUDE.md M1 scope; invariants 1/2/3/5 land with fixup/delete). */
static int check_ordering(const rbtree_t *t, rbnode_t *node,
                           const char **prev) {
    if (node == t->nil) return 0;
    if (check_ordering(t, node->left, prev) != 0) return -1;
    if (*prev != NULL && strcmp(*prev, node->key) >= 0) return -1;
    *prev = node->key;
    return check_ordering(t, node->right, prev);
}

int rb_validate(const rbtree_t *t) {
    if (!t) return -1;
    const char *prev = NULL;
    return check_ordering(t, t->root, &prev);
}

static void foreach_inorder(const rbtree_t *t, rbnode_t *node,
                             void (*fn)(const char *, void *, void *),
                             void *ctx) {
    if (node == t->nil) return;
    foreach_inorder(t, node->left, fn, ctx);
    fn(node->key, node->value, ctx);
    foreach_inorder(t, node->right, fn, ctx);
}

void rb_foreach(const rbtree_t *t,
                void (*fn)(const char *key, void *value, void *ctx),
                void *ctx) {
    if (!t) return;
    foreach_inorder(t, t->root, fn, ctx);
}

/* Post-order free: children before parent, value/key/node last. The
 * value_free != NULL guard matters for trees created with value_free ==
 * NULL ("values not owned") -- calling through a NULL function pointer
 * would be UB. */
static void destroy_subtree(rbtree_t *t, rbnode_t *node) {
    if (node == t->nil) return;
    destroy_subtree(t, node->left);
    destroy_subtree(t, node->right);
    if (t->value_free) t->value_free(node->value);
    rb_free(node->key);
    rb_free(node);
}

void rb_destroy(rbtree_t *t) {
    if (!t) return;
    destroy_subtree(t, t->root);
    rb_free(t);
}
