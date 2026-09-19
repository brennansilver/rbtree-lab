#include "rbtree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Deterministic key pool: "k%05d" zero-padding makes lexicographic order
 * equal index order, so an in-order rb_foreach walk can be compared
 * directly against an index-order scan of the model array below with no
 * separate sort step. */
#define POOL_SIZE 2000
#define KEY_LEN 8

/* Reference model: one slot per pool key, tracking whether the tree
 * should currently contain it and, if so, what value it should map to.
 * model_count mirrors what rb_size(t) must report. */
typedef struct {
    int present;
    int value;
} model_entry_t;

static model_entry_t model[POOL_SIZE];
static size_t model_count = 0;
static char pool_keys[POOL_SIZE][KEY_LEN];

static void init_pool(void) {
    for (int i = 0; i < POOL_SIZE; i++) {
        snprintf(pool_keys[i], KEY_LEN, "k%05d", i);
    }
}

static int *make_int(int v) {
    int *p = malloc(sizeof *p);
    assert(p != NULL);
    *p = v;
    return p;
}

/* Cursor-driven full-content check: walks the model in index order
 * (already key-sorted, see pool comment above) and, for every
 * rb_foreach callback, asserts it names the next present model entry in
 * sequence. This is a merge-style comparison -- it catches a lost,
 * duplicated, or reordered node that rb_size + ordering-only
 * rb_validate could in principle miss, since neither of those checks
 * the tree's actual contents against an independent source of truth. */
typedef struct {
    int next_idx;
    size_t calls;
} full_check_ctx_t;

static void full_check_cb(const char *key, void *value, void *ctx_v) {
    full_check_ctx_t *ctx = ctx_v;
    while (ctx->next_idx < POOL_SIZE && !model[ctx->next_idx].present) {
        ctx->next_idx++;
    }
    /* If the tree has a phantom entry beyond every present model slot,
     * fail loudly here instead of reading model[]/pool_keys[] out of
     * bounds below. */
    assert(ctx->next_idx < POOL_SIZE);
    assert(strcmp(key, pool_keys[ctx->next_idx]) == 0);
    assert(*(int *)value == model[ctx->next_idx].value);
    ctx->next_idx++;
    ctx->calls++;
}

static void full_check(const rbtree_t *t, long op) {
    full_check_ctx_t ctx = {.next_idx = 0, .calls = 0};
    rb_foreach(t, full_check_cb, &ctx);
    if (ctx.calls != model_count) {
        fprintf(stderr, "full_check mismatch at op %ld: foreach saw %zu "
                         "entries, model has %zu\n",
                op, ctx.calls, model_count);
        assert(ctx.calls == model_count);
    }
}

static void check_invariants(const rbtree_t *t, long op) {
    if (rb_validate(t) != 0) {
        fprintf(stderr, "rb_validate failed at op %ld\n", op);
        assert(0);
    }
    if (rb_size(t) != model_count) {
        fprintf(stderr, "rb_size mismatch at op %ld: got %zu, want %zu\n",
                op, rb_size(t), model_count);
        assert(0);
    }
    full_check(t, op);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 1;
    }
    long iterations = strtol(argv[1], NULL, 10);
    unsigned seed = (argc >= 3) ? (unsigned)strtoul(argv[2], NULL, 10)
                                : (unsigned)time(NULL);
    printf("fuzz: %ld iterations, seed %u\n", iterations, seed);
    srand(seed);

    init_pool();
    rbtree_t *t = rb_create(free);
    assert(t != NULL);

    /* Early fuzzer: insert + find only, since rb_delete doesn't exist
     * yet. Add a third op case here (with matching model bookkeeping in
     * check_invariants/full_check) once rb_delete lands. */
    for (long op = 0; op < iterations; op++) {
        int idx = rand() % POOL_SIZE;
        int do_insert = (rand() % 2) == 0;

        if (do_insert) {
            int value = rand();
            assert(rb_insert(t, pool_keys[idx], make_int(value)) == 0);
            if (!model[idx].present) {
                model[idx].present = 1;
                model_count++;
            }
            model[idx].value = value;
        } else {
            /* Small fixed chance of probing a key entirely outside the
             * pool namespace, to also exercise the guaranteed-absent
             * path (every pool key is exactly KEY_LEN-1 chars; this one
             * isn't, so it can never collide with a real pool key). */
            if (rand() % 20 == 0) {
                assert(rb_find(t, "not-in-pool") == NULL);
            } else {
                void *found = rb_find(t, pool_keys[idx]);
                if (model[idx].present) {
                    if (found == NULL || *(int *)found != model[idx].value) {
                        fprintf(stderr, "find mismatch at op %ld for %s\n",
                                op, pool_keys[idx]);
                        assert(0);
                    }
                } else {
                    assert(found == NULL);
                }
            }
        }

        if ((op + 1) % 100 == 0) {
            check_invariants(t, op);
        }
    }

    check_invariants(t, iterations);
    rb_destroy(t);

    printf("fuzz: all %ld ops passed\n", iterations);
    return 0;
}
