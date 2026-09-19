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
