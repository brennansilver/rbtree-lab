#include "rbtree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* value_free stub: counts frees so overwrite/destroy tests can check
 * exactly how many times a value was freed, and plain-insert tests can
 * check it's never called spuriously. */
static int g_free_count = 0;

static void counting_free(void *value) {
    g_free_count++;
    free(value);
}

/* Heap-boxes an int so it can be used as an owned rb_insert value. */
static int *make_int(int v) {
    int *p = malloc(sizeof *p);
    assert(p != NULL);
    *p = v;
    return p;
}

/* Empty tree: rb_create succeeds, rb_size starts at 0 (not uninitialized
 * or off-by-one), and rb_validate/rb_destroy both correctly handle their
 * t->root == t->nil base case on their own, not just as the terminating
 * step of a bigger recursive walk. */
static void test_create_empty(void) {
    rbtree_t *t = rb_create(NULL);
    assert(t != NULL);
    assert(rb_size(t) == 0);
    assert(rb_validate(t) == 0);
    rb_destroy(t);
}

/* Insert keys in scrambled order and, after each insert, re-check every
 * key inserted so far (not just the newest) -- catches a flipped
 * comparison-direction bug in rb_insert immediately, pinpointed to the
 * insert that broke it, rather than only surfacing as an aggregate
 * failure at the end. Also checks: rb_size increments exactly once per
 * real insert, rb_find returns NULL for an absent key and an empty-string
 * key, value_free is never invoked on a non-overwrite insert (isolates
 * the overwrite test from this one), and rb_validate's ordering check
 * runs over a real multi-level, both-subtrees tree for the first time. */
static void test_insert_find_mixed_order(void) {
    rbtree_t *t = rb_create(counting_free);
    g_free_count = 0;

    const char *keys[] = {"m", "c", "x", "a", "d", "z", "b"};
    size_t n = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < n; i++) {
        int rc = rb_insert(t, keys[i], make_int((int)i));
        assert(rc == 0);
        assert(rb_size(t) == i + 1);
        for (size_t j = 0; j <= i; j++) {
            void *found = rb_find(t, keys[j]);
            assert(found != NULL);
            assert(*(int *)found == (int)j);
        }
    }

    assert(rb_find(t, "nope") == NULL);
    assert(rb_find(t, "") == NULL);
    assert(rb_size(t) == n);
    assert(rb_validate(t) == 0);
    assert(g_free_count == 0);

    rb_destroy(t);
}

/* Overwrite an existing key with a second rb_insert and check the header's
 * documented ownership rule: the old value is freed via value_free exactly
 * once (not leaked, not double-freed), rb_size stays at 1 (a duplicate key
 * is an overwrite, not a new node -- rb_validate's strict ordering check
 * is a second line of defense against a duplicate slipping in as a real
 * second node), the new value is what rb_find returns afterward, and the
 * tree is still valid (overwrite must not touch structure). */
static void test_insert_duplicate_overwrites_and_frees_old_value(void) {
    rbtree_t *t = rb_create(counting_free);
    g_free_count = 0;

    assert(rb_insert(t, "dup", make_int(1)) == 0);
    assert(rb_size(t) == 1);

    assert(rb_insert(t, "dup", make_int(2)) == 0);
    assert(rb_size(t) == 1);
    assert(g_free_count == 1);

    void *found = rb_find(t, "dup");
    assert(found != NULL);
    assert(*(int *)found == 2);

    assert(rb_validate(t) == 0);
    rb_destroy(t);
}

/* rb_validate/rb_destroy with value_free == NULL (header: "value_free may
 * be NULL (values not owned)") -- destroy_subtree must guard the call
 * (t->value_free != NULL) rather than unconditionally invoking it, which
 * would crash through a NULL function pointer; not caught by any test
 * above since they all register counting_free. Also exercises rb_validate
 * in isolation from rb_find/rb_size assertions, on a tree shape (root with
 * a right-child two levels deep on each side) that crosses both branches
 * of check_ordering's recursion. */
static void test_validate_ordering_on_populated_tree(void) {
    rbtree_t *t = rb_create(NULL);
    const char *keys[] = {"banana", "apple", "cherry", "date", "avocado"};
    size_t n = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < n; i++) {
        assert(rb_insert(t, keys[i], NULL) == 0);
    }
    assert(rb_validate(t) == 0);
    rb_destroy(t);
}

struct foreach_ctx {
    const char *last_key;
    int calls;
    int strictly_increasing;
};

static void foreach_check_order(const char *key, void *value, void *ctx_v) {
    (void)value;
    struct foreach_ctx *ctx = ctx_v;
    if (ctx->last_key != NULL && strcmp(ctx->last_key, key) >= 0) {
        ctx->strictly_increasing = 0;
    }
    ctx->last_key = key;
    ctx->calls++;
}

/* rb_foreach is a separate traversal implementation from rb_validate's
 * check_ordering, so a bug specific to rb_foreach (not check_ordering)
 * would go uncaught by every test above -- none of them call rb_foreach.
 * Checks: visits happen in-order (strictly_increasing via strcmp between
 * consecutive callback calls, catching pre-order/wrong-branch bugs),
 * exactly n calls happen (catches a skipped subtree if fewer, or a
 * revisited node if more), and ctx is threaded through to the callback
 * correctly (implicit: assertions only pass if the callback actually
 * mutated *ctx through the pointer rb_foreach handed it). */
static void test_foreach_visits_in_increasing_order(void) {
    rbtree_t *t = rb_create(NULL);
    const char *keys[] = {"m", "c", "x", "a", "d", "z", "b"};
    size_t n = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < n; i++) {
        assert(rb_insert(t, keys[i], NULL) == 0);
    }

    struct foreach_ctx ctx = {.last_key = NULL, .calls = 0,
                               .strictly_increasing = 1};
    rb_foreach(t, foreach_check_order, &ctx);
    assert(ctx.calls == (int)n);
    assert(ctx.strictly_increasing);

    rb_destroy(t);
}

/* rb_destroy's traversal must free every node's owned value across a
 * multi-node tree exactly once -- none of the tests above assert a total
 * free count after rb_destroy, so a leak or double-free in the destroy
 * walk specifically would go uncaught. g_free_count < n means a subtree
 * was skipped (leaked values); g_free_count > n means a node was visited
 * twice (double free, UB even without asan/valgrind to catch it). Does
 * NOT cover the key-copy or node-struct frees -- counting_free only wraps
 * value frees; those two allocation kinds are covered by make asan / make
 * memcheck instead. */
static void test_destroy_frees_owned_values(void) {
    rbtree_t *t = rb_create(counting_free);
    g_free_count = 0;
    const char *keys[] = {"one", "two", "three"};
    size_t n = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < n; i++) {
        assert(rb_insert(t, keys[i], make_int((int)i)) == 0);
    }
    rb_destroy(t);
    assert(g_free_count == (int)n);
}

/* NULL-guard design decision (plan-mode, not a header requirement except
 * for rb_destroy): every function must treat t == NULL as a clean,
 * documented case instead of dereferencing t->root/t->nil/t->value_free
 * and segfaulting. Checks the exact documented NULL-safe return per
 * function -- rb_insert/-1 (nothing to change), rb_find/NULL (consistent
 * with "absent"), rb_validate/-1 (fails loudly rather than vacuously
 * "valid" -- the specific choice worth pinning down), rb_size/0,
 * rb_foreach/no callback invocations, rb_destroy/no-op (the one case the
 * header itself requires). */
static void test_null_tree_is_safe_everywhere(void) {
    assert(rb_insert(NULL, "k", NULL) == -1);
    assert(rb_find(NULL, "k") == NULL);
    assert(rb_validate(NULL) == -1);
    assert(rb_size(NULL) == 0);

    struct foreach_ctx ctx = {.last_key = NULL, .calls = 0,
                               .strictly_increasing = 1};
    rb_foreach(NULL, foreach_check_order, &ctx);
    assert(ctx.calls == 0);

    rb_destroy(NULL);
}

int main(void) {
    test_create_empty();
    test_insert_find_mixed_order();
    test_insert_duplicate_overwrites_and_frees_old_value();
    test_validate_ordering_on_populated_tree();
    test_foreach_visits_in_increasing_order();
    test_destroy_frees_owned_values();
    test_null_tree_is_safe_everywhere();

    printf("all tests passed\n");
    return 0;
}
